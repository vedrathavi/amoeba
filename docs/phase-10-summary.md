# Amoeba — Phase 10: Executive Summary & Technical Direction

**Date**: November 2026  
**Auditor**: Senior Software Architect & Technical Reviewer  
**Status**: Completed & Submitted for Review  
**Deliverable**: Architectural Synthesis for Technical Leadership

---

## 1. Executive Summary

Amoeba possesses a high-performance, well-engineered C++20 core featuring Tree-sitter multi-language AST extraction, custom BM25 lexical search, in-memory HNSW vector retrieval, and microsecond graph traversal. 

However, **critical configuration drift, missing persistence, and fragile heuristics currently cripple the real-world user experience**:
1. **Zero Index Persistence**: The CLI rebuilds the AST, inverted index, HNSW graph, and call graph from scratch on every invocation.
2. **CLI Retrieval Drift**: The CLI overrides Phase 8.4's calibrated parameters ($\alpha = 0.8$, $k = 50$) with uncalibrated settings ($\alpha = 0.5$, `adaptive_fusion = true`, $k = 5$), causing conversational questions to drop lexical precision and starve candidate pools.
3. **Ingestion Bottlenecks**: The scanner ignores `.gitignore` and lacks modern exclusions, freezing on Python `.venv` and `site-packages` directories.
4. **ContextBuilder Information Loss**: Code excerpts are hard-truncated at line 50, and overflow candidates are discarded wholesale.
5. **Architectural Coupling**: The core engine lacks a unified facade; all subsystem orchestration is hardcoded inside `apps/cli/main.cpp`.

Fixing these issues in Phase 11 does not require complex distributed systems or external database servers. It requires **aligning CLI parameters with existing engine calibration, embedding SQLite for incremental state persistence, and exposing a clean `AmoebaEngine` facade**.

---

## 2. Codebase Reality: What We Actually Have

* **C++ Core Foundation**: Native C++20 static library (`amoeba_engine`) compiling 58 translation units with zero third-party database daemons.
* **Multi-Language AST Parsing**: 11 Tree-sitter grammars (C, C++, Python, Java, Go, Rust, JS, TS, TSX, HTML, CSS) parsing files in $< 2\text{ ms}$ per file.
* **Lexical BM25 Engine**: Custom, zero-dependency inverted index with subword tokenization and code-aware structural weighting.
* **Vector Index**: Integrated `hnswlib` for approximate nearest-neighbor search with custom `"AMOH"` binary serialization.
* **Relationship Graph**: In-memory directed graph with dual adjacency maps resolving 1-hop and 2-hop edges in $< 1\ \mu\text{s}$.
* **Reasoning Runtime**: Native Windows HTTP client streaming tokens from local Ollama (`qwen2.5-coder:1.5b`).
* **Tool Abstraction**: Complete `ToolRegistry` with 4 verified tools (`SearchCode`, `FindSymbol`, `ReadFile`, `GetRelationships`), currently unused by the CLI.
* **Test Suite**: **542 GoogleTests** in `amoeba_engine_tests.exe` covering units and integration paths.
* **Benchmark Suite**: RepoProbe v1 containing 70 ground-truth questions across 10 cloned production repositories.

---

## 3. What Works Exceptionally Well

1. **Tree-sitter Parsing Speed & Precision**: Parses multi-language codebases cleanly, extracting cohesive `RetrievalUnit` blocks along AST boundaries.
2. **In-Memory Graph Traversal**: Resolving callers, callees, and imports directly in RAM is virtually instantaneous ($< 1\ \mu\text{s}$), outperforming external graph databases by $1,000\times$.
3. **Calibrated Hybrid Fusion Math**: The underlying engine algorithms for Min-Max normalization and weighted score fusion ($\alpha = 0.8$) provide top-tier precision and recall when invoked properly.
4. **100% Offline Capability**: Scanning, parsing, BM25 indexing, vector search, graph expansion, and local Ollama reasoning operate entirely offline without network access.

---

## 4. What Does NOT Work / Broken in Practice

1. **Cold In-Memory Execution on Every Run**:
   `amoeba ask` re-scans the disk, re-parses all files, re-computes embeddings, and rebuilds the HNSW graph on every run. No incremental state exists.
2. **Scanner Freezes on Python Virtual Environments**:
   `EXCLUDED_DIRECTORIES` only contains 8 static folders. It ignores `.gitignore` and crawls `.venv/site-packages`, parsing tens of thousands of PyTorch/NumPy files.
3. **CLI Inversion of Fusion Weights**:
   `apps/cli/main.cpp` sets `.alpha = 0.5` and `.adaptive_fusion = true`. Questions with interrogatives ("Where is...") trigger `recommended_alpha = 0.25`, suppressing exact code identifiers.
4. **Candidate Reservoir Starvation**:
   CLI clamps candidate depth to `max_results = 5`, dropping candidates 6–15 that would have been rescued by graph investigation.
5. **ContextBuilder Line-50 Hard Truncation**:
   Functions longer than 50 lines are cut off, hiding return values, error handlers, and callees from the LLM prompt.
6. **Benchmark Overfitting in Stopwords**:
   `kConversationalNoise` literally hardcodes the 10 RepoProbe repository names (`"pkl"`, `"dokploy"`, `"beszel"`, etc.), compromising generalization.
7. **Citation Validation is Aspirational**:
   Citations are copied directly from the context package without parsing or validating the LLM's generated response.
8. **Platform Boundary**:
   `LocalLLMRuntime` depends on Windows WinHTTP API and fails on Linux/macOS.

---

## 5. Historical Issues: Resolved vs. Remaining

| Historical Issue | Verification Status | Current Code Reality |
|---|---|---|
| **AST Parser Stability** | `Resolved` | Stable multi-language extraction across 11 grammars. |
| **Hybrid Fusion Calibration** | `Resolved in Engine, Broken in CLI` | $\alpha=0.8$ calibrated in engine headers, but overridden in `apps/cli/main.cpp`. |
| **Bounded BFS Traversal** | `Resolved` | Depth and candidate bounds are strictly enforced in RAM. |
| **Tool Framework** | `Resolved in Engine, Unused in CLI` | Tools exist and pass 60+ unit tests, but are disconnected from runtime. |
| **Scanner Exclusions & `.gitignore`** | `Remaining (Unresolved)` | 8 static folders; `.gitignore` ignored; `.venv` traversed. |
| **Index Persistence** | `Remaining (Unresolved)` | Rebuilt from scratch on every run; no `.amoeba/` directory. |
| **Interrogative Inversion Trap** | `Remaining (Unresolved)` | Symbol queries with "Where" or "?" misrouted to $\alpha=0.25$. |
| **ContextBuilder Line-50 Cut** | `Remaining (Unresolved)` | Hard 50-line cut destroys multi-branch function logic. |
| **Citation Hallucination Check** | `Remaining (Unresolved)` | LLM output is not parsed; citations copied from context. |

---

## 6. Biggest Architectural Risks

1. **Latency & Friction Kills Adoption**:
   If a tool takes 15–30 seconds to answer simple questions on medium codebases because it re-indexes every time, developers will abandon it.
2. **Context Loss Induces Hallucinations**:
   When `ContextBuilder` cuts off code at line 50 or drops overflow items, small LLMs (`qwen2.5-coder:1.5b`) are forced to guess, causing fabricated explanations.
3. **Interface Coupling**:
   Without a unified `AmoebaEngine` facade, creating a Web application or VS Code extension requires duplicating 100+ lines of fragile pipeline orchestration.

---

## 7. Decisions That Are Sufficiently Supported

1. **Maintain Custom C++ In-Memory Graph**:
   Replacing Amoeba's microsecond adjacency list with Neo4j or Memgraph would add hundreds of megabytes of RAM, network latency, and installation friction for zero benefit.
2. **Adopt Embedded SQLite 3 for Persistence**:
   Single-file `.amoeba/index.db`, zero-config, ACID transactions, WAL mode, and instant C++ integration.
3. **Calibrated Hybrid Fusion ($\alpha = 0.8$, $\beta = 0.2$)**:
   Supported by rigorous 16-strategy sweep across 70 tasks, preserving 86.7% of lexical wins while retaining depth recall.
4. **Anchor-Centered Context Windows**:
   Centering source windows on focal symbol lines preserves target logic without expanding token budgets.

---

## 8. Decisions That Require Small Empirical Experiments

1. **`sqlite-vec` vs. `hnswlib` File Export**:
   - *Question*: Can `sqlite-vec` provide sufficient vector search performance ($< 10\text{ ms}$ for $k=50$) to unify all vectors and metadata into a single `.amoeba/index.db` file?
   - *Experiment*: Run a 5,000-vector benchmark comparing query latency and recall of `sqlite-vec` vs `hnswlib`.
2. **Local ONNX vs. Ollama Embeddings**:
   - *Question*: Does a real 384-d dense embedding model (e.g. `bge-code-small`) meaningfully improve recall over the heuristic hash feature map on conceptual holdout tasks?
   - *Experiment*: Measure Hit@10 on 20 conceptual questions using ONNX embeddings vs current hash embeddings.

---

## 9. Concrete Scope for Phase 11

### Phase 11 Priorities (Immediate & Focused):
1. **P0 Fixes**:
   - Align CLI retrieval options with engine calibration ($\alpha = 0.8$, `adaptive_fusion = false`, $k = 50$).
   - Expand scanner default exclusions (`.venv`, `vendor`, `.next`, etc.) and add `--exclude` flag.
   - Invert intent precedence in `QueryUnderstanding` (code syntax takes priority over interrogatives).
   - Purge benchmark repository names from `kConversationalNoise`.
2. **P1 Foundation**:
   - Embed `sqlite3.c` into CMake; implement `SqliteMetadataStore` in `.amoeba/index.db`.
   - Implement mtime-based incremental change detection (instant query on unchanged repos).
   - Connect `HnswSemanticIndex` save/load to `.amoeba/vectors.bin`.
   - Implement Anchor-Centered Slicing in `ContextBuilder`.
   - Refactor engine orchestration into a clean `AmoebaEngine` facade.

---

## 10. What Should Explicitly NOT Be Built Yet

* **DO NOT** build a Web server, GUI, or dashboard (defer to Phase 13).
* **DO NOT** introduce multi-agent orchestration or autonomous ReAct loops into the core retrieval path.
* **DO NOT** install or depend on external database servers (PostgreSQL, MongoDB, Qdrant, Neo4j).
* **DO NOT** replace Tree-sitter with heavy compiler tooling (Clang LibTooling).
* **DO NOT** implement multi-turn conversational chat sessions until single-turn Q&A latency and accuracy are rock solid.

