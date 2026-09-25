# Phase 6.1 — Real Code Embeddings & Semantic Retrieval

## 1. Overview & Objectives

Phase 6.1 establishes Amoeba's first semantic retrieval baseline using pretrained dense vector embeddings.

By evaluating a pretrained embedding model alongside lexical and BM25 ranking methods, Amoeba addresses vocabulary mismatches between natural developer queries and syntactic code identifiers.

```text
                  [P6.1-D01: Amoeba Phase 6.1 — Real Semantic Retrieval Pipeline]

           CodeElement (or User Query String)
                          │
                          ▼
            SemanticTextFormatter (A/B)
                          │
                          ▼
                      Tokenizer
                          │
                          ▼
           Pretrained all-MiniLM-L6-v2 Transformer
                          │
                          ▼
             Mean Pooling (Attention Mask Aware)
                          │
                          ▼
                   L2 Normalization
                          │
                          ▼
              384-dimensional Embedding
                          │
                          ▼
             SemanticIndex (Vector Store)
                          │
                          ▼
            Cosine Similarity Linear Scan
                          │
                          ▼
             Top-K Semantic Search Results
```

---

## 2. Model Selection & Provenance

### Model Specification
* **Model Name**: `all-MiniLM-L6-v2`
* **Model Source**: `sentence-transformers/all-MiniLM-L6-v2` (Git revision: `fa97f96`)
* **Embedding Dimension**: 384 (float32, 1,536 bytes per vector)
* **Model Artifact Size**: ~80 MB (dense weights) / ~23 MB (quantized ONNX)
* **License**: Apache 2.0
* **Target Hardware**: Standard x86_64 / ARM64 CPU

### Architectural Characterization
`all-MiniLM-L6-v2` is a general-purpose sentence/text embedding model evaluated as Amoeba's first real semantic retrieval baseline. It is not specialized for abstract syntax trees or programming languages, but its dense subword representations provide an effective baseline for bridging natural language queries to structured identifier contexts.

### Pipeline Verification
The embedding generation pipeline strictly adheres to the canonical sentence transformer architecture:
1. **Tokenization**: Input text parsed into subword token IDs with special `[CLS]` and `[SEP]` tokens.
2. **Transformer Inference**: 6-layer MiniLM transformer forward pass generating token hidden states ($D = 384$).
3. **Pooling**: Attention-mask-aware mean pooling over valid token embeddings.
4. **Normalization**: Unit L2 vector normalization such that $\sum_{i=1}^{384} v_i^2 = 1.0$, allowing cosine similarity to be computed as a direct dot product.

---

## 3. Provider Abstraction & Decoupling

The core engine interfaces with embeddings strictly through the [`EmbeddingProvider`](file:///d:/amoeba/engine/include/amoeba/semantic/embedding_provider.hpp) interface:

```text
                           EmbeddingProvider
                                  │
                 ┌────────────────┴────────────────┐
                 ▼                                 ▼
   DeterministicEmbeddingProvider    PretrainedEmbeddingProvider
       (Fast CI / Unit Tests)        (all-MiniLM-L6-v2 384-d Model)
```

- **Deterministic Provider Preserved**: [`DeterministicEmbeddingProvider`](file:///d:/amoeba/engine/include/amoeba/semantic/deterministic_embedding_provider.hpp) remains the default for unit tests, architecture tests, and CI builds where downloading external model weights is prohibited.
- **Model Decoupling**: No runtime-specific ML dependencies (e.g. PyTorch or ONNX internals) leak into Amoeba's public C++ API headers.

---

## 4. Semantic Text Representation

[`SemanticTextFormatter`](file:///d:/amoeba/engine/include/amoeba/semantic/semantic_document.hpp) standardizes `CodeElement` metadata into structured text for embedding generation:

* **Representation A (Metadata Only)**:
  ```text
  Language: C++
  File: src/auth/auth_service.cpp
  Kind: Method
  Context: AuthService
  Name: validateCredentials
  Detail: validates credentials and creates session token
  ```
* **Representation B (Metadata + Code Snippet)**:
  ```text
  Language: C++
  File: src/auth/auth_service.cpp
  Kind: Method
  Context: AuthService
  Name: validateCredentials
  Detail: validates credentials and creates session token
  Code: bool validateCredentials(const Credentials& creds) { return verify(creds); }
  ```

---

## 5. Benchmark Evaluation & Results

### Validation Suite Scope & Structure
The evaluation suite contains 7 corpus elements across 5 languages (C++, TypeScript, Python, Go, TSX) and 10 queries across 10 query categories (1 query per category):

### Comparative Quality Metrics

| Method | P@1 | P@3 | P@5 | Recall@5 | Recall@10 | MRR | NDCG@5 | NDCG@10 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Baseline Lexical** | 0.90 | 0.43 | 0.28 | 1.00 | 1.00 | 0.933 | 0.957 | 0.957 |
| **BM25** | 0.90 | 0.47 | 0.28 | 1.00 | 1.00 | 0.950 | 0.969 | 0.969 |
| **CodeAware** | 0.90 | 0.43 | 0.28 | 1.00 | 1.00 | 0.933 | 0.957 | 0.957 |
| **Semantic (`all-MiniLM-L6-v2`)** | **1.00** | **0.43** | **0.28** | **1.00** | **1.00** | **1.000** | **0.997** | **0.997** |

### Per-Category Breakdown (1 Query per Category)

| Category | Example Query | Lexical (CodeAware) | Semantic (`all-MiniLM-L6-v2`) | Category Observation |
| :--- | :--- | :--- | :--- | :--- |
| **Exact Identifier** | `"authenticateUser"` | **P@1: 1.0** (Rank 1) | **P@1: 1.0** (Rank 1) | Both succeed on exact identifier names. |
| **Normalized Identifier** | `"find user by id"` | **P@1: 1.0** (Rank 1) | **P@1: 1.0** (Rank 1) | Subword tokenization and normalization align well. |
| **Partial / Subword** | `"validate creds"` | **P@1: 1.0** (Rank 1) | **P@1: 1.0** (Rank 1) | Both methods resolve common abbreviations. |
| **Multi-Term** | `"process payment transactions and charge invoices"` | **P@1: 1.0** (Rank 1) | **P@1: 1.0** (Rank 1) | Strong term overlap ensures high ranking. |
| **Contextual Lexical** | `"auth service session token"` | **P@1: 1.0** (Rank 1) | **P@1: 1.0** (Rank 1) | Combined symbol/file context matches. |
| **Path / Repository** | `"services/token_service.py"` | **P@1: 1.0** (Rank 1) | **P@1: 1.0** (Rank 1) | Lexical path index matches directly. |
| **Framework** | `"export const UserProfileCard"` | **P@1: 1.0** (Rank 1) | **P@1: 1.0** (Rank 1) | Exact keyword structure matches. |
| **Ambiguous** | `"user"` | **P@1: 1.0** (Rank 1) | **P@1: 1.0** (Rank 1) | Multiple relevant entities returned. |
| **Cross-Language** | `"hash password with salt"` | **P@1: 1.0** (Rank 1) | **P@1: 1.0** (Rank 1) | Term matching identifies Python crypto utility. |
| **Conceptual / Semantic** | `"where do we check user login credentials?"` | P@1: 0.0 (Rank 3) | **P@1: 1.0** (Rank 1) | Semantic bridges `"check login credentials"` $\to$ `validateCredentials`. |

### Scope Interpretation
On this small 10-query validation set, semantic retrieval correctly handled the single conceptual query that lexical retrieval missed while matching lexical methods on the remaining categories. This is an encouraging validation result, but the corpus and query set are too small to establish general semantic-retrieval superiority.

---

## 6. Performance Benchmarks (Measured on CPU)

*Hardware Environment: AMD/Intel x86_64, Windows 11, MinGW Clang++ C++20 / Python 3.12.*

| Benchmark Measurement | Measured Value (Median) | Methodology |
| :--- | :--- | :--- |
| **Single Element Embedding** | $11.57\text{ ms}$ | Median over 50 iterations on CPU |
| **Batch Embedding (50 items)** | $40.20\text{ ms}$ | Batched forward pass (~0.80ms / item) |
| **Query Embedding** | $9.68\text{ ms}$ | Single string inference (median over 50 runs) |
| **Index Retrieval (7 elements)** | $< 0.01\text{ ms}$ | Cosine similarity dot-product scan (median over 1,000 runs) |
| **Index Retrieval (500 elements)** | $0.013\text{ ms}$ | Vectorized CPU scan (median over 100 runs) |
| **Index Retrieval (50,000 elements)** | $6.38\text{ ms}$ | Vectorized CPU scan (median over 100 runs) |
| **Vector Memory Footprint** | $1,584\text{ bytes}$ / vector | 1,536 bytes (384 float32) + 48 bytes metadata |

> **Note on Microbenchmark Variance**: Very small retrieval timings (<0.1 ms on small corpora) are susceptible to measurement noise, timer resolution, CPU scheduling, and cache effects. Timings are reported using the median across repeated trials.

---

## 7. Research Artifacts & Generated Graphs

All benchmark artifacts are organized under `benchmarks/phase-06.1/`:

* **Data Files**:
  - [`benchmarks/phase-06.1/data/phase-06.1-results.json`](file:///d:/amoeba/benchmarks/phase-06.1/data/phase-06.1-results.json)
  - [`benchmarks/phase-06.1/data/phase-06.1-results.csv`](file:///d:/amoeba/benchmarks/phase-06.1/data/phase-06.1-results.csv)
* **Plot Manifest**:
  - [`benchmarks/phase-06.1/manifests/phase-06.1-plot-manifest.json`](file:///d:/amoeba/benchmarks/phase-06.1/manifests/phase-06.1-plot-manifest.json)
* **Research Plots**:
  - **P6.1-G01**: `P6.1-G01-p01-method-comparison.png` — Precision@1 on Phase 6.1 Validation Set.
  - **P6.1-G02**: `P6.1-G02-ranking-quality-comparison.png` — MRR, NDCG@5, NDCG@10 comparison on validation set.
  - **P6.1-G03**: `P6.1-G03-category-quality.png` — Retrieval Quality by Category (1 Query per Category, N=10 total).
  - **P6.1-G04**: `P6.1-G04-retrieval-latency.png` — Index retrieval latency comparison across methods.
  - **P6.1-G05**: `P6.1-G05-embedding-latency.png` — Pretrained model inference latency (single vs. query vs. batch).
  - **P6.1-G06**: `P6.1-G06-vector-memory-scaling.png` — Vector store memory scaling up to 50,000 vectors.
  - **P6.1-G07**: `P6.1-G07-corpus-size-vs-retrieval-latency.png` — Measured brute-force retrieval latency vs. corpus size.

---

## 8. Limitations & Scope Boundaries

1. **Validation Corpus Size**: The validation set (7 elements, 10 queries, 1 query per category) serves as an architectural sanity check; broad retrieval claims require larger benchmarks.
2. **General-Purpose Model**: `all-MiniLM-L6-v2` is trained on natural language sentence pairs; while effective on identifiers and descriptions, specialized code models (or domain fine-tuning) remain an area for future research.
3. **Brute-Force Retrieval Scaling**: Brute-force retrieval remained below 6.4 ms on the measured 50,000-vector benchmark in this environment. ANN/HNSW evaluation is deferred until larger corpora or measured latency requirements justify it.
4. **No Hybrid Fusion Yet**: Merging BM25 lexical posting scores with dense semantic cosine scores is the primary objective of Phase 6.2 (Hybrid Retrieval & Fusion).
5. **CPU Environment**: Measurements are reported for CPU execution without requiring GPU/CUDA drivers.

---

## 9. Verification & Test Suite Sign-off

- **Total Test Cases**: **192 / 192 passing (100%)**
- **Compiler Warnings**: **0 warnings** with `-Wall -Wextra -Wpedantic` in C++20.
- **Code Style**: 100% compliant with `.clang-format`.
- **Reproducibility**: Benchmark data and plots reproducible via `python benchmarks/scripts/generate_phase_06_1_data.py` and `python benchmarks/scripts/plot_phase_06_1.py`.

---

## 10. Phase Verdict

**PHASE 6.1A COMPLETE — PHASE 6.1 VALIDATED**
