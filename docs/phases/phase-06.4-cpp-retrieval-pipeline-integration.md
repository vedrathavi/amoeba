# Phase 6.4 — C++ Retrieval Pipeline Integration

## 1. Problem Statement

Amoeba's source-code search has evolved through multiple retrieval phases. By Phase 6.2 (Hybrid Retrieval), the production pipeline retrieves and ranks **all** parsed `CodeElement` kinds as equivalent candidates:

```
InvertedIndex (ALL elements)
    ↓
SearchEngine
    ↓
{Class, Function, Method, Component, Call, Attribute, JSXElement, UtilityClass, ...}
    ↓
Ranking (Baseline / BM25 / CodeAware)
```

The real-world holdout evaluation (Phase 6.2) on the calendar corpus revealed that this causes **retrieval competition between architectural symbols and fine-grained AST sub-elements**:

> For a query: *"Where is the destination image and travel description rendered for each month?"*  
> A `className` attribute inside `getImagePanelData()` matched and ranked above the `Component` that the developer intended to find.

This is not a parser error — the parser correctly extracted all elements. The problem is that retrieval treats `className` (an Attribute) as equally valid a top-level result as `getImagePanelData` (a Function).

---

## 2. Phase 6.2 Discovery

Real-world holdout evaluation (frozen N=10 queries, calendar corpus):

| Pipeline | P@1 |
| :--- | :--- |
| Raw CodeElement retrieval | 0.10 |
| RetrievalUnit-aware retrieval | 0.70 |

The 60-point P@1 improvement came from simply changing **what** is presented as a top-level candidate — not from changing the ranking algorithms.

> This is a retrieval representation problem, not a ranking algorithm problem.

---

## 3. Phase 6.3 Solution

Phase 6.3 introduced a clean C++ abstraction layer:

```
CodeElement   = structural/AST representation (parser output, unchanged)
RetrievalUnit = search-oriented representation (view over existing elements)
```

`RetrievalUnitClassifier` — deterministic policy:

| Primary (top-level candidates) | Supporting (context/evidence) |
| :--- | :--- |
| Class, Struct, Interface | Call |
| Function, Method | Attribute, JSXElement, JSXComponent |
| Component, Hook, Route | UtilityClass, Include, Property, Selector |

`SupportingEvidenceResolver` builds a hierarchy per ParsedFile — but **Phase 6.3 intentionally did not integrate with the production retrieval pipeline**.

---

## 4. Architecture Before Phase 6.4

```
ParsedFile.elements[]
    │
    ├──▶ InvertedIndex::add_parsed_file()
    │       indexes ALL elements (Class, Function, Method, Call, Attribute, ...)
    │
    ├──▶ SemanticTextFormatter.format() × N_elements
    │       embeds ALL elements independently
    │
    ├──▶ SemanticIndex (ALL element embeddings)
    │
SearchEngine.search()
    │   postings lookup → ALL element kinds compete
    ↓
SemanticRetriever.retrieve()
    │   cosine similarity over ALL element embeddings
    ↓
HybridRetriever.search()
    │   fusion of above → ALL element kinds in final result
    ↓
SearchResult[] / HybridSearchResult[]
    (mix of Class, Function, Call, Attribute, JSXElement, UtilityClass, ...)
```

---

## 5. Architecture After Phase 6.4

```
ParsedFile.elements[]
    │
    ├──▶ InvertedIndex::add_parsed_file()       ← UNCHANGED (all elements indexed)
    │
    └──▶ PrimaryRetrievalPipeline (NEW)
            │
            ├── SupportingEvidenceResolver
            │       → RetrievalUnit[] (primary + supporting attached)
            │
            ├── reconcile_element_ids()          ← Phase 6.4: patches primary IDs
            │       → primary_element_id = InvertedIndex ElementId
            │
            ├── SemanticTextFormatter.format_unit()  ← Phase 6.4: enriched text
            │       → PRIMARY units only embedded
            │
            └── SemanticIndex (PRIMARY unit embeddings only)

PrimaryRetrievalPipeline.search(query)
    │
    ├── SearchEngine.search()
    │       lexical results (any ElementKind match from InvertedIndex)
    │       ↓ ElementMatchKey lookup → InvertedIndex ElementId
    │       ↓ element_id_to_unit_ map → primary RetrievalUnit
    │
    ├── SemanticRetriever.retrieve_text()
    │       semantic results (primary units only — per SemanticIndex)
    │       ↓ already primary-keyed
    │
    ├── ScoreNormalizer.min_max_normalize()
    ├── FusionStrategy.fuse_weighted() or fuse_rrf()
    │
    └── PrimarySearchResult[]
            ├── unit (RetrievalUnit: primary + supporting evidence)
            ├── lexical_score / semantic_score / hybrid_score
            └── provenance (LexicalOnly / SemanticOnly / HybridBoth)
```

---

## 6. Design Alternatives Considered

### Option A — Retrieve Raw Elements Then Map (Rejected)

Post-hoc mapping: run existing retrieval, then map results to their primary unit.

**Verdict**: Does NOT solve the problem. Supporting elements still compete in the retrieval scoring phase. A `className` Attribute that scores above `getImagePanelData` Function in the ranker cannot be recovered by post-hoc mapping — it was already awarded the top-1 slot.

### Option B — Primary-Only Index (Rejected)

Remove all supporting elements from the InvertedIndex and SemanticIndex.

**Verdict**: Loses valuable lexical signal. A `getMonthData()` call inside `renderCalendar()` makes `renderCalendar` findable via "getMonthData". Removing supporting elements would hurt recall on structural queries.

### Option C — Primary Retrieval + Supporting Evidence (Chosen)

Keep the InvertedIndex with ALL elements (for full lexical coverage). After lexical retrieval, map any hit on a supporting element to its owning primary unit via `element_id_to_unit_`. Build the SemanticIndex from PRIMARY unit enriched text representations only.

**Why this wins**:
- Retains all lexical signal (supporting elements contribute via `parent_context` in the InvertedIndex)
- Eliminates supporting elements from top-level result competition
- Works without modifying InvertedIndex, BaselineRanker, BM25, CodeAwareRanker
- Compatible with Phase 5 relationship graph (relationship graph is unchanged)
- Clean: `PrimaryRetrievalPipeline` wraps the existing pipeline without replacing it

---

## 7. Chosen Architecture Details

### 7.1 ElementId Reconciliation

Phase 6.3's `SupportingEvidenceResolver` assigns `primary_element_id` as the element's position within `ParsedFile::elements` (per-file index). This is NOT the same as the `InvertedIndex` ElementId, which is global across all files.

Phase 6.4 introduces `reconcile_element_ids()` — called at construction, it patches each `RetrievalUnit::primary_element_id` to the corresponding `InvertedIndex::ElementId` using `ElementMatchKey` lookup. Without this step, two units from different files sharing position 0 in their respective `ParsedFile::elements` would collide in the `SemanticIndex`.

### 7.2 Enriched Semantic Representation

`SemanticTextFormatter::format_unit()` produces an enriched text for each primary unit:

```
Language: TypeScript
File: src/auth.ts
Kind: Function
Name: authenticateUser
Detail: (credentials: Credentials) => boolean
Calls: checkCredentials validateToken logAttempt
Attributes: className id
```

Key design decisions:
- **Selective evidence**: Only `Call`, `JSXComponent`, `Attribute` kinds are included. `UtilityClass`, `Include`, `Selector`, `Property` are excluded (too noisy).
- **Capped per category**: ≤5 callees, ≤4 JSX components, ≤4 attributes.
- **Deduplication**: duplicate names are suppressed.
- **Primary signal preserved**: The enrichment is additive; the primary element's metadata always dominates.

### 7.3 Candidate Map Keyed by Unit Index

Fusion uses **unit index** (position in `units_` vector) as the deduplication key, not `ElementId`. This guarantees that multiple lexical hits that map to the same primary unit are merged correctly, regardless of which supporting element triggered the match.

### 7.4 Lexical Hit Resolution

```
SearchResult (no element_id) 
    ↓ ElementMatchKey{file_path, name, start_line, start_col}
    ↓ elem_key_to_id (InvertedIndex, built per search call O(N))
    ↓ InvertedIndex ElementId
    ↓ element_id_to_unit_ (built at construction, O(1))
    ↓ unit index → RetrievalUnit
```

The `ElementMatchKey` approach is identical to `HybridRetriever`'s existing pattern. Building it per search call (not cached) mirrors `HybridRetriever` and avoids stale-cache issues.

---

## 8. New Components

| Component | Location | Responsibility |
| :--- | :--- | :--- |
| `RetrievalProvenance` | `retrieval/primary_search_result.hpp` | Which modalities contributed |
| `PrimarySearchResult` | `retrieval/primary_search_result.hpp` | Top-level search result type |
| `PrimarySearchOptions` | `retrieval/primary_retrieval_pipeline.hpp` | Search configuration |
| `PipelineMetrics` | `retrieval/primary_retrieval_pipeline.hpp` | Latency + count measurements |
| `PrimaryRetrievalPipeline` | `retrieval/primary_retrieval_pipeline.hpp/.cpp` | Main integration orchestrator |
| `SemanticTextFormatter::format_unit()` | `semantic/semantic_document.hpp/.cpp` | Enriched unit text |
| `SemanticTextFormatter::create_unit_document()` | same | SemanticDocument from unit |

---

## 9. OOP / LLD Design

### Composition over Inheritance
`PrimaryRetrievalPipeline` owns `SemanticIndex` and `SemanticRetriever` by value, and holds `InvertedIndex` and `EmbeddingProvider` by const reference. No inheritance hierarchy.

### SRP
- `SupportingEvidenceResolver` — resolves ownership (unchanged)
- `SemanticTextFormatter` — formats text (extended with new overload)
- `PrimaryRetrievalPipeline` — orchestrates retrieval (new)
- `FusionStrategy` / `ScoreNormalizer` — unchanged

### OCP
Adding a new retrieval modality (e.g., structural graph retrieval) requires adding a new step inside `search_with_metrics()`. No existing components need modification.

### DIP
`PrimaryRetrievalPipeline` depends on `EmbeddingProvider` (abstract interface), not any concrete provider. Test code uses `DeterministicEmbeddingProvider`.

### ISP
Callers who don't want primary-unit retrieval continue using `SearchEngine`, `HybridRetriever`, `SemanticRetriever` directly — unchanged.

---

## 10. SOLID Compliance

| Principle | Application |
| :--- | :--- |
| SRP | Each class has one reason to change |
| OCP | New modalities don't require rewriting existing components |
| LSP | No polymorphism in new code (not needed) |
| ISP | Old components remain fully usable |
| DIP | `EmbeddingProvider` abstraction preserved throughout |

---

## 11. Ownership and Lifetime

| Resource | Ownership | Lifetime |
| :--- | :--- | :--- |
| `index_` | `const&` reference | Must outlive `PrimaryRetrievalPipeline` |
| `provider_` | `const&` reference | Must outlive `PrimaryRetrievalPipeline` |
| `units_` | Value member | Owned by pipeline |
| `semantic_index_` | Value member | Owned by pipeline |
| `search_engine_` | Value member | Wraps `index_` const ref |
| `semantic_retriever_` | Value member | Wraps `semantic_index_` member |
| `element_id_to_unit_` | Value member | Owned by pipeline |

The pipeline is movable (all members are movable) and non-copyable by default (`InvertedIndex` is non-trivially managed).

---

## 12. Determinism

- `reconcile_element_ids()` produces deterministic results (same InvertedIndex → same mapping).
- `element_id_to_unit_` construction is deterministic (unit ordering is stable from `SupportingEvidenceResolver`).
- `FusionStrategy::fuse_weighted()` and `fuse_rrf()` tie-break by `element_id` ascending.
- `SemanticRetriever` is deterministic (brute-force cosine similarity, stable sort).
- `DeterministicEmbeddingProvider` produces fixed-seed embeddings for tests.

---

## 13. Performance

| Measurement | What it captures |
| :--- | :--- |
| `lexical_ms` | `SearchEngine::search()` duration |
| `semantic_ms` | `SemanticRetriever::retrieve_text()` duration |
| `fusion_ms` | Normalization + fusion + result building |
| `total_ms` | End-to-end pipeline duration |
| `total_elements` | Total CodeElements in InvertedIndex |
| `primary_unit_count` | Primary units (SemanticIndex size) |
| `supporting_count` | Total supporting elements |
| `lexical_candidates` | Raw lexical results before mapping |
| `semantic_candidates` | Semantic results |
| `fused_candidates` | Candidates after union |

Construction cost:
- O(N_elements) for `reconcile_element_ids()` — called once
- O(N_primary_units) embedding calls for `build_semantic_index()`
- O(N_elements) for `build_element_id_map()`

Query cost:
- O(N_elements) for `ElementMatchKey` → `ElementId` map construction (one per search call)
- O(lex_top_k × N_primary) for lexical candidate mapping
- O(sem_top_k) for semantic result ingestion
- O((lex_top_k + sem_top_k) log(lex_top_k + sem_top_k)) for fusion sorting

---

## 14. Testing

### Phase 6.4 Tests (26 tests, `primary_retrieval_pipeline_test.cpp`)

**Construction & Guarantees:**
1. Empty repository → empty pipeline
2. CSS file → synthetic module unit
3. Single primary symbol
4. Multiple primary symbols
5. Class + Method as separate primary units
6. Supporting elements never become top-level results
7. All results have `role == Primary`
8. Provenance enum has valid value

**Semantic Index & Lexical:**
9. SemanticIndex size equals primary unit count
10. SemanticIndex keyed by primary_element_id
11. Exact identifier query
12. Subword query
13. Lexical hit on supporting element surfaces owning primary unit

**Fusion & Metrics:**
14. Weighted fusion alpha=1.0
15. RRF fusion
16. Metrics counts internally consistent

**Required Invariant Tests (10 Invariants):**
17. `Invariant1_SupportingElementsNeverTopLevelCandidates`: Supporting elements (Call, Attribute, JSXElement, UtilityClass) never appear as top-level candidates across all query types and alpha modes.
18. `Invariant2_SupportingLexicalHitMapsToOwningPrimaryUnitWithEvidence`: Lexical hit on `Call: getUserById()` owned by `UserService` maps to `UserService` with `getUserById` in `supporting_elements`.
19. `Invariant3_SemanticQueryRetrievesPrimaryUnitWithEnrichedEvidence`: Semantic query for supporting call token retrieves primary unit whose enriched text contains the call.
20. `Invariant4_SamePrimaryElementIdUsedConsistently`: The same `primary_element_id` is used consistently across `RetrievalUnit`, `SemanticIndex`, lexical mapping, and hybrid fusion.
21. `Invariant5_MultiFileIdenticalPerFilePositionsDoNotCollide`: Multi-file elements at identical per-file positions do not collide and receive distinct global `primary_element_id`s.
22. `Invariant6_MultipleSupportingHitsCollapseIntoSinglePrimaryResult`: Multiple raw supporting hits within the same primary unit collapse into exactly ONE primary result.
23. `Invariant7_DeterministicTieBreaking`: Repeated queries produce deterministic, byte-identical results and ordering.
24. `Invariant8_UnresolvedSupportingEvidenceNotAssignedArbitrarily`: Unresolved supporting evidence is never arbitrarily attached to unrelated primary units.
25. `Invariant9_ExistingRetrieverComponentsBehaviorallyUnchanged`: Direct execution of `SearchEngine`, `BaselineRanker`, `BM25Ranker`, `CodeAwareRanker`, `SemanticRetriever`, and `HybridRetriever` remains behaviorally identical.
26. `Invariant10_Phase5RelationshipComponentsUnchanged`: `RelationshipGraph`, `GraphQueryService`, and `RelationshipAwareSearchEngine` continue to operate cleanly and independently.

---

## 15. Architectural & SRP Audit

### Single Responsibility Principle (SRP) Audit of `PrimaryRetrievalPipeline`
- **Orchestration Boundary**: `PrimaryRetrievalPipeline` orchestrates primary-unit-level hybrid retrieval across lexical and semantic modalities.
- **Delegation of Sub-responsibilities**:
  - Lexical indexing & tokenization: Delegated to `InvertedIndex` and `CodeTokenizer`.
  - Lexical ranking: Delegated to `SearchEngine` / `Ranker` (`Baseline`, `BM25`, `CodeAware`).
  - Vector similarity & semantic search: Delegated to `SemanticRetriever` and `Similarity`.
  - Score normalization & fusion: Delegated to `ScoreNormalizer` and `FusionStrategy`.
  - Evidence assignment: Delegated to `SupportingEvidenceResolver`.
- **Verdict**: `PrimaryRetrievalPipeline` does NOT implement ad-hoc ranking math or vector math; its private methods (`build_units`, `reconcile_element_ids`, `build_semantic_index`, `build_element_id_map`) represent construction lifecycle steps. It is well within SRP bounds and does not require artificial class decomposition.

### Primary Unit Count Explanation (Phase 6.3 ~183 vs Phase 6.4 ~180)
The calendar holdout corpus produces ~183 units in Phase 6.3 graph analysis vs 180 primary `RetrievalUnits` in Phase 6.4 `SupportingEvidenceResolver`:
1. **`JSXComponent` Classification Difference**: In `CodeAwareRanker`, `ElementKind::JSXComponent` was treated as a primary declaration. In `RetrievalUnitClassifier`, `JSXComponent` is classified as `Supporting` (since JSX elements like `<Avatar />` are render calls within an enclosing component/function).
2. **File-level Module Units**: Synthetic module units are only emitted for files with zero primary symbols (e.g., pure CSS files). Pure TS/TSX files containing primary symbols do not generate redundant synthetic module units.

---

## 16. Benchmark Measurements & Claims

### A. Representation-Level Measurement (Frozen Calendar Holdout)
*Corpus: `demo_test_projects/calendar` (27 source files)*

| Metric | Raw CodeElement Level | Primary RetrievalUnit Level | Reduction |
| :--- | :--- | :--- | :--- |
| **Total Candidates Indexed** | 2,820 CodeElements | 180 Primary Units | **93.6% reduction** |
| **Supporting Elements Attached** | N/A (all competed) | 2,640 Supporting Elements | Preserved as evidence |
| **Semantic Document Count** | 2,820 (if all indexed) | 180 Primary Documents | **93.6% reduction** |

**Label**: *Representation-level candidate/document reduction on the measured corpus.*

### B. Retrieval-Quality Measurement
> [!IMPORTANT]
> **Full C++ retrieval-quality benchmark requires a dedicated benchmark entry point and is deferred.**
> 
> The Python evaluation script uses simplified lexical tokenization and does NOT reproduce the real C++ `CodeTokenizer`, field weighting, BM25, or `CodeAwareRanker`. Therefore, Python P@1/MRR figures MUST NOT be used as evidence that Phase 6.4 improves retrieval quality.

---

## 17. Limitations

1. **Dedicated C++ Benchmark Entry Point**: `PrimaryRetrievalPipeline::search()` is verified via 26 comprehensive C++ unit and integration tests. A dedicated command-line benchmark runner executable for large-scale external corpora is deferred to Phase 6.5.
2. **ElementMatchKey Construction Overhead**: Rebuilding the `ElementMatchKey → ElementId` map per search call is O(N_elements). While negligible for sub-millisecond local searches on moderate repositories, caching the mapping at pipeline construction can be added in future optimizations.
3. **Synthetic Module Unit Collisions**: Synthetic module units for files with zero primary symbols use `primary_element_id = 0` if not present in the InvertedIndex. Documented edge case for pure non-code asset files.

---

## 18. Future Extension Boundary

Phase 6.4 intentionally does NOT implement:
- HNSW / FAISS / Vector Databases
- LLM / RAG generation layers
- Search modes (FAST / BALANCED / DEEP)
- Caching layers or query result caches

---

## 19. Final Phase Status

| Acceptance Criterion | Status |
| :--- | :--- |
| Phase 6.3 implementation inspected and reused | ✅ |
| RetrievalUnit integrated into actual C++ retrieval | ✅ |
| Primary RetrievalUnits are top-level candidates | ✅ |
| Supporting Evidence remains available | ✅ |
| Unresolved evidence never arbitrarily attached | ✅ |
| Lexical retrieval works (Baseline / BM25 / CodeAware) | ✅ |
| Semantic retrieval works | ✅ |
| Hybrid weighted fusion works | ✅ |
| RRF works | ✅ |
| Candidate identity deterministic | ✅ |
| ElementId reconciliation correct | ✅ |
| 10 required invariants verified with C++ integration tests | ✅ |
| All previous tests continue passing (250/250) | ✅ |
| Zero compiler warnings (Amoeba codebase) | ✅ |
| clang-format clean | ✅ |
| 26 Phase 6.4 tests passing | ✅ |
| Benchmark claims properly scoped & separated (A & B) | ✅ |
| SRP audit documented | ✅ |
| Primary-unit count discrepancy explained | ✅ |
| Git diff reviewed & status inspected | ✅ |
| Not committed | ✅ |

**PHASE 6.4 READY FOR REVIEW**
