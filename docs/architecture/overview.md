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
    [PLANNED]      [PLANNED]       [PHASE 0]
        │              │              │
        └──────────────┼──────────────┘
                       │
                 ┌─────▼─────┐
                 │   Engine  │
                 │    C++    │
                 │ [PHASE 0] │
                 └───────────┘
```

### Component Status Matrix

| Component | Technology | Role | Status |
| :--- | :--- | :--- | :--- |
| **CLI** | C++20 | Native developer command-line interface | **Phase 0 Baseline** |
| **Engine Core** | C++20 | Reusable high-performance search & indexing library | **Phase 0 Baseline** |
| **Server / API** | Node.js / TypeScript | Lightweight HTTP API & integration layer | *Planned (Future Phase)* |
| **Web UI** | React / TypeScript / Vite | Interactive search and visualization interface | *Planned (Future Phase)* |

> [!NOTE]
> All server, web frontend, and external integration layers are **planned future milestones** and are deliberately NOT implemented in Phase 0.

---

## Core Engine Architecture (Conceptual)

The C++ core engine will eventually encapsulate the following modular concepts:

```text
┌─────────────────────────────────────────────────────────────┐
│                        Engine Core                          │
├─────────────────────────────────────────────────────────────┤
│  • Repository Management      • Parser Integration          │
│  • File Scanner               • Index Management            │
│  • Code Representation        • Retrieval Engine            │
│  • Symbol / Dependency Graph  • Ranking Pipeline            │
│  • Context Builder            • Storage / Query Interfaces  │
└─────────────────────────────────────────────────────────────┘
```

### Conceptual Module Breakdown (Planned)

1. **Repository & File Scanner**: Efficient filesystem traversal, `.gitignore` filtering, file metadata extraction, and change detection.
2. **Parser Integration**: Structural syntax parsing (e.g., via Tree-sitter) into concrete syntax trees and token streams.
3. **Code Representation**: Abstract representation of source code structures, tokens, symbols, scopes, and AST nodes.
4. **Index & Storage**: Inverted indexes, trigram indexes, symbol tables, and vector embeddings storage.
5. **Retrieval & Ranking**: Exact substring, regex, identifier lookup, semantic vector similarity, and composite relevance scoring.
6. **Symbol & Dependency Graph**: Cross-file references, call graphs, type hierarchies, and import dependencies.
7. **Context Builder**: Extracting relevant code snippets, surrounding scopes, and dependencies for intelligent code assistance.

---

## Integration Strategy & Technology Reuse

Our core engineering philosophy guides how external technologies are integrated:

> **Build the core, reuse mature infrastructure, measure bottlenecks, and replace components only when there is a good engineering reason.**

* **Parsing**: Leverage mature parsers (such as Tree-sitter) rather than building hand-crafted grammar parsers from scratch.
* **Vector Indexing / ANN**: Evaluate established ANN vector indexing libraries (such as FAISS or HNSW) when semantic search is introduced.
* **Relational Persistence**: Utilize embedded relational engines (such as SQLite) or external databases (PostgreSQL) when persistent relational metadata is needed.
* **Embedding Models & LLMs**: Integrate mature local/remote embedding and language model APIs rather than training baseline models from scratch.
* **Core Search Logic**: The core tokenization, inverted indexing, ranking algorithms, and symbol representations remain custom-built within the C++ engine to ensure deep understanding, predictability, and uncompromising performance.
