# Architectural Overview

## Long-term Vision

**Amoeba** is a high-performance source-code search and indexing engine designed to progressively evolve across distinct capability horizons:

$$\text{Source-Code Indexing} \longrightarrow \text{Lexical Retrieval} \longrightarrow \text{Code-Aware Ranking} \longrightarrow \text{Semantic Search} \longrightarrow \text{Code Intelligence}$$

---

## High-Level System Architecture

The architecture is structured across three primary tiers: Presentation, Application Server / API, and Core Engine.

```text
                    Amoeba
                       │
        ┌──────────────┼──────────────┐
        │              │              │
       Web            Server          CLI
    React/TS        Node/TS          C++
    [PLANNED]      [PLANNED]       [ACTIVE]
        │              │              │
        └──────────────┼──────────────┘
                       │
                 ┌─────▼─────┐
                 │   Engine  │
                 │    C++    │
                 │  [ACTIVE] │
                 └───────────┘
```

### Component Status Matrix

| Component | Technology | Role | Status |
| :--- | :--- | :--- | :--- |
| **CLI** | C++20 | Native developer CLI (`index`, `search`, `parse` commands) | **Active (Phase 4)** |
| **Engine Core** | C++20 | Core search, indexing & ranking library (`amoeba_engine`) | **Active (Phase 4)** |
| **Repository Scanner** | C++20 (`amoeba::scanner`) | Recursive filesystem traversal & file filtering | **Phase 1 Implemented** |
| **Source Parser** | C++20 / Tree-sitter (`amoeba::parser`) | Multi-language syntax parsing & AST symbol extraction | **Phase 2 Implemented** |
| **Inverted Index & Tokenizer** | C++20 (`amoeba::index`) | In-memory inverted index, subword tokenization | **Phase 3 Implemented** |
| **Ranking Pipeline** | C++20 (`amoeba::rank`) | Heuristic baseline, Okapi BM25 & Code-Aware Hybrid Ranker | **Phase 4 Implemented** |
| **Symbol & Dependency Graph** | C++20 (`amoeba::graph`) | Cross-file references, call graphs, type hierarchies | *Planned (Phase 5)* |
| **Candidate Fusion / Future Hybrid Ranker** | C++20 / ML | Hybrid lexical + semantic ranking fusion | *Planned (Future)* |
| **Server / API** | Node.js / TypeScript | Lightweight HTTP API & daemon integration layer | *Planned (Future)* |
| **Web UI** | React / TypeScript / Vite | Interactive search and code navigation interface | *Planned (Future)* |

---

## Core Engine Architecture

```text
┌─────────────────────────────────────────────────────────────┐
│                        Engine Core                          │
├─────────────────────────────────────────────────────────────┤
│  • Repository Scanner [Phase 1] • Source Parser [Phase 2]   │
│  • Inverted Index     [Phase 3] • Code Tokenizer [Phase 3]  │
│  • Candidate Search   [Phase 3] • Ranking Core   [Phase 4]  │
│  • Baseline Ranker    [Phase 4] • BM25 Ranker    [Phase 4]  │
│  • Code-Aware Ranker  [Phase 4] • Symbol Graph   [Phase 5]  │
└─────────────────────────────────────────────────────────────┘
```

### Module Breakdown & Status

1. **Repository Scanner (`engine/scanner`)** — **Implemented (Phase 1)**:
   * Validates repository paths and traverses directories recursively.
   * Filters out non-source files and excluded directories (`.git`, `node_modules`, `build`, etc.).
   * Produces structured `FileInfo` metadata (`path`, `extension`, `size`).
2. **Source Parser (`engine/parser`)** — **Implemented (Phase 2)**:
   * Integrates 11 Tree-sitter grammars (C, C++, Python, Java, Go, Rust, JS, TS, TSX, HTML, CSS).
   * Extracts structural elements (`Class`, `Struct`, `Function`, `Method`, `Include`, `Call`, `Route`).
   * Captures 1-indexed line/column source ranges and doc comments.
3. **Index & Tokenizer (`engine/index`)** — **Implemented (Phase 3)**:
   * Case-preserving and subword tokenization (CamelCase, snake_case, kebab-case, acronyms).
   * In-memory inverted index with element/file ID lookups and postings lists.
4. **Retrieval & Ranking (`engine/rank`)** — **Implemented (Phase 4)**:
   * Decoupled candidate retrieval and deterministic ranking pipeline.
   * Heuristic Baseline Ranker, Okapi BM25 Lexical Ranker, and Code-Aware Hybrid Ranker.
   * Empirical validation demonstrating $\text{P@1} = 0.868, \text{MRR} = 0.924, \text{NDCG@5} = 0.875$.
5. **Symbol & Dependency Graph (`engine/graph`)** — *Planned (Phase 5)*: Cross-file references, call graphs, type hierarchies, and import dependencies.
6. **Context Builder & Semantic Retrieval** — *Planned*: Context extraction and vector/embedding hybrid search.

---

## Integration Strategy & Engineering Principles

* **Build the core, reuse mature infrastructure, measure bottlenecks, and replace components only when there is a good engineering reason.**
* **Parsing**: Tree-sitter provides robust, incremental parsing across 11 grammars.
* **Vector Indexing / ANN**: Evaluate established libraries (e.g. FAISS, HNSW) when dense vector embeddings are introduced.
* **Extensibility**: Ranking and retrieval remain cleanly modularized to facilitate future neural, embedding, or candidate fusion rankers.
