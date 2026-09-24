# Architectural Overview

## Long-term Vision

**Amoeba** is a source-code search and indexing engine designed to progressively evolve across distinct capability horizons:

$$\text{Source-Code Indexing} \longrightarrow \text{Retrieval} \longrightarrow \text{Code Understanding} \longrightarrow \text{Semantic Search} \longrightarrow \text{Code Intelligence}$$

---

## High-Level System Architecture

The target architecture is structured across three primary tiers: Presentation, Application Server / API, and Core Engine.

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
| **CLI** | C++20 | Native developer command-line interface (`index` scan command) | **Phase 1 Active** |
| **Engine Core** | C++20 | Search & indexing engine library (`amoeba_engine`) | **Phase 1 Active** |
| **Repository Scanner** | C++20 (`amoeba::scanner`) | Recursive filesystem scanner & file filter | **Phase 1 Implemented** |
| **Tokenizer / Parser** | C++20 / Tree-sitter | Syntax parsing and token extraction | *Planned (Phase 2)* |
| **Index & Storage** | C++20 | Inverted index & symbol tables | *Planned (Future)* |
| **Retrieval & Ranking** | C++20 | Exact, fuzzy, and semantic search pipelines | *Planned (Future)* |
| **Server / API** | Node.js / TypeScript | Lightweight HTTP API & integration layer | *Planned (Future)* |
| **Web UI** | React / TypeScript / Vite | Interactive search and visualization interface | *Planned (Future)* |

---

## Core Engine Architecture

```text
┌─────────────────────────────────────────────────────────────┐
│                        Engine Core                          │
├─────────────────────────────────────────────────────────────┤
│  • Repository Scanner [Phase 1] • Parser Integration        │
│  • Code Representation          • Index Management          │
│  • Symbol / Dependency Graph    • Retrieval Engine          │
│  • Context Builder              • Ranking Pipeline          │
└─────────────────────────────────────────────────────────────┘
```

### Module Breakdown & Status

1. **Repository Scanner (`engine/scanner`)** — **Implemented (Phase 1)**:
   * Validates repository paths.
   * Traverses directories recursively using `std::filesystem`.
   * Filters out non-source files and excluded directories (`.git`, `node_modules`, `build`, etc.).
   * Produces structured `FileInfo` metadata (`path`, `extension`, `size`).
2. **Tokenizer & Parser Integration (`engine/parser`)** — *Planned (Phase 2)*: Structural syntax parsing into concrete syntax trees and token streams.
3. **Code Representation** — *Planned*: Abstract representation of source code structures, tokens, symbols, scopes, and AST nodes.
4. **Index & Storage** — *Planned*: Inverted indexes, trigram indexes, symbol tables, and vector embeddings storage.
5. **Retrieval & Ranking** — *Planned*: Exact substring, regex, identifier lookup, semantic vector similarity, and composite relevance scoring.
6. **Symbol & Dependency Graph** — *Planned*: Cross-file references, call graphs, type hierarchies, and import dependencies.
7. **Context Builder** — *Planned*: Extracting relevant code snippets, surrounding scopes, and dependencies for intelligent code assistance.

---

## Integration Strategy & Technology Reuse

Our core engineering philosophy guides how external technologies are integrated:

> **Build the core, reuse mature infrastructure, measure bottlenecks, and replace components only when there is a good engineering reason.**

* **Parsing**: Leverage mature parsers (such as Tree-sitter) rather than building hand-crafted grammar parsers from scratch.
* **Vector Indexing / ANN**: Evaluate established ANN vector indexing libraries (such as FAISS or HNSW) when semantic search is introduced.
* **Relational Persistence**: Utilize embedded relational engines (such as SQLite) or external databases (PostgreSQL) when persistent relational metadata is needed.
* **Core Search Logic**: The core tokenization, inverted indexing, ranking algorithms, and symbol representations remain custom-built within the C++ engine to ensure deep understanding, predictability, and uncompromising performance.
