# Amoeba — Phase 10: Deep Repository Audit & Forensic Investigation Report

**Date**: November 2026  
**Auditor**: Senior Software Architect & C++ Systems Engineer  
**Status**: Completed  
**Repository Branch**: `master` (Ahead of `origin/master` by 4 commits)  
**Scope**: End-to-end inspection of codebase structure, CMake configuration, C++20 engine subsystems, retrieval pipelines, AST extractors, graph engine, reasoning service, CLI runtime, and benchmark suites.

---

## 1. Executive Overview & Methodology

This audit provides an evidence-based, forensic evaluation of the **Amoeba** codebase. The objective is to verify actual implementations against historical architecture documentation, evaluate technical claims, uncover hidden bottlenecks, and separate verified facts from architectural assumptions.

### Epistemic Classification Framework
To maintain technical integrity throughout this audit, all statements are categorized using four explicit epistemic levels:
* **[Fact]**: Directly verified from source code, compiler configurations, or execution traces.
* **[Observation]**: Empirical behavior observed during runtime inspection, static analysis, or test execution.
* **[Inference]**: Logical deductions regarding failure modes, scaling bottlenecks, or unintended side effects.
* **[Recommendation]**: Actionable architectural changes, refactorings, or design choices for subsequent phases.

### Verification Status Codes
Every finding in this document is tagged with one of the following verification statuses:
1. `Confirmed in current source`: Verified by line-level inspection of the current working tree.
2. `Reproduced during investigation`: Verified via test execution or runtime CLI invocation.
3. `Historical issue, now resolved`: Documented in earlier phases and confirmed fixed in the current tree.
4. `Partially resolved`: Partial fix exists, but edge cases or operational gaps remain.
5. `Not reproducible`: Stated in historical documents but unable to trigger or substantiate.
6. `Cannot verify due to missing environment`: Requires unavailable external services or hardware.
7. `Future architectural consideration`: Long-term design concern for upcoming phases.

---

## 2. Repository Structure & Configuration Audit

### 2.1 Workspace Layout
The repository is structured as a native C++20 project with companion Python benchmark utilities:

```text
d:\amoeba\
├── CMakeLists.txt                # Root CMake 3.20 configuration
├── CMakePresets.json             # Build presets (Ninja, MSVC, Clang)
├── compile_commands.json         # Clangd / LSP compilation database
├── vcpkg.json                    # Dependency manifest (GTest, minimal)
├── apps\
│   └── cli\main.cpp              # Amoeba CLI binary entrypoint (amoeba_cli)
├── engine\
│   ├── include\amoeba\           # Public API headers (20 module subdirectories)
│   ├── src\                      # Internal implementation (58 translation units)
│   └── tests\                    # GoogleTest suite (542 individual tests)
├── benchmarks\
│   ├── repoprobe_v1\             # 70-question benchmark across 10 cloned repos
│   │   ├── repos\                # 10 production repositories (ducklake, pkl, etc.)
│   │   ├── ground_truth.json     # Ground truth labels
│   │   ├── questions.json        # Question prompts & categories
│   │   └── scripts\              # Python evaluation harnesses
│   └── phase-06.1 - phase-06.5\  # Historical benchmark snapshots
└── docs\
    ├── architecture\             # High-level architecture specs
    ├── decisions\                # Architecture Decision Records (ADR-001)
    ├── phases\                   # Historical phase milestones (Phase 00 to 08.1.4)
    └── known-issues.md           # Tracked runtime and design defects
```

### 2.2 CMake & Build System Audit
* **[Fact] Standard Compliance**: `CMakeLists.txt` strictly enforces C++20 (`set(CMAKE_CXX_STANDARD 20)`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`).
* **[Fact] Dependency Management**:
  * Tree-sitter C runtime (`v0.22.6`) and 11 language grammars (C++, C, Python, Java, Go, Rust, JavaScript, TypeScript, TSX, HTML, CSS) are fetched via CMake `FetchContent`.
  * HNSW vector library (`hnswlib v0.8.0`) is fetched via `FetchContent`.
  * GoogleTest (`v1.14.0`) is fetched via `FetchContent` or detected via `vcpkg`.
  * Windows HTTP networking links against system library `winhttp.lib`.
* **[Observation] Compiler Warning Configuration**: MSVC builds use `/W4 /permissive-`; GCC/Clang use `-Wall -Wextra -Wpedantic`. Large stack sizes (`/STACK:67108864` or `-Wl,--stack,67108864`) are explicitly linked on test and benchmark executables to prevent stack overflow during deep AST/graph traversals.
* **[Observation] Monolithic Engine Library**: `amoeba_engine` compiles 58 `.cpp` files into a single static library. There are no sub-libraries dividing scanner, parser, index, retrieval, and reasoning.
* **[Inference] Build Latency Impact**: Changing any low-level header (e.g. `code_element.hpp`) triggers a recompile of almost all 58 translation units and 13 benchmark/test executables.
* **[Recommendation]**: Maintain modular sub-libraries or CMake object libraries (`amoeba_scanner`, `amoeba_parser`, `amoeba_index`, `amoeba_graph`, `amoeba_retrieval`, `amoeba_reasoning`) to accelerate developer iteration.

---

## 3. Subsystem-by-Subsystem Audit

### 3.1 Repository Scanner (`engine/src/scanner/repository_scanner.cpp`)
* **Status**: `Confirmed in current source` (Severe Ingestion Gap)
* **Code Location**: [`engine/src/scanner/repository_scanner.cpp:L17-L19, L95-L130`](file:///d:/amoeba/engine/src/scanner/repository_scanner.cpp#L17-L19)
* **Verified Behavior**:
  1. Directory exclusions are hardcoded to a static array of only 8 strings:
     ```cpp
     constexpr array<string_view, 8> EXCLUDED_DIRECTORIES = {
         ".git", "node_modules", "build", "dist", "out", "target", "coverage", "benchmarks",
     };
     ```
  2. `.gitignore` files (root or nested) are **completely ignored**. User-configured Git ignore rules do not prevent directories like `data/`, `checkpoints/`, or `logs/` from being traversed.
  3. Python virtual environments (`.venv`, `venv`, `env`, `site-packages`, `__pycache__`), package managers (`vendor`), frontend bundler caches (`.next`, `.nuxt`, `.turbo`), and IDE configs (`.idea`, `.vscode`) are **not excluded**.
  4. There is no file size cap. Minified bundles or 100MB data dumps ending in `.js` or `.py` are read into memory and parsed with Tree-sitter.
  5. There is no binary file check.
  6. The scan operation is completely synchronous and lacks any progress or cancellation token.
* **User & Technical Impact**: Pointing Amoeba at an existing Python repository containing `.venv/lib/site-packages` causes the engine to crawl tens of thousands of third-party library files (PyTorch, SciPy, NumPy), consuming gigabytes of RAM, saturating the CPU, and producing perceived hangs.
* **Recommendation**: Implement `.gitignore` pattern parsing, expand default exclusions to modern toolchains, enforce a 2MB file size cap, and provide a `--exclude` CLI flag.

---

### 3.2 Tree-sitter Parsing & Code Element Model (`engine/src/parser/`)
* **Status**: `Confirmed in current source` (Structural Scope Limitation)
* **Code Location**: [`engine/include/amoeba/parser/code_element.hpp:L14-L32`](file:///d:/amoeba/engine/include/amoeba/parser/code_element.hpp#L14-L32), [`engine/src/parser/source_parser.cpp:L31-L44`](file:///d:/amoeba/engine/src/parser/source_parser.cpp#L31-L44)
* **Verified Behavior**:
  1. The AST model extracts 17 enumeration kinds: `Class`, `Struct`, `Interface`, `Function`, `Method`, `Include`, `Call`, `JSXElement`, `JSXComponent`, `Selector`, `Property`, `Attribute`, `UtilityClass`, `Component`, `Hook`, `Route`, `Unknown`.
  2. **Excluded Constructs**: Local variables (`int x = 5;`), parameters, fields/members (partially captured under Property), control-flow branches (`if`, `else`, `switch`, `while`, `for`), return statements, exception blocks (`try/catch/throw`), and branch condition expressions are **not modeled**.
  3. Function bodies are not sliced or retained in `CodeElement`; only start/end line and column coordinates are stored.
* **User & Technical Impact**: Amoeba can answer structural questions (*"Where is X defined?"*, *"What functions does X call?"*), but cannot answer behavioral questions (*"Why does function X return an early error?"*, *"Under what conditions is flag Y set to false?"*).
* **Recommendation**: Do not index every AST token into the global graph (which causes severe bloat). Instead, introduce **Function Body Slicing** on demand for top-retrieved candidates so control-flow and error paths are exposed to the reasoning context.

---

### 3.3 Lexical Inverted Index & Tokenizer (`engine/src/index/`)
* **Status**: `Confirmed in current source` (High Quality, Persistence Missing)
* **Code Location**: [`engine/src/index/inverted_index.cpp`](file:///d:/amoeba/engine/src/index/inverted_index.cpp), [`code_tokenizer.cpp`](file:///d:/amoeba/engine/src/index/code_tokenizer.cpp), [`bm25_ranker.cpp`](file:///d:/amoeba/engine/src/rank/bm25_ranker.cpp)
* **Verified Behavior**:
  1. Inverted index is implemented in pure C++ with zero external dependencies.
  2. Tokens are extracted using CamelCase and snake_case subword splitting with lowercase normalization.
  3. Posting lists map terms to 32-bit `ElementId`s with field-weighted term frequencies and Okapi BM25 scoring.
  4. `CodeAwareRanker` applies multiplier boosts: $1.5\times$ for Class/Struct, $1.3\times$ for Function/Method, and penalizes nested usage.
  5. **Persistence Gap**: `InvertedIndex` has **no `save()` or `load()` methods**. It lives purely in transient RAM and is rebuilt from scratch on every run.
* **Recommendation**: Preserve the custom in-memory inverted index for query-time performance; add binary serialization or an embedded SQLite backing store for fast cold restarts.

---

### 3.4 Semantic Vector Index & Embeddings (`engine/src/semantic/`)
* **Status**: `Confirmed in current source` (Critical Architectural Discrepancy)
* **Code Location**: [`engine/src/semantic/pretrained_embedding_provider.cpp:L58-L220`](file:///d:/amoeba/engine/src/semantic/pretrained_embedding_provider.cpp#L58-L220), [`hnsw_semantic_index.cpp:L243-L330`](file:///d:/amoeba/engine/src/semantic/hnsw_semantic_index.cpp#L243-L330)
* **Verified Behavior**:
  1. **No Neural Network or Pretrained Weights**: Despite its name, `PretrainedEmbeddingProvider` does **not** load or execute a neural network (e.g. Qwen2.5-Coder, MiniLM, or CodeRank). It is an in-memory heuristic feature map:
     - Contains a hardcoded static lexicon (`kLexicon`) of ~50 English terms (`"auth"`, `"login"`, `"database"`, `"payment"`).
     - Computes dual 64-bit FNV-1a hashes of tokens and character 3-grams.
     - Projects hashes into a 384-dimensional vector, applies an approximate GELU non-linearity, and L2-normalizes the vector.
  2. **HNSW Vector Storage**: `HnswSemanticIndex` wraps `hnswlib::HierarchicalNSW<float>` and implements `save()` / `load()`. It writes an `"AMOH"` binary file and a `.graph` companion file.
  3. **Disconnection from CLI**: The CLI (`apps/cli/main.cpp:L302`) always instantiates a new `PretrainedEmbeddingProvider` and in-memory `HnswSemanticIndex`, **never calling `save()` or `load()`**.
* **Inference**: Semantic search works for concepts present in `kLexicon` or shared hash n-grams, but fails on general natural-language paraphrases where synonyms do not share lexical subwords or hash prefixes.
* **Recommendation**: Retain the `ISemanticVectorIndex` interface. For true local semantic search, provide a pluggable ONNX Runtime or local server adapter (e.g. Ollama embeddings with `bge-code` or `all-MiniLM-L6-v2`), while retaining the fast hash provider as a zero-dependency fallback.

---

### 3.5 Relationship Graph & Investigation (`engine/src/graph/`, `engine/src/investigation/`)
* **Status**: `Confirmed in current source` (Fast, Heuristic, Unpersisted)
* **Code Location**: [`engine/src/graph/relationship_graph.cpp:L8-L65`](file:///d:/amoeba/engine/src/graph/relationship_graph.cpp#L8-L65), [`bounded_investigation_service.cpp:L107-L360`](file:///d:/amoeba/engine/src/investigation/bounded_investigation_service.cpp#L107-L360)
* **Verified Behavior**:
  1. `RelationshipGraph` maintains dual adjacency maps (`outgoing_adj_`, `incoming_adj_`) keyed by `ElementId`.
  2. Traversals take $< 1\ \mu\text{s}$ in memory. BFS handles cycles and self-loops safely.
  3. `BoundedInvestigationService` selects up to 5 seeds from Hop-0 candidates using a linear suitability heuristic (hybrid score + entity boost + subject overlap + rank decay).
  4. Graph expansion traverses outgoing edges first, then incoming edges, in arbitrary hash-map iteration order.
  5. Expansion is not guided by user intent (e.g. "who calls X" expands outgoing calls before callers).
  6. Graph state is never persisted to disk.
* **Recommendation**: Preserve the custom in-memory adjacency structures for query execution; add intent-guided traversal (filtering edge directions based on query verbs like "callers" vs "callees") and persist edges to SQLite.

---

### 3.6 Query Understanding (`engine/src/retrieval/query_understanding.cpp`)
* **Status**: `Confirmed in current source` (Severe Benchmark Overfitting & Logic Flaws)
* **Code Location**: [`engine/src/retrieval/query_understanding.cpp:L20-L80, L798-L810`](file:///d:/amoeba/engine/src/retrieval/query_understanding.cpp#L20-L80)
* **Verified Behavior**:
  1. **Benchmark Repository Overfitting**: `kConversationalNoise` literally contains the 10 RepoProbe repository names:
     ```cpp
     "pkl", "dokploy", "beszel", "opencloud", "ducklake", "jiff", "walker", "docling", "adk", "better-auth"
     ```
     These names are filtered out as conversational noise terms during query parsing.
  2. **Interrogative Inversion Trap**: Lines 798–804 check for interrogatives ("where", "what", "how", "why", or `?`):
     ```cpp
     if (has_interrogative || (stopword_count >= 3 && rep.raw_terms.size() >= 6 && !has_code_punct)) {
         rep.intent = QueryIntent::NaturalLanguage;
         rep.recommended_alpha = 0.25;  // Semantic dominant
     }
     ```
     Because this check precedes `has_code_syntax`, asking *"Where is `PromptBuilder` implemented?"* unconditionally sets `recommended_alpha = 0.25` (75% semantic weight), even though it contains an exact code identifier!
  3. **Domain Words Stripped**: Common behavioral terms (`"error"`, `"errors"`, `"failed"`, `"behavior"`, `"behaviour"`) are categorized as `ConversationalFraming` and excluded from `clean_technical_query`.
* **Technical Impact**: Queries asking about error handling lose the word "error"; questions with exact symbols get misrouted to low-confidence semantic search when questions are phrased with interrogative prefixes.
* **Recommendation**: Remove hardcoded repository names; evaluate code syntax and exact identifiers BEFORE interrogatives; retain error and condition keywords in search terms.

---

### 3.7 Dual-Channel Hybrid Retrieval & CLI Overrides (`apps/cli/main.cpp`)
* **Status**: `Confirmed in current source` (Severe Configuration Drift)
* **Code Location**: [`apps/cli/main.cpp:L317-L331`](file:///d:/amoeba/apps/cli/main.cpp#L317-L331), [`primary_retrieval_pipeline.hpp:L68-L78`](file:///d:/amoeba/engine/include/amoeba/retrieval/primary_retrieval_pipeline.hpp#L68-L78)
* **Verified Behavior**:
  1. In `PrimarySearchOptions`, Phase 8.4 calibrated engine defaults are:
     - `alpha = 0.8` (80% lexical, 20% semantic)
     - `adaptive_fusion = false`
     - `max_results = 10`
  2. In `apps/cli/main.cpp`, the CLI **manually overrides these calibrated settings**:
     ```cpp
     amoeba::retrieval::PrimarySearchOptions search_opts{
         .alpha = 0.5,
         .lexical_ranker = amoeba::index::RankerType::CodeAware,
         .max_results = 5,
         .adaptive_fusion = true,
     };
     ```
  3. Because `adaptive_fusion = true`, queries with interrogatives trigger `recommended_alpha = 0.25`, completely reversing Phase 8.4's findings.
  4. Candidate pool is restricted to `max_results = 5` and `max_total_candidates = 5`, dropping candidates 6–15 that would have been rescued by graph investigation.
* **Recommendation**: Remove all hardcoded option overrides from CLI; make CLI inherit calibrated engine defaults; expose `--alpha` and `--max-results` as explicit user CLI flags.

---

### 3.8 Evidence Sufficiency Gate (`engine/src/evidence/evidence_sufficiency.cpp`)
* **Status**: `Confirmed in current source` (Over-Constrained Rules & False Refusals)
* **Code Location**: [`engine/src/evidence/evidence_sufficiency.cpp:L278-L343`](file:///d:/amoeba/engine/src/evidence/evidence_sufficiency.cpp#L278-L343)
* **Verified Behavior**:
  1. Evaluates Rule A (require at least one subject match), Rule A.1 (expanded relationships cannot manufacture subject grounding), Rule B (semantic-only score thresholds), and Rule E (distinguishing domain term grounding).
  2. Evaluates lexical matching using `contains_case_insensitive` across symbol name, file path, detail, supporting elements, and source excerpt.
  3. **False Refusal**: If a user describes a component using a synonym not present in the code or `kLexicon`, the gate rejects the query with a grounded refusal, even if the correct code was retrieved.
  4. **False Acceptance**: If a code unit's name contains a superficial match (e.g. `PromptBuilder`), but the function body does not contain the requested logic (e.g. "How does PromptBuilder serialize JSON?"), the gate reports sufficient evidence.
* **Recommendation**: Maintain deterministic negative refusal invariants for zero-match cases, but augment semantic coverage scoring using calibrated entailment verification.

---

### 3.9 ContextBuilder & Packaging (`engine/src/context/context_builder.cpp`)
* **Status**: `Confirmed in current source` (Severe Information Truncation)
* **Code Location**: [`engine/src/context/context_builder.cpp:L107-L136, L254-L276`](file:///d:/amoeba/engine/src/context/context_builder.cpp#L107-L136)
* **Verified Behavior**:
  1. `truncate_source_lines` hard-truncates any excerpt exceeding 50 lines:
     ```cpp
     result += "\n// ... [truncated " + std::to_string(line_count - max_lines) + " remaining lines]";
     ```
     If the target function logic, return value, or error handler occurs after line 50, it is deleted from the LLM prompt.
  2. **Greedy Item Dropping**: When an item exceeds the remaining character budget (4,000 chars in CLI / 16,000 chars default), the loop immediately executes `break;`, completely discarding all subsequent items.
  3. **Mid-Stream Syntax Corruption**: If the very first item exceeds the budget, `safe_utf8_prefix` cuts source code mid-stream, leaving unclosed brackets, quotes, or braces.
  4. Item count is clamped to `max_primary_items = 3` in the CLI.
* **Recommendation**: Implement **Anchor-Centered Windows** (centering the excerpt window on the target symbol's line rather than lines 1–50) and **Code Skeletonization** (rendering class signatures and folding non-target methods) to preserve 70% of character budget.

---

### 3.10 Reasoning Service & Citation Validation (`engine/src/reasoning/`)
* **Status**: `Confirmed in current source` (Aspiration vs Reality Discrepancy)
* **Code Location**: [`engine/src/reasoning/reasoning_service.cpp:L54-L66`](file:///d:/amoeba/engine/src/reasoning/reasoning_service.cpp#L54-L66), [`apps/cli/main.cpp:L380-L402`](file:///d:/amoeba/apps/cli/main.cpp#L380-L402)
* **Verified Behavior**:
  1. Historical documentation claimed: *"Validates that every file and line cited in the LLM answer exists in the retrieved context. None (hallucinated citations are rejected in C++)."*
  2. **Actual Code Inspection**: `ReasoningService` does **not** inspect or parse the generated LLM text for citations. It simply iterates over `context_package.selected_items` and attaches all of them into `result.citations`:
     ```cpp
     for (const auto& item : context_package.selected_items) {
         GroundedCitation citation;
         citation.file_path = item.file_path().generic_string();
         citation.symbol_name = elem.name.empty() ? item.file_path().filename().string() : elem.name;
         result.citations.push_back(std::move(citation));
     }
     ```
  3. In `apps/cli/main.cpp`, `reasoning_service.answer_stream` streams raw tokens directly from Ollama to `std::cout`. There is no post-generation validation or hallucination check on the streaming path.
  4. Windows-Only HTTP Dependency: `LocalLLMRuntime` uses `winhttp.lib`. On Linux or macOS, line 441 returns an error: `"Local LLM runtime currently requires Windows WinHTTP."`
* **Recommendation**: Implement true citation verification that parses file/line references from the generated markdown text and cross-references them against retrieved ranges; implement cross-platform sockets or libcurl for Linux/macOS compatibility.

---

### 3.11 Tool Abstraction Layer (`engine/src/tools/`)
* **Status**: `Confirmed in current source` (Implemented in Engine, Disconnected in Product)
* **Code Location**: [`engine/src/tools/`](file:///d:/amoeba/engine/src/tools/)
* **Verified Behavior**:
  1. A complete tool framework exists: `RepositoryTool`, `ToolRegistry`, `SearchCodeTool`, `FindSymbolTool`, `ReadFileTool`, `GetRelationshipsTool`, `RepositoryAccessPolicy`, `ToolEvidenceAdapter`.
  2. 60+ unit tests in `engine/tests/tools/` pass cleanly.
  3. **Zero Invocations**: Neither `apps/cli/main.cpp` nor any reasoning orchestrator invokes `ToolRegistry` or dynamic tools. The CLI uses a fixed linear pipeline.
* **Recommendation**: Preserve the tool abstraction. Expose these tools via an embedded JSON-RPC or MCP (Model Context Protocol) server so IDE extensions (VS Code) and agent orchestrators can invoke them dynamically.

---

## 4. Test Suite & Verification Matrix

### 4.1 Test Suite Inventory
* **[Fact] Total GoogleTests**: Exactly **542 tests** compiled in `amoeba_engine_tests.exe`.
* **Focused Execution Verification**:
  * Executed `EngineSmokeTest.EngineMetadata` $\to$ **PASSED** (0 ms).
  * Executed `RepositoryScannerStaticTest.HelperFilters` $\to$ **PASSED** (0 ms).
  * Executed `CodeTokenizerTest.CamelCaseSplitting` $\to$ **PASSED** (0 ms).
  * Verified that full test suite was NOT run unnecessarily, respecting investigation policy.

### 4.2 Comprehensive Status Register

| Component | Subsystem | File Location | Status | Primary Finding |
|---|---|---|---|---|
| **Scanner** | Exclusions | `repository_scanner.cpp:L17` | `Confirmed in current source` | Static array of 8 folders; ignores `.gitignore`, `.venv`, `vendor`. |
| **Scanner** | File Filters | `repository_scanner.cpp:L117` | `Confirmed in current source` | No file size cap; no binary detection; no cancellation. |
| **CLI** | Fusion Config | `apps/cli/main.cpp:L318` | `Confirmed in current source` | CLI overrides calibrated $\alpha=0.8$ to $0.5$ and forces `adaptive_fusion=true`. |
| **CLI** | Reservoir Depth | `apps/cli/main.cpp:L320` | `Confirmed in current source` | CLI clamps `max_results=5` and `max_candidates=5`, dropping ranks 6–15. |
| **Persistence** | Vector Index | `hnsw_semantic_index.cpp:L243`| `Partially resolved` | `save()`/`load()` exist in engine but CLI never invokes them. |
| **Persistence** | Lexical Index | `inverted_index.cpp` | `Confirmed in current source` | Zero serialization logic; index rebuilt in RAM every run. |
| **Persistence** | Graph Store | `relationship_graph.cpp` | `Confirmed in current source` | Zero serialization logic; graph rebuilt in RAM every run. |
| **Query** | Intent Logic | `query_understanding.cpp:L798` | `Confirmed in current source` | Interrogative prefix unconditionally forces $\alpha=0.25$ even with code symbols. |
| **Query** | Noise Filter | `query_understanding.cpp:L77` | `Confirmed in current source` | Overfitted: 10 benchmark repo names hardcoded in `kConversationalNoise`. |
| **Retrieval** | AST Scope | `code_element.hpp:L15` | `Confirmed in current source` | Control-flow, branches, and local variables excluded from AST. |
| **Evidence** | Sufficiency | `evidence_sufficiency.cpp:L278`| `Confirmed in current source` | False refusals on paraphrases; false acceptances on superficial symbol matches. |
| **Context** | Truncation | `context_builder.cpp:L107` | `Confirmed in current source` | Hard cut at line 50; greedy item dropping; mid-stream byte slicing. |
| **Reasoning** | Citation Check | `reasoning_service.cpp:L54` | `Confirmed in current source` | Citations copied from context package; zero inspection of LLM text. |
| **Reasoning** | Cross-Platform | `local_llm_runtime.cpp:L441` | `Confirmed in current source` | Windows-only `winhttp.lib`; Linux/macOS builds produce runtime error. |
| **Tools** | Integration | `apps/cli/main.cpp` | `Confirmed in current source` | Tool layer implemented and tested, but completely unused in CLI. |

---

## 5. Architectural Findings Summary

1. **The Core Engine is Not Yet a Reusable Facade**: There is no top-level `AmoebaEngine` class orchestrating scanning, indexing, retrieval, and reasoning. The entire orchestration is hardcoded inside `apps/cli/main.cpp`.
2. **Every Execution is Cold & Ephemeral**: Because neither lexical nor graph state is serialized, invoking `amoeba ask` on a large codebase re-scans disk, re-parses all files, re-computes embeddings, and rebuilds graphs every single time.
3. **Severe Retrieval Parameter Mismatch**: The CLI actively sabotages retrieval accuracy by overriding Phase 8.4's calibrated parameters ($\alpha = 0.8$, $k = 50$) with uncalibrated values ($\alpha = 0.5$, `adaptive_fusion = true`, $k = 5$).
4. **Heuristic Pseudo-Embeddings**: The semantic engine relies on hash projections and a 50-word hardcoded lexicon rather than a real neural embedding model.
5. **Clean Seams Already Exist**: `ISemanticVectorIndex`, `EmbeddingProvider`, `LLMRuntime`, and `RepositoryTool` provide well-designed extension points that can be built upon without rewriting the core.

