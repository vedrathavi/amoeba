# Phase 6.7 — Final Retrieval Hardening & Phase 6 Closure

**PHASE 6 RETRIEVAL FOUNDATION: FINAL AUDIT, ADVERSARIAL EVALUATION & FORMAL SIGN-OFF**

---

## 1. Phase 6 History & Architectural Evolution

Phase 6 established Amoeba's core retrieval and understanding capabilities across progressive iterations:

* **Phase 6.0 (Semantic Retrieval Foundation)**: Integrated dense vector embeddings, cosine similarity, `SemanticDocument`, and in-memory `SemanticIndex`.
* **Phase 6.1 (Real MiniLM Embeddings)**: Incorporated the 384-dimensional `all-MiniLM-L6-v2` ONNX/C++ model with ONNX Runtime execution.
* **Phase 6.2 (Hybrid Retrieval)**: Implemented `ScoreNormalizer` (min-max) and `FusionStrategy` (Weighted Linear & Reciprocal Rank Fusion) combining BM25 and dense semantic scores.
* **Phase 6.3 (Retrieval Unit Model)**: Introduced the distinction between raw AST `CodeElement` nodes and standalone `RetrievalUnit` primary architectural symbols, with supporting evidence resolution.
* **Phase 6.4 (Primary Retrieval Pipeline Integration)**: Connected `RetrievalUnit` and `SupportingEvidenceResolver` into the native C++ retrieval path via `PrimaryRetrievalPipeline`.
* **Phase 6.5 (Native Retrieval Evaluation & Hardening)**: Validated candidate-space reduction (93.5%) and established the frozen 10-query holdout benchmark over `demo_test_projects/calendar`.
* **Phase 6.6 (Query Understanding & Retrieval Refinement)**: Introduced `QueryUnderstanding`, compound identifier reconstruction (`use calendar` $\to$ `useCalendar`), and intent-adaptive hybrid fusion.
* **Phase 6.7 (Final Retrieval Hardening & Closure)**: Performed adversarial test suite verification, determinism checks, cross-file identity validation, and formal Phase 6 contract sign-off.

---

## 2. Final Retrieval Architecture

```text
                        ┌───────────────────────────────┐
                        │          User Query           │
                        └───────────────┬───────────────┘
                                        │
                                        ▼
                        ┌───────────────────────────────┐
                        │      QueryUnderstanding       │
                        │  ├── Text Normalization       │
                        │  ├── Compound Synthesis       │
                        │  └── Intent Classification    │
                        └───────┬───────────────┬───────┘
                                │               │
          ┌─────────────────────┘               └─────────────────────┐
          ▼                                                           ▼
┌───────────────────────────────┐                           ┌───────────────────────────────┐
│     Lexical Retrieval         │                           │      Semantic Retrieval       │
│  ├── InvertedIndex Lookup     │                           │  ├── Pretrained MiniLM-384    │
│  ├── Exact & Compound Match   │                           │  ├── Primary-only Units Index │
│  └── CodeAwareRanker Scoring  │                           │  └── Cosine Similarity Match  │
└──────────────┬────────────────┘                           └───────────────┬───────────────┘
               │                                                            │
               └──────────────────────────────┬─────────────────────────────┘
                                              │
                                              ▼
                        ┌───────────────────────────────────────────┐
                        │         Hybrid Fusion & Normalization     │
                        │  ├── ScoreNormalizer (Min-Max)            │
                        │  ├── Intent-Guided Adaptive Alpha (0.75)  │
                        │  └── Modality Purity Guards (a=1.0, 0.0)  │
                        └─────────────────────┬─────────────────────┘
                                              │
                                              ▼
                        ┌───────────────────────────────────────────┐
                        │          PrimaryRetrievalPipeline         │
                        │  ├── RetrievalUnit (Primary Symbol View)  │
                        │  └── Attached Supporting Evidence Context │
                        └─────────────────────┬─────────────────────┘
                                              │
                                              ▼
                        ┌───────────────────────────────────────────┐
                        │            PrimarySearchResult[]          │
                        │  (Zero supporting element pollution)      │
                        └───────────────────────────────────────────┘
```

---

## 3. Regression Benchmark History (Frozen Holdout N=10)

The table below tracks retrieval performance across Phase 6 milestones over the same frozen holdout (`demo_test_projects/calendar`, 27 source files, 2,820 raw elements $\to$ 183 primary units):

| Method / Phase | Architecture | P@1 | P@3 | P@5 | Rec@5 | Rec@10 | MRR | NDCG@5 | NDCG@10 | Latency |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **Raw Baseline (Phase 6.5)** | BEFORE | 0.400 | 0.367 | 0.300 | 1.400 | 1.800 | 0.558 | 0.916 | 1.067 | 33.9 ms |
| **Raw CodeAware (Phase 6.5)** | BEFORE | 0.600 | 0.400 | 0.320 | 1.300 | 1.950 | 0.652 | 0.950 | 1.170 | 36.8 ms |
| **Raw Semantic (Phase 6.5)** | BEFORE | 0.100 | 0.100 | 0.100 | 0.400 | 0.400 | 0.231 | 0.269 | 0.269 | 11.9 ms |
| **Unit Baseline (Phase 6.5)** | AFTER (Units) | 0.400 | 0.233 | 0.200 | 0.800 | 0.800 | 0.553 | 0.607 | 0.607 | 37.0 ms |
| **Unit BM25 (Phase 6.5)** | AFTER (Units) | 0.600 | 0.233 | 0.200 | 0.900 | 0.900 | 0.676 | 0.725 | 0.725 | 36.1 ms |
| **Unit CodeAware (Phase 6.5)** | AFTER (Units) | 0.600 | 0.300 | 0.200 | 0.800 | 0.900 | 0.679 | 0.685 | 0.717 | 36.6 ms |
| **Unit Hybrid α=0.5 (Phase 6.5)** | AFTER (Units) | 0.400 | 0.267 | 0.160 | 0.700 | 0.700 | 0.550 | 0.568 | 0.568 | 35.9 ms |
| **Unit CodeAware (Phase 6.6)** | AFTER (6.6) | **0.700** | **0.333** | **0.220** | **0.900** | **1.000** | **0.771** | **0.785** | **0.817** | 37.3 ms |
| **Unit BM25 (Phase 6.6)** | AFTER (6.6) | **0.700** | **0.267** | **0.220** | **1.000** | **1.000** | **0.770** | **0.825** | **0.825** | 36.7 ms |
| **Unit Hybrid α=0.5 (Phase 6.6)** | AFTER (6.6) | **0.500** | **0.300** | **0.220** | **0.950** | **0.950** | **0.662** | **0.717** | **0.717** | 36.9 ms |
| **Unit CodeAware (Phase 6.7)** | FINAL (6.7) | **0.700** | **0.333** | **0.220** | **0.900** | **1.000** | **0.771** | **0.785** | **0.817** | 37.3 ms |
| **Unit BM25 (Phase 6.7)** | FINAL (6.7) | **0.700** | **0.267** | **0.220** | **1.000** | **1.000** | **0.770** | **0.825** | **0.825** | 36.7 ms |
| **Unit Hybrid α=0.5 (Phase 6.7)** | FINAL (6.7) | **0.500** | **0.300** | **0.220** | **0.950** | **0.950** | **0.662** | **0.717** | **0.717** | 36.9 ms |

---

## 4. Adversarial & Hardening Evaluation

A dedicated adversarial test suite (`engine/tests/retrieval/retrieval_hardening_test.cpp`) was added to stress-test the retrieval pipeline across 10 critical operational dimensions:

1. **Exact Identifier Queries**: `formatDate`, `AuthController` resolve directly to Rank #1.
2. **Naturally-Spaced Compound Queries**: `"use calendar"` $\to$ `useCalendar`, `"user profile card"` $\to$ `UserProfileCard`.
3. **Case Convention Invariance**: `useLocalStorage`, `use_local_storage`, and `UseLocalStorage` all resolve to the same primary unit.
4. **Natural Language Conceptual Queries**: Long descriptive questions trigger `QueryIntent::NaturalLanguage` ($\alpha \le 0.3$) and surface relevant primary units safely.
5. **Ambiguous Short Queries**: Broad terms (`"user"`, `"date"`) return structured primary units without ranking anomalies or panics.
6. **Mixed Technical Queries**: `"formatDate string formatting utility"` successfully balances lexical identifier match with contextual description.
7. **Strict Determinism**: Repeated query executions produce bit-for-bit identical candidate ordering, hybrid scores, and provenance.
8. **Edge Cases**: Empty queries, whitespace-only, punctuation-only (`!@#$%^&*()`), 1-character, non-existent symbols, file-path syntax, and 500+ character queries execute safely and return clean responses.
9. **Cross-File Symbol Disambiguation**: Identical function names across distinct files (e.g. `FormA.tsx::handleSubmit` vs `FormB.tsx::handleSubmit`) remain independently addressable without identity collisions.
10. **Supporting Evidence Isolation**: Queries for terms prominent in AST evidence (`useState`, `className`, `text-xl font-bold`, `console.log`) surface only their owning primary units; zero supporting elements compete as top-level candidates.

---

## 5. Architectural & Quality Audit

* **Single Responsibility Principle (SRP)**:
  * `QueryUnderstanding`: Query analysis, normalization, and intent detection.
  * `SearchEngine`: Inverted index lexical candidate retrieval and scoring.
  * `SemanticRetriever`: Dense embedding projection and cosine candidate retrieval.
  * `FusionStrategy`: Mathematical score combination (weighted linear & RRF).
  * `SupportingEvidenceResolver`: Structural AST ownership and evidence grouping.
  * `PrimaryRetrievalPipeline`: High-level orchestration and result production.
* **Ownership & Lifetimes**: Explicit value semantics and const-reference sharing; no naked pointers or ambiguous lifetimes.
* **Public API Boundaries**: Headers expose clean value structs and domain types without leaking Tree-sitter AST pointers or ONNX Runtime internals.
* **Multi-Language Regression**: Full test coverage verified across all 10 supported languages (C/C++, Python, Java, Go, Rust, JavaScript, TypeScript, TSX, HTML, CSS).
* **Test Suite Status**: **278 / 278 tests passing** across 46 test suites in 1,710 ms.
* **Compiler Cleanliness**: 0 compiler warnings (`-Wall -Wextra -Wpedantic`).
* **Formatting**: 100% compliant with project `clang-format` specification.

---

## 6. Performance & Memory Sanity

* **Query Understanding**: $< 0.005$ ms ($< 5$ μs) per query.
* **Lexical Candidate Retrieval**: $< 0.5$ ms.
* **Semantic Vector Embedding (MiniLM CPU)**: $\approx 10$–$11$ ms per query.
* **Score Normalization & Fusion**: $< 0.05$ ms.
* **Total Pipeline Query Latency**: $\approx 36$–$44$ ms on CPU.
* **Memory Invariant**: `RetrievalUnit` holds references to primary symbols and supporting evidence indices without duplicating raw source buffers.

---

## 7. The Phase 6 Retrieval Contract

### What Phase 6 Guarantees:
1. **Primary Symbol Unit of Retrieval**: All top-level search results returned by `PrimaryRetrievalPipeline` are guaranteed to be primary architectural symbols (`Class`, `Struct`, `Interface`, `Function`, `Method`, `Component`, `Route`).
2. **Attached Evidence**: Supporting AST elements (`Call`, `Attribute`, `JSXElement`, `UtilityClass`, `Property`, `Include`) act strictly as evidence attached to their owning primary unit and never appear as independent top-level results.
3. **Compound Identifier Resolution**: Natural-space identifier queries (e.g., `"use calendar"`) are synthesized and matched against normalized code symbols.
4. **Intent-Guided Hybrid Fusion**: Dense semantic retrieval and lexical BM25/CodeAware ranking are fused adaptively based on query intent.
5. **Deterministic Results**: Retrieval ordering, scores, and candidate sets are 100% deterministic and repeatable.

### What Phase 6 Does NOT Guarantee (Deferred to Future Phases):
1. **Natural Language Code Generation or Synthesis**: Amoeba retrieval finds and ranks existing source code symbols; it does not generate new code.
2. **Multi-Hop Architectural Reasoning**: Deep cross-repository multi-hop question answering is left for downstream reasoning layers (Phase 7+).
3. **Conversational Agent Explanations**: Natural language explanation of retrieved code is deferred to the LLM integration phase.

---

## 8. Phase 6 Formal Closure Decision

Phase 6 is **officially complete and closed**. The retrieval architecture has been audited, hardened, stress-tested, and verified against all functional and regression requirements.
