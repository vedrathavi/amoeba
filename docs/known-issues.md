# Known Issues & Architectural Limitations

This document tracks identified bugs, operational bottlenecks, and edge cases discovered during real-world usage and benchmarking of the Amoeba engine.

---

## 1. Hardcoded Directory Exclusions & Missing Ecosystem Filters (`venv`, `vendor`, `.gitignore`)

* **Category**: Filesystem Scanner / Ingestion Bottleneck
* **Severity**: High (causes perceived freeze, high memory consumption, or timeouts on large codebases)
* **Status**: Open (Documented & Reproducible)
* **Affected File**: [`engine/src/scanner/repository_scanner.cpp`](file:///d:/amoeba/engine/src/scanner/repository_scanner.cpp#L17-L19)

### Problem Description
When executing `amoeba_cli ask <repo-path> "<query>"` on a repository containing unexcluded dependency folders, virtual environments, or dataset directories, the CLI appears to hang indefinitely after printing:
```text
Amoeba
Source Code Search & Indexing Engine
```
If interrupted with `Ctrl+C`, the process exits with code 1.

### Root Cause Analysis
In [`engine/src/scanner/repository_scanner.cpp`](file:///d:/amoeba/engine/src/scanner/repository_scanner.cpp), the excluded directory array is a **static, hardcoded list of only 8 folder names**:

```cpp
constexpr array<string_view, 8> EXCLUDED_DIRECTORIES = {
    ".git", "node_modules", "build", "dist", "out", "target", "coverage", "benchmarks",
};
```

This static approach causes several major limitations:

#### A. Missing Python Virtual Environments & Caches
* Virtual environments: `.venv`, `venv`, `env`, `.env`, `virtualenv`
* Package distributions: `site-packages`, `dist-info`, `egg-info`
* Tool caches: `__pycache__`, `.pytest_cache`, `.mypy_cache`, `.ruff_cache`, `.tox`
* **Impact**: On Python/ML repositories (e.g. `D:\BTP`), Amoeba traverses into `site-packages/` and attempts to parse 50,000+ files from PyTorch, NumPy, SciPy, and other libraries with Tree-sitter and generate vector embeddings for each of them.

#### B. Missing Go & PHP Vendor Directories (`vendor/`)
* Go and PHP codebases often vendor their dependencies directly into a `vendor/` directory.
* **Impact**: External library code is indexed alongside the user's primary code, polluting search results and drowning out primary symbols.

#### C. Missing Frontend Framework & Tooling Build Caches
* Modern web frameworks generate large build outputs not covered by `build/dist`:
  * Next.js: `.next`
  * Nuxt: `.nuxt`
  * SvelteKit / Astro: `.svelte-kit`, `.astro`
  * Turbo / Vite caches: `.turbo`, `.vite`

#### D. Missing Java / JVM Build Tool Caches
* Gradle / Maven caches: `.gradle`, `.mvn`

#### E. Missing IDE & Editor Metadata Directories
* IDE folders containing XML/JSON and cached index files: `.idea`, `.vscode`

#### F. No `.gitignore` Parsing
* `RepositoryScanner` does not read or parse root or nested `.gitignore` files. Local data folders (such as `data/`, `datasets/`, `checkpoints/`, `runs/`, `logs/`) that the user explicitly ignored in Git are still scanned and parsed.

#### G. No Dynamic CLI Flag (`--exclude`)
* The CLI does not provide a `--exclude` or `--ignore` parameter, leaving users with no mechanism to exclude specific directories without changing directory roots.

### Temporary Workarounds
1. **Pass Subdirectories Directly**: Point the CLI to your specific code directory (e.g. `src/`, `models/`, `app/`) rather than the repository root:
   ```powershell
   .\build\amoeba_cli.exe ask "D:\BTP\src" "where is FedAvg implemented"
   ```
2. **Move/Rename Virtual Environments**: Place `.venv` outside the scanned workspace.

### Recommended Permanent Architecture Fixes
1. **Expand Default Static Exclusions**:
   ```cpp
   constexpr array<string_view, 24> EXCLUDED_DIRECTORIES = {
       ".git", "node_modules", "build", "dist", "out", "target", "coverage", "benchmarks",
       ".venv", "venv", "env", ".env", "__pycache__", ".pytest_cache", ".mypy_cache",
       ".ruff_cache", "site-packages", "vendor", ".gradle", ".next", ".nuxt", ".turbo",
       ".idea", ".vscode"
   };
   ```
2. **Implement `.gitignore` Parsing**: Load `.gitignore` rules at the start of `RepositoryScanner::scan()` to dynamically prune user-ignored folders.
3. **Add CLI `--exclude` Flag**: Support comma-separated directory exclusion overrides:
   ```powershell
   .\build\amoeba_cli.exe ask <repo> "<query>" --exclude="data,venv,checkpoints"
   ```

---

## 2. Low Candidate Limit in CLI `ask` Command (`k = 5`)

* **Category**: CLI Configuration / Retrieval Recall
* **Severity**: Medium
* **Status**: Open
* **Affected File**: [`apps/cli/main.cpp`](file:///d:/amoeba/apps/cli/main.cpp#L318-L331)

### Problem Description
Natural language queries with conversational filler (e.g. `"where does prompting ?"`) trigger the Evidence Sufficiency Gate refusal even when matching symbols (like `PromptBuilder`) exist in the repository.

### Root Cause Analysis
In [`apps/cli/main.cpp`](file:///d:/amoeba/apps/cli/main.cpp#L320), the CLI overrides the engine's calibrated defaults:
```cpp
amoeba::retrieval::PrimarySearchOptions search_opts{
    .alpha = 0.5,
    .lexical_ranker = amoeba::index::RankerType::CodeAware,
    .max_results = 5,           // Overrides calibrated Top-50 reservoir to only 5
    .adaptive_fusion = true,
};
```
When conversational stopwords dilute top-ranked semantic results, genuine target symbols pushed to ranks 6–15 are completely dropped before reaching the Bounded Investigation graph or Evidence Sufficiency checker.

### Proposed Fix
Align the CLI with the Phase 9.3B/9.3C engine parameters:
* Set `.alpha = 0.8`
* Set `.max_results = 15` (or use the Top-50 reservoir)
* Set `invest_opts.max_total_candidates = 15` and `max_seeds = 5`

---

## 3. In-Memory Indexing Latency on Every Invocation

* **Category**: Persistence / CLI User Experience
* **Severity**: Medium
* **Status**: Documented Architecture Gap
* **Affected File**: [`apps/cli/main.cpp`](file:///d:/amoeba/apps/cli/main.cpp)

### Problem Description
Every invocation of `amoeba_cli search` or `amoeba_cli ask` re-scans the repository from disk, re-parses all files via Tree-sitter, and rebuilds the BM25 inverted index and HNSW vector graph in memory.

### Proposed Fix
Implement disk persistence for the InvertedIndex and HnswSemanticIndex using memory-mapped serialization (e.g., `.amoeba/index.bin`), checking repository file timestamps before triggering re-indexing.

---

## 4. Shallow Query Understanding (Heuristic Keyword Normalization)

* **Category**: Query Understanding / Intent Classification
* **Severity**: High
* **Status**: Open Architectural Limitation
* **Affected File**: [`engine/src/retrieval/query_understanding.cpp`](file:///d:/amoeba/engine/src/retrieval/query_understanding.cpp)

### Problem Description
Current query processing relies strictly on deterministic string prefix stripping, subword splitting, and hardcoded action-verb dictionaries (`kGenericActionBases`). It normalizes queries into raw terms but does not parse deep semantic intent.

### Limitations & Failure Modes
1. **Inability to Differentiate Query Intent**:
   * *"Where is `PromptBuilder` implemented?"* $\to$ Needs exact symbol lookup & file definition.
   * *"What component is responsible for assembling the final prompt?"* $\to$ Needs conceptual semantic search + reranking.
   * *"What calls `build_system_prompt`?"* $\to$ Needs incoming call-graph edge exploration.
   * *"Why does this handler return an early error?"* $\to$ Needs control-flow inspection, error branch analysis, and local variable context.
   Today, all 4 queries pass through the exact same keyword pipeline.
2. **Fragility of Natural-Language Paraphrases**:
   If a user describes a component behaviorally without mentioning the symbol name, heuristic noise stripping can strip or dilute the distinguishing semantic concepts.

### Proposed Architectural Evolution
* **Introduce a Small Query Understanding Transformer / On-Device Classifier**:
  * Predict structured intent: `intent` (find_definition, trace_callers, explain_architecture), `target_entity`, `constraints`, and `expected_answer_type`.
  * Keep exact identifier extraction deterministic.

---

## 5. Fixed Hybrid-Search Weights (`alpha = 0.8` Rigidity)

* **Category**: Retrieval Fusion / Ranking
* **Severity**: High
* **Status**: Open Architectural Limitation
* **Affected Files**: [`engine/src/hybrid/fusion_strategy.cpp`](file:///d:/amoeba/engine/src/hybrid/fusion_strategy.cpp), [`primary_retrieval_pipeline.hpp`](file:///d:/amoeba/engine/include/amoeba/retrieval/primary_retrieval_pipeline.hpp)

### Problem Description
BM25 lexical scores and HNSW semantic scores are combined via a single global linear coefficient ($\alpha = 0.8$ lexical, $0.2$ semantic):
$$\text{Score} = \alpha \cdot S_{\text{lexical}} + (1 - \alpha) \cdot S_{\text{semantic}}$$

### Limitations & Failure Modes
1. **One Size Does Not Fit All**:
   * For exact identifier queries (`"PromptBuilder"`), $\alpha = 0.9$ or $1.0$ is optimal.
   * For conceptual questions (*"how does error recovery work during parsing?"*), lexical tokens are absent or misleading; an $\alpha = 0.2$ or $0.3$ is needed to let dense semantic vectors surface relevant files.
   * Fixing $\alpha = 0.8$ penalizes purely conceptual questions where no exact code keywords exist.

### Proposed Architectural Evolution
* **Phase 1 (Query-Aware Dynamic Alpha)**: Use intent from Query Understanding to dynamically select $\alpha$ (e.g. $\alpha = 0.9$ for identifier queries, $\alpha = 0.3$ for conceptual queries).
* **Phase 2 (Learned Ranking Model / Reranker)**: Introduce a lightweight cross-encoder or GBDT (Gradient Boosted Decision Tree) to score candidates using multiple features (BM25, vector similarity, symbol type, path depth, graph connectivity).

---

## 6. Heuristic Graph Seed Selection & Traversal

* **Category**: Graph Navigation / Relationship Expansion
* **Severity**: High
* **Status**: Open Architectural Limitation
* **Affected File**: [`engine/src/investigation/bounded_investigation_service.cpp`](file:///d:/amoeba/engine/src/investigation/bounded_investigation_service.cpp)

### Problem Description
Investigation seeds are chosen using static linear heuristics (rank decay + technical entity overlap + AST connectivity). Graph traversal naively explores 1-hop and 2-hop edges in order of node discovery.

### Limitations & Failure Modes
1. **Proximity Does Not Equal Relevance**:
   A function directly called by a seed (1-hop) may be an uninteresting utility (`std::string::empty()`, `printf()`), while a critical business-logic dependency is 2 or 3 edges away or connected via a weak lexical match.
2. **Combinatorial Traversal Explosions**:
   In dense OOP hierarchies or heavily connected web frameworks, expanding outgoing edges indiscriminately pollutes the candidate pool with generic helpers.

### Proposed Architectural Evolution
* **Intent-Guided Graph Navigation**:
  * Let query intent dictate which edge types matter:
    * If query asks *"who calls X"*, only traverse **Incoming Calls**.
    * If query asks *"what implements interface Y"*, only traverse **Implements / InheritsFrom**.
  * Keep graph traversal bounded and deterministic ($depth \le 2$, candidates $\le 15$), but use a neural path-scorer to rank which edges to expand.

---

## 7. Brittleness of the Lexical Evidence Sufficiency Gate

* **Category**: Verification / Hallucination Prevention
* **Severity**: High
* **Status**: Open Architectural Limitation
* **Affected File**: [`engine/src/evidence/evidence_sufficiency.cpp`](file:///d:/amoeba/engine/src/evidence/evidence_sufficiency.cpp)

### Problem Description
The Evidence Sufficiency Gate programmatically checks whether query subjects appear in primary code units (Rule A, Rule A.1, Rule E). 

### Limitations & Failure Modes
1. **False Refusals on Paraphrases**:
   If the user asks *"Where is prompt engineering done?"*, the engine succeeds because `prompt` matches `PromptBuilder`. But if the user asks *"Where are AI instructions formatted?"*, the gate rejects the retrieved `PromptBuilder` because neither `"ai"` nor `"instructions"` appear literally in `prompt_builder.hpp`.
2. **False Approvals on Superficial Matches**:
   A query matching a symbol by name can pass the gate even if the retrieved function body does not actually contain the logic required to answer the question.

### Proposed Architectural Evolution
* **Hybrid Evidence Verifier**:
  * Retain deterministic rules as a baseline for provenance and hard citation validation.
  * Supplement with a calibrated cross-encoder semantic verifier that scores whether the candidate code chunk actually entails or supports the user's specific claim.

---

## 8. Structural AST Scope Loss (Exclusion of Control Flow & Variables)

* **Category**: AST Modeling & Extraction
* **Severity**: Medium
* **Status**: Known Design Trade-off
* **Affected File**: [`engine/src/parser/source_parser.cpp`](file:///d:/amoeba/engine/src/parser/source_parser.cpp), [`code_element.hpp`](file:///d:/amoeba/engine/include/amoeba/parser/code_element.hpp)

### Problem Description
To prevent graph and index bloat, Amoeba's Tree-sitter extractors only index declarations (`Class`, `Struct`, `Function`, `Method`, `Import`, `Call`). Local variables, control-flow branches (`if/else`, `switch`, `early return`), and runtime configuration entries are ignored.

### Limitations & Failure Modes
* **Inability to Answer Behavioral "Why" Questions**:
  * Questions such as *"Why does `scan()` return early?"* or *"Under what conditions is `is_sufficient` set to false?"* cannot be answered from symbol declarations alone. The answer resides in internal branch conditions and local boolean flags.

### Proposed Architectural Evolution
* Maintain the AST declaration table as the primary index, but introduce **Function Body Slicing**: index full executable method blocks for top-scoring entities so control-flow and error-handling conditions are accessible to retrieval.

---

## 9. ContextBuilder Truncation & Item Dropping (Information Loss Risks)

* **Category**: Context Packaging / Prompt Construction
* **Severity**: High
* **Status**: Open Architectural Limitation
* **Affected File**: [`engine/src/context/context_builder.cpp`](file:///d:/amoeba/engine/src/context/context_builder.cpp), [`context_builder.hpp`](file:///d:/amoeba/engine/include/amoeba/context/context_builder.hpp)

### Problem Description
To guarantee fast generation times (< 2 seconds) and avoid out-of-memory (VRAM) crashes on local LLMs (`qwen2.5-coder:1.5b`), `ContextBuilder` enforces strict resource bounds:
1. `max_character_budget = 4000` total characters.
2. `max_primary_items = 5` items.
3. `max_source_lines_per_item = 50` lines.
4. `max_supporting_elements_per_item = 5` AST nodes.
5. `max_relationships_per_item = 5` graph edges.

While these caps prevent prompt overflow, **they inherently risk dropping or truncating critical information** needed by the LLM to provide a correct, hallucination-free answer.

### The 4 Truncation & Dropping Mechanisms

1. **Source Line Truncation (`truncate_source_lines`, L107–L136)**:
   * Any source excerpt exceeding 50 lines is hard-truncated at line 50 and appended with `// ... [truncated N remaining lines]`.
   * **Failure Mode**: In large functions or classes (e.g., 100–300 lines), if the target logic, error handling, return value, or inner algorithm occurs after line 50, the LLM literally never sees it. The LLM may hallucinate what the function does or incorrectly claim that error handling or a specific feature does not exist.

2. **Greedy Item-Level Dropping (L254–L276)**:
   * Candidates are iterated in descending retrieval score order. If adding candidate $i$ would push `current_markdown.size() + item_text.size() > 4000`, candidate $i$ and **all remaining candidates are completely dropped**.
   * **Failure Mode (Dependency Orphanage)**: If Item 1 is a 2,500-character function and Item 2 is a 1,800-character helper called by Item 1, Item 2 is dropped. The LLM is asked *"How does X work?"*, sees that Item 1 delegates to Item 2, but has zero visibility into Item 2's implementation.

3. **Hard Item Count Cap (`max_primary_items`, L157–L162)**:
   * Even if each candidate item is very compact (e.g., 5-line declarations taking only 200 characters each), `items_to_process` is clamped to at most 5 items. The remaining 10 items in a 15-item reservoir are ignored, wasting character budget that could have provided valuable context.

4. **Mid-Stream Prefix Slicing (`safe_utf8_prefix`, L14–L29, L263–L274)**:
   * If even the very first item exceeds the 4,000-character budget, `ContextBuilder` truncates it midway through using UTF-8 byte boundary preservation and appends `... [Context truncated to fit budget]`.
   * **Failure Mode (Syntax Invalidation)**: Cutting off raw source code mid-item produces unclosed braces (`{`), unfinished strings, or unclosed comments. Small LLMs often get confused by syntactically invalid code blocks, leading to degraded reasoning or generation loops.

### Summary of Information Lost

| Truncation / Drop Type | What is Lost | Real-World Consequence on LLM |
| :--- | :--- | :--- |
| **Line 50+ Truncation** | Return statements, loop bodies, callee invocations, edge-case checks. | *"Function X does not validate inputs"* (when validation is at line 75). |
| **Knapsack Item Dropping** | Callee definitions, config structs, caller context. | Hallucinated helper logic; inability to trace multi-file execution paths. |
| **Graph Relationship Cap (> 5)** | Secondary callers/callees in hub classes. | Incomplete dependency lists when answering architectural questions. |
| **Item Count Cap (> 5 items)** | Broad repository coverage in multi-module queries. | Incomplete answers to questions like *"Where are all rankers implemented?"* |

### Proposed Architectural Evolutions

1. **Focal Symbol Slicing (Anchor-Centered Windows)**:
   * Instead of naively taking lines 1 to 50 of the enclosing file or parent construct, compute the focal symbol's exact location $[L_{start}, L_{end}]$ from Tree-sitter.
   * Center the 50-line window on the symbol: $[L_{start} - 15, L_{start} + 35]$, ensuring the symbol's actual body is visible.

2. **AST Skeletonization (Code Folding)**:
   * For large classes or functions, do not send raw text lines.
   * Send the AST skeleton (class signature, field names, and method signatures) and only expand the full body of the specific method relevant to the query. This saves ~70% of character budget while retaining full structural context.

3. **Knapsack with Compression instead of All-or-Nothing Dropping**:
   * If Item 2 exceeds the remaining budget, instead of dropping it entirely, downgrade it to a **summary representation**: `[File + Symbol Name + Signature + 1-line Docstring]`. This gives the LLM partial evidence rather than total blindness.

4. **Dynamic Line Allocation by Score**:
   * Instead of a flat 50 lines per item, allocate lines proportionally to the normalized hybrid score: Candidate #1 receives 80 lines, while Candidate #3 receives 25 lines.

---

## 10. Summary Matrix: Where AI Should Replace Rules vs. What Must Remain Deterministic

| Pipeline Stage | Recommended Architecture | Role of Deterministic Code | Role of Neural / AI Models |
| :--- | :--- | :--- | :--- |
| **Scanning & File Filtering** | **100% Deterministic** | Filesystem walk, `.gitignore` parsing, permission checks. | None (adds latency and unpredictability). |
| **AST Syntax Extraction** | **100% Deterministic** | Tree-sitter grammars; exact line/column symbol ranges. | None (syntax rules must be exact). |
| **Symbol Tables & Locations** | **100% Deterministic** | Stable 32-bit `ElementId` mapping, file provenance. | None. |
| **Tokenization & BM25** | **100% Deterministic** | Subword splitting, inverted postings lists, TF-IDF. | None. |
| **Query Understanding** | **Hybrid (Neural + Rules)** | Exact identifier and path extraction. | Transformer intent classification, constraint extraction, paraphrase parsing. |
| **Score Fusion & Ranking** | **Learned Ranking / Reranking** | Candidate generation ($k=50$) via BM25 + HNSW. | Cross-encoder reranking over Top-20 candidates based on query-code relevance. |
| **Graph Navigation** | **Deterministic Structure + Learned Navigation** | Adjacency list storing real `Calls`, `Inherits`, `Imports`. | Path relevance scoring to guide which 1-hop/2-hop edges to expand. |
| **Evidence Sufficiency Gate** | **Hybrid Gate** | Provenance verification, 100% negative refusal invariants. | Semantic entailment scoring between evidence and query claim. |
| **Context Packaging** | **Structured Context Planner** | Strict character and token budget enforcement (4,000 chars), UTF-8 validation. | Intent-based planning of complementary evidence (caller + callee + config). |
| **Answer Generation** | **Transformer LLM** | Verification that cited lines/symbols actually exist in prompt. | Natural language synthesis, code explanation via Ollama (`qwen2.5-coder`). |
| **Citation Validation** | **100% Deterministic** | Validates that every file and line cited in the LLM answer exists in the retrieved context. | None (hallucinated citations are rejected in C++). |


