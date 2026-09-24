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
| **CLI** | C++20 | Native developer CLI (`index` scan, `parse` inspect commands) | **Phase 2 Active** |
| **Engine Core** | C++20 | Core search & indexing library (`amoeba_engine`) | **Phase 2 Active** |
| **Repository Scanner** | C++20 (`amoeba::scanner`) | Recursive filesystem traversal & file filtering | **Phase 1 Implemented** |
| **Source Parser** | C++20 / Tree-sitter (`amoeba::parser`) | Concrete syntax tree parsing & structural extraction | **Phase 2 Implemented** |
| **Index & Storage** | C++20 | Inverted index & symbol tables | *Planned (Phase 3)* |
| **Retrieval & Ranking** | C++20 | Exact, fuzzy, and semantic search pipelines | *Planned (Future)* |
| **Server / API** | Node.js / TypeScript | Lightweight HTTP API & integration layer | *Planned (Future)* |
| **Web UI** | React / TypeScript / Vite | Interactive search and visualization interface | *Planned (Future)* |

---

## Core Engine Architecture

```text
┌─────────────────────────────────────────────────────────────┐
│                        Engine Core                          │
├─────────────────────────────────────────────────────────────┤
│  • Repository Scanner [Phase 1] • Source Parser [Phase 2]   │
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
2. **Source Parser (`engine/parser`)** — **Implemented (Phase 2)**:
   * Integrates Tree-sitter C/C++ runtimes.
   * Parses source files into concrete syntax trees (CST).
   * Extracts structural elements (`Class`, `Struct`, `Function`, `Method`, `Include`, `Call`).
   * Captures 1-indexed line/column source ranges.
3. **Index & Storage (`engine/index`)** — *Planned (Phase 3)*: Inverted index, trigram structures, and symbol lookups.
4. **Retrieval & Ranking** — *Planned*: Exact substring, identifier lookup, semantic vector similarity, and composite relevance scoring.
5. **Symbol & Dependency Graph** — *Planned*: Cross-file references, call graphs, type hierarchies, and import dependencies.
6. **Context Builder** — *Planned*: Extracting relevant code snippets, surrounding scopes, and dependencies for intelligent code assistance.

---

## Integration Strategy & Technology Reuse

Our core engineering philosophy guides how external technologies are integrated:

> **Build the core, reuse mature infrastructure, measure bottlenecks, and replace components only when there is a good engineering reason.**

* **Parsing**: Leverage Tree-sitter for robust, fault-tolerant syntax parsing rather than writing hand-crafted parsers.
* **Vector Indexing / ANN**: Evaluate established ANN vector indexing libraries (such as FAISS or HNSW) when semantic search is introduced.
* **Relational Persistence**: Utilize embedded relational engines (such as SQLite) or external databases (PostgreSQL) when persistent relational metadata is needed.
* **Core Search Logic**: The core tokenization, inverted indexing, ranking algorithms, and symbol representations remain custom-built within the C++ engine to ensure deep understanding, predictability, and uncompromising performance.
