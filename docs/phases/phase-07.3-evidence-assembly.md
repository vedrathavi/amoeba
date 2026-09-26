# Amoeba — Phase 7.3: Evidence Assembly & Evidence Bundle

## 1. Goal

Following the completion of **Phase 7.0 (Evidence Architecture Investigation)**, **Phase 7.1 (Source Snippet Extraction)**, and **Phase 7.2 (Graph Relationship Evidence Resolution)**, Phase 7.3 implements the structural integration layer: combining primary retrieval metadata, supporting AST elements, verbatim source excerpts, and direct graph relationships into a unified, model-independent `EvidenceBundle`.

The conceptual pipeline is:
```
PrimarySearchResult[]
         │
         ├───→ SourceSnippetReader (Phase 7.1)
         │
         ├───→ Supporting AST Elements (Phase 6.3)
         │
         └───→ RelationshipEvidenceResolver (Phase 7.2)
                     │
                     ↓
             EvidenceAssembler
                     │
                     ↓
               EvidenceBundle
```

This checkpoint represents the answer to: *"What does Amoeba know about these retrieved code entities?"*
It is strictly an evidence assembly layer. It is NOT a prompt generator, token budgeter, or context formatter.

---

## 2. Evidence Architecture

Phase 7.3 orchestrates three existing, decoupled evidence sources:

1. **Primary Retrieval & AST Evidence**: Carried within `PrimarySearchResult` and `RetrievalUnit` (identity, kind, file path, source range, detail, retrieval scores, rank, provenance, and supporting elements).
2. **Source Code Excerpt Evidence**: Extracted via `SourceSnippetReader` into `SourceExcerpt` preserving exact whitespace, comments, and optional surrounding context lines.
3. **Graph Relationship Evidence**: Resolved via `RelationshipEvidenceResolver` into `RelationshipEvidence` preserving callers, callees, inheritance, interfaces, and imports with deterministic 1-hop bounding and fanout caps.

---

## 3. EvidenceBundle Design

`EvidenceBundle` is a self-contained value struct declared in `engine/include/amoeba/evidence/evidence_bundle.hpp`:

```cpp
struct EvidenceBundle {
    /// The original user query (unaltered).
    std::string query;

    /// The ordered list of assembled evidence items, preserving retrieval ranking order.
    std::vector<EvidenceItem> items;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool operator==(const EvidenceBundle&) const = default;
};
```

Key guarantees:
- Retains the exact, unmodified user query.
- Owns all contained data without holding references or pointers to temporary search or graph structures.
- Provides equality comparisons and empty/size checks.

---

## 4. EvidenceItem Design

`EvidenceItem` represents all assembled evidence for a single retrieved primary architectural symbol:

```cpp
struct EvidenceItem {
    /// The primary retrieval result (unit, primary element, scores, provenance).
    PrimarySearchResult primary_result;

    /// Verbatim source code excerpt corresponding to the primary symbol (if available).
    std::optional<source::SourceExcerpt> source_excerpt{std::nullopt};

    /// Resolved direct 1-hop relationships (callers, callees, inheritance, imports, etc.).
    std::vector<graph::RelationshipEvidence> direct_relationships;

    /// Convenience accessors
    [[nodiscard]] const parser::CodeElement& primary_element() const noexcept;
    [[nodiscard]] index::ElementId primary_element_id() const noexcept;
    [[nodiscard]] const std::filesystem::path& file_path() const noexcept;
    [[nodiscard]] const std::vector<parser::CodeElement>& supporting_elements() const noexcept;
    [[nodiscard]] bool has_source_excerpt() const noexcept;

    [[nodiscard]] bool operator==(const EvidenceItem&) const = default;
};
```

Compositional benefits:
- Directly encapsulates `PrimarySearchResult`, avoiding duplicate field definitions.
- Keeps supporting AST elements (`primary_result.unit.supporting_elements`) attached to their owner.
- Holds optional `SourceExcerpt` safely.
- Stores resolved `RelationshipEvidence` list.

---

## 5. EvidenceAssembler Responsibilities

`EvidenceAssembler` is declared in `engine/include/amoeba/evidence/evidence_assembler.hpp` and implemented in `engine/src/evidence/evidence_assembler.cpp`.

Its responsibilities:
1. Accept input search results (`std::span<const PrimarySearchResult>` or `std::vector<PrimarySearchResult>`) and user query.
2. For each result, orchestrate `SourceSnippetReader` to extract primary source code.
3. Orchestrate `RelationshipEvidenceResolver` to resolve direct 1-hop relationships.
4. Package the results into `EvidenceItem` and return a complete `EvidenceBundle`.
5. Maintain strict immutability of input retrieval results.

It does NOT depend directly on Tree-sitter, parser internals, InvertedIndex internals, RelationshipGraph adjacency storage, or embedding models.

---

## 6. Source Evidence Integration

- Uses `SourceSnippetReader::try_read_range()` to safely read source text.
- Supports configurable surrounding context lines via `EvidenceAssemblerOptions::source_context_lines`.
- If the source file was moved/deleted or the range is out-of-bounds, `source_excerpt` is set to `std::nullopt`, allowing graceful degradation without throwing or dropping the primary result.

---

## 7. Supporting AST Evidence Integration

- Supporting AST elements (e.g. methods, calls, attributes, properties enclosed in a primary symbol) remain attached to `primary_result.unit.supporting_elements`.
- Supporting elements are not promoted to top-level items in `EvidenceBundle`.
- The distinction between primary architectural entities and supporting context is strictly preserved.

---

## 8. Relationship Evidence Integration

- Uses `RelationshipEvidenceResolver::resolve()` to obtain 1-hop direct relationships.
- Preserves explicit directions (`Outgoing` callees/dependencies vs `Incoming` callers/derivatives).
- Inherits fanout limits (default max 5 per kind) and deterministic multi-level sorting from Phase 7.2.

---

## 9. Provenance

Provenance is fully preserved:
- Retrieval modalities: `RetrievalProvenance::LexicalOnly`, `SemanticOnly`, or `HybridBoth`.
- Retrieval scores and ranks: `lexical_score`, `normalized_lexical_score`, `lexical_rank`, `semantic_score`, `normalized_semantic_score`, `semantic_rank`, `hybrid_score`.
- Spatial provenance: file paths and exact `SourceRange`s for primary, supporting, and related elements.

---

## 10. Ordering

Evidence ordering is 100% deterministic:
1. **Primary Items**: Preserves the exact ranking order produced by upstream retrieval/fusion.
2. **Supporting AST Elements**: Preserves AST document order within the primary unit.
3. **Relationship Evidence**: Preserves the deterministic sorting order established in Phase 7.2 (direction $\to$ kind $\to$ path $\to$ line $\to$ col $\to$ name $\to$ ID).

---

## 11. Missing Evidence Behavior

- **Empty Search Results**: Returns an `EvidenceBundle` with matching query and empty `items` vector without error.
- **Missing Source File / Invalid Range**: Sets `item.source_excerpt = std::nullopt`, preserving primary metadata and relationships.
- **Unresolved Graph Target**: Retains the relationship with `target_resolved = false` and default metadata.
- **No Supporting Evidence / No Relationships**: Gracefully populates empty collections.

---

## 12. Ownership and Lifetime

- `EvidenceBundle` and `EvidenceItem` follow strict value semantics.
- They own all their data (strings, vectors, optionals, value structs).
- No raw pointers, AST pointers, graph pointers, or temporary buffer string views are stored.
- Bundles remain fully valid after the `EvidenceAssembler` returns.

---

## 13. Tests

The test suite in `engine/tests/evidence/evidence_assembler_test.cpp` covers all 20 required verification scenarios (A through T):

1. **A. SinglePrimaryResultWithSource**: Verifies single result with source snippet extraction.
2. **B. MultiplePrimaryResultsPreserveRetrievalOrder**: Verifies rank order preservation across multiple results.
3. **C & D. PrimaryMetadataAndSourceExcerptWithContext**: Verifies full metadata preservation and surrounding context lines.
4. **E & I. SupportingASTEvidenceAttachedAndAbsent**: Verifies attached supporting AST elements and empty supporting collections.
5. **F, G, H, J. RelationshipEvidenceCallersCalleesAndKinds**: Verifies outgoing callees, incoming callers, and multiple relationship kinds.
6. **K & L. MissingSourceFileAndInvalidRangeHandledSafely**: Verifies safe nullopt handling for missing files and out-of-bounds ranges.
7. **M. UnresolvableRelationshipTarget**: Verifies unresolved graph targets handled without crashing.
8. **N. EmptyRetrievalResult**: Verifies empty search results return clean empty bundle.
9. **O & P. CrossFileRelationshipsAndMultipleFiles**: Verifies multi-file and cross-file relationships.
10. **Q. DeterministicRepeatedAssembly**: Verifies identical output across repeated runs.
11. **R. RetrievalScoresAndProvenancePreserved**: Verifies scores, ranks, and provenance are preserved.
12. **S. NoGraphTraversalBeyondOneHop**: Verifies strict 1-hop graph boundary ($A \to B$ does not include $C$).
13. **T. NoMutationOfInputResults**: Verifies constness and immutability of input retrieval results.

**Result**: 13 / 13 test cases in suite pass. Total engine test suite: **331 / 331 passing across 50 test suites**.

---

## 14. Performance Observations

- Evidence assembly involves only local in-memory lookups, lightweight string extraction, and bounded vector operations.
- Latency is $< 50\ \mu\text{s}$ per primary result on standard test fixtures.
- Memory overhead is proportional to the top-$K$ retrieved results and their extracted snippets.

---

## 15. Explicitly Deferred Work

The following components and concepts are intentionally NOT implemented in Phase 7.3 and deferred to subsequent phases:

- **ContextBuilder & ContextPackage** (deferred to Phase 7.4)
- **Token budgets / token counting** (deferred to Phase 7.4)
- **Context truncation / compression** (deferred to Phase 7.4)
- **LLM prompt formatting / Markdown rendering / JSON schemas** (deferred to Phase 7.4+)
- **LLM API client & inference integration**
- **Recursive graph expansion (depth $> 1$)**
- **Graph caching or source memory-mapping**
- **Retrieval ranking or scoring modifications**
