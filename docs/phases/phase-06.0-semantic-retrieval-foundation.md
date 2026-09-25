# Phase 6.0 — Semantic Retrieval Foundation

## 1. Overview & Core Principles

Phase 6.0 establishes the architectural, structural, and evaluation foundation for **Semantic Retrieval** in Amoeba.

Semantic retrieval serves as an **additive retrieval capability** alongside Amoeba's existing inverted index, BM25 ranker, Code-Aware hybrid ranker, and relationship graph.

```text
                    Query
                      │
          ┌───────────┴───────────┐
          ↓                       ↓
   Lexical Retrieval       Semantic Retrieval
   InvertedIndex           Embeddings
          │                       │
          └───────────┬───────────┘
                      ↓
               Candidate Fusion (Future)
                      ↓
               CodeAware Ranking
                      ↓
              Relationship Context
```

---

## 2. Why Semantic Retrieval

### The Vocabulary Mismatch Problem
Lexical search operates on token matching (exact identifiers, subwords, camelCase splits). When a developer's query phrasing differs from the exact identifier or comment names used by the author, lexical retrieval can miss relevant code elements.

Example:
* **User Query**: `"where do we validate user credentials?"`
* **Source Code**:
  ```cpp
  class AuthService {
      bool authenticate(const UserRecord& user, const std::string& token);
  };
  ```
Lexical search requires shared stems or tokens (`validate` vs `authenticate`, `credentials` vs `UserRecord`/`token`). Semantic retrieval projects both queries and code representations into a continuous vector space where semantically related concepts cluster together, bridging the vocabulary gap.

---

## 3. What Is an Embedding & Embedding vs. LLM

### Dense Vector Embeddings
An embedding is a fixed-length dense vector $\mathbf{v} \in \mathbb{R}^D$ where geometric proximity (e.g. cosine angle) reflects conceptual similarity.

### Embedding Model vs. LLM (Clear Separation)
| Component | Primary Function | Computational Profile | Amoeba Architectural Role |
| :--- | :--- | :--- | :--- |
| **Embedding Model** | Maps text/code to dense vectors $\mathbb{R}^D$ | Lightweight, fast inference, indexable | **Retrieval Candidate Generation** |
| **LLM (Large Language Model)** | Autoregressive token generation & reasoning | Heavyweight, high latency | **Future Post-Retrieval Synthesis & Explanation** |

Amoeba separates embedding generation from reasoning: embeddings find candidate code elements, while future LLMs or rankers analyze the focused context.

---

## 4. Semantic Retrieval Pipeline Architecture

Phase 6.0 enforces strict Single Responsibility (SRP) and Dependency Inversion (DIP):
```text
CodeElement
    ↓
Semantic Representation (SemanticTextFormatter)
    ↓
Embedding Provider (EmbeddingProvider / DeterministicEmbeddingProvider)
    ↓
Semantic Index (SemanticIndex)
    ↓
Vector Similarity (Cosine Similarity)
    ↓
Semantic Retriever (SemanticRetriever)
```

```text
Embedding Generation  ≠  Embedding Storage  ≠  Similarity Search
 (EmbeddingProvider)      (SemanticIndex)       (SemanticRetriever)
```

---

## 5. Component Specifications

### A. [`Embedding`](file:///d:/amoeba/engine/include/amoeba/semantic/embedding.hpp)
Compact vector record linking an element ID to dense floating-point values:
```cpp
struct Embedding {
    ElementId element_id{0};
    std::vector<float> values;

    [[nodiscard]] std::size_t dimensions() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
};
```

### B. [`cosine_similarity`](file:///d:/amoeba/engine/include/amoeba/semantic/similarity.hpp)
Computes the angle between vectors:
$$\text{sim}(\mathbf{a}, \mathbf{b}) = \frac{\mathbf{a} \cdot \mathbf{b}}{\|\mathbf{a}\|_2 \|\mathbf{b}\|_2}$$
* **Safety**: Safely returns `0.0f` for zero vectors, empty spans, and dimension mismatches without division by zero or NaN propagation.
* **Range**: Clamped strictly to $[-1.0, 1.0]$.

### C. [`SemanticDocument` & Representation](file:///d:/amoeba/engine/include/amoeba/semantic/semantic_document.hpp)
Transforms a [`CodeElement`](file:///d:/amoeba/engine/include/amoeba/parser/code_element.hpp) into structured text without duplicating entire files:
```text
Language: C++
File: src/auth/auth_service.cpp
Kind: Method
Context: AuthService
Name: authenticateUser
Detail: validates credentials and creates session
```

### D. [`EmbeddingProvider`](file:///d:/amoeba/engine/include/amoeba/semantic/embedding_provider.hpp) & Test Double
- Abstract interface with `embed(text)` and `embed_batch(texts)`.
- [`DeterministicEmbeddingProvider`](file:///d:/amoeba/engine/include/amoeba/semantic/deterministic_embedding_provider.hpp): A deterministic, normalized hash-projection test double used exclusively for unit and integration testing of the retrieval pipeline mechanics. *(Documented as a test double, not a production semantic model).*

### E. [`SemanticIndex`](file:///d:/amoeba/engine/include/amoeba/semantic/semantic_index.hpp)
In-memory vector store mapping `ElementId -> Embedding`:
- Operations: `add`, `replace`, `remove`, `lookup`, `size`, `clear`, `estimate_memory_bytes`.
- Enforces consistent dimensionality across indexed vectors.

### F. [`SemanticRetriever`](file:///d:/amoeba/engine/include/amoeba/semantic/semantic_retriever.hpp)
- Performs exhaustive brute-force cosine similarity search against stored embeddings.
- Returns ranked [`SemanticSearchResult`](file:///d:/amoeba/engine/include/amoeba/semantic/semantic_retriever.hpp) records sorted by similarity descending, with deterministic tie-breaking on `ElementId`.

---

## 6. Evaluation & Research Benchmark Foundation

Per Section 21 of the Phase 6 specification, evaluation infrastructure is strictly decoupled from production search:

### A. [`RankingMetrics`](file:///d:/amoeba/engine/include/amoeba/eval/ranking_metrics.hpp)
Reusable computation of standard IR evaluation metrics:
* Precision@K ($P@1, P@3, P@5, P@10$)
* Recall@K ($R@5, R@10$)
* Mean Reciprocal Rank ($MRR$)
* Normalized Discounted Cumulative Gain ($NDCG@5, NDCG@10$)

### B. [`BenchmarkResult`](file:///d:/amoeba/engine/include/amoeba/eval/benchmark_result.hpp)
Machine-readable container recording full experimental context and serializable to:
* Structured **JSON** (`to_json()`)
* Standard **CSV** (`to_csv_header()`, `to_csv_row()`)

Captures:
* Dataset & experiment identifiers
* Corpus statistics (files, elements, terms, relationships)
* Embedding dimensionality
* Performance metrics (indexing time, query latency, memory usage)
* Evaluated IR ranking metrics
* Environment metadata (OS, build configuration, compiler)

---

## 7. Current Scope & Non-Goals

### Supported in Phase 6.0:
* Core semantic vector abstractions (`Embedding`, `SemanticDocument`)
* Robust, safe `cosine_similarity`
* Structured semantic representation policy for code elements
* Decoupled `EmbeddingProvider` interface and deterministic test double
* In-memory `SemanticIndex`
* Brute-force exhaustive `SemanticRetriever`
* Reusable research evaluation metrics and machine-readable benchmark exporters

### Explicitly Excluded (Future Phases):
* ❌ Production pretrained neural model / ONNX runtime integration (Phase 6.1+)
* ❌ Vector databases (Qdrant, Milvus, Chroma, SQLite)
* ❌ Approximate Nearest Neighbor (ANN / HNSW / FAISS)
* ❌ Reciprocal Rank Fusion (RRF) / Hybrid candidate fusion
* ❌ Large Language Models (LLM) / RAG pipelines / LangChain
* ❌ C++ in-engine plotting / visualization dashboards

---

## 8. Verification & Test Suite Sign-off

- **Previous Test Cases**: 158 tests
- **New Test Cases Added**: 27 tests (across similarity, embedding provider, semantic index, retriever, and evaluation metrics)
- **Total Test Cases**: **185 / 185 passing (100%)**
- **Compiler Warnings**: **0 warnings** with `-Wall -Wextra -Wpedantic` in C++20.
- **Code Formatting**: 100% compliant with `.clang-format`.
- **Phase 1–5 Integrity**: Zero regressions to lexical search, BM25, CodeAware ranking, or graph relationships.

---

## 9. Final Verdict

**PHASE 6.0 COMPLETE — READY FOR PHASE 6.1**
