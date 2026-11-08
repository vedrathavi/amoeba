# Amoeba — Phase 10: Consolidated Issues & Technical Doubts Register

**Date**: November 2026  
**Auditor**: Senior Software Architect & Technical Reviewer  
**Status**: Complete  
**Scope**: Definitive issue register analyzing historical concerns, codebase reality, root causes, tradeoffs, and prioritized actions.

---

## Issue Group 1: Filesystem Ingestion & Scanner Bottlenecks

### Issue 1.1: Hardcoded Directory Exclusions & Missing Ecosystem Filters
* **Original Concern**: Directory exclusions are hardcoded; virtual environments (`.venv`), package vendor directories (`vendor/`, `site-packages/`), and build caches (`.next`, `.turbo`) are scanned, causing massive indexing hangs.
* **Historical Context**: Documented in `docs/known-issues.md` (§1) after testing on large Python/ML repositories (e.g. `D:\BTP`).
* **Current Implementation**: [`engine/src/scanner/repository_scanner.cpp:L17-L19`](file:///d:/amoeba/engine/src/scanner/repository_scanner.cpp#L17-L19) defines a static array:
  ```cpp
  constexpr array<string_view, 8> EXCLUDED_DIRECTORIES = {
      ".git", "node_modules", "build", "dist", "out", "target", "coverage", "benchmarks",
  };
  ```
* **Verification Status**: `Confirmed in current source`.
* **Root Cause**: Hardcoded, minimalist exclusion array lacking modern framework caches and package directories.
* **User Impact**: In Python, Go, PHP, or Next.js codebases, Amoeba crawls tens of thousands of library dependency files, freezing the CLI for several minutes and exhausting system RAM.
* **Technical Impact**: 50,000+ unnecessary files are parsed with Tree-sitter and projected into vectors, polluting the symbol index with third-party symbols.
* **Possible Solutions**:
  1. Expand static exclusions to 24+ common modern directories (`.venv`, `venv`, `env`, `site-packages`, `vendor`, `.next`, `.nuxt`, `.turbo`, `.gradle`, `.idea`, `.vscode`, `__pycache__`).
  2. Implement proper `.gitignore` rule parsing (root and nested).
  3. Add a `--exclude` CLI argument for dynamic user-specified folder ignores.
* **Tradeoffs**: Adding static exclusions is trivial ($O(1)$) but incomplete; `.gitignore` parsing adds small traversal overhead but matches developer intent perfectly.
* **Priority**: **P0 (Immediate / Critical)**
* **Recommended Next Action**: Expand static exclusions immediately, implement `.gitignore` parser in Phase 11, and expose `--exclude` flag in the CLI.

---

### Issue 1.2: Missing File Size Bounds, Binary Detection & Scan Cancellation
* **Original Concern**: Large minified files or binary files with code extensions cause memory spikes; long scans cannot be cancelled cleanly.
* **Historical Context**: Discussed in `docs/doubts.md` (§8) regarding scanner robustness.
* **Current Implementation**: [`engine/src/scanner/repository_scanner.cpp:L95-L130`](file:///d:/amoeba/engine/src/scanner/repository_scanner.cpp#L95-L130).
  ```cpp
  const auto file_size = entry.file_size(ec);
  result.files.push_back(FileInfo{ .path = entry.path(), .extension = ext, .size = ec ? 0 : file_size });
  ```
  No maximum file size check is performed before pushing to `result.files`.
* **Verification Status**: `Confirmed in current source`.
* **Root Cause**: `RepositoryScanner` accepts every file with a matching extension regardless of size, character encoding, or binary content.
* **User Impact**: A 100MB generated `.js` file or bundled model weight causes an out-of-memory exception or long hang.
* **Technical Impact**: Unbounded memory allocation during `parse_file(path)`.
* **Possible Solutions**:
  1. Enforce a default file size limit (e.g. 2 MB) skipping larger files with a warning.
  2. Inspect the first 512 bytes for null bytes (`\0`) to detect binary files masquerading with source extensions.
  3. Pass an `std::atomic<bool>& cancel_token` or progress callback.
* **Tradeoffs**: Skipping large files might miss generated code, but generated code rarely contains primary human-authored architectural logic.
* **Priority**: **P1 (High)**
* **Recommended Next Action**: Add `max_file_size_bytes` (default 2 MB) and null-byte check to `RepositoryScanner`.

---

## Issue Group 2: CLI Configuration Drift & Candidate Starvation

### Issue 2.1: CLI Inversion of Calibrated Retrieval Settings
* **Original Concern**: The CLI runtime in `apps/cli/main.cpp` overrides engine defaults, setting $\alpha = 0.5$ and re-enabling broken adaptive fusion.
* **Historical Context**: Phase 8.4 (`docs/phase-08.4-hybrid-fusion-recalibration.md`) empirically established that $\alpha = 0.8$ with `adaptive_fusion = false` achieved the highest Hit@1 (28.8%), Hit@10 (54.5%), and MRR (0.3854).
* **Current Implementation**: [`apps/cli/main.cpp:L317-L322`](file:///d:/amoeba/apps/cli/main.cpp#L317-L322):
  ```cpp
  amoeba::retrieval::PrimarySearchOptions search_opts{
      .alpha = 0.5,
      .lexical_ranker = amoeba::index::RankerType::CodeAware,
      .max_results = 5,
      .adaptive_fusion = true,
  };
  ```
* **Verification Status**: `Confirmed in current source`.
* **Root Cause**: Developer never propagated the Phase 8.4 benchmark calibration from engine headers into `apps/cli/main.cpp`.
* **User Impact**: Conversational user questions (e.g. *"Where is PromptBuilder?"*) trigger `recommended_alpha = 0.25`, causing the semantic channel to dominate and suppress exact code identifiers.
* **Technical Impact**: End-to-end recall drops from 54.5% to 45.5% on positive tasks.
* **Possible Solutions**:
  1. Strip hardcoded overrides from `main.cpp`; use default `PrimarySearchOptions{}`.
  2. Let CLI flags (`--alpha`, `--adaptive-fusion`) override engine defaults only when explicitly passed by the user.
* **Tradeoffs**: None. This is a pure bug fix aligning CLI with verified engine calibration.
* **Priority**: **P0 (Immediate)**
* **Recommended Next Action**: Delete hardcoded `.alpha = 0.5` and `.adaptive_fusion = true` from `apps/cli/main.cpp`.

---

### Issue 2.2: Severe Candidate Pool Starvation ($k = 5$)
* **Original Concern**: CLI sets `max_results = 5` and `max_total_candidates = 5`, dropping ranks 6–15 before Bounded Investigation can inspect them.
* **Historical Context**: Documented in `docs/phase-09.1-top50-reservoir-investigation.md` and `docs/known-issues.md` (§2). Phase 9 proved that a Top-50 reservoir rescued 13 out of 16 missing candidates via graph expansion.
* **Current Implementation**: [`apps/cli/main.cpp:L320, L330`](file:///d:/amoeba/apps/cli/main.cpp#L320):
  ```cpp
  search_opts.max_results = 5;
  invest_opts.max_total_candidates = 5;
  ctx_opts.max_primary_items = 3;
  ```
* **Verification Status**: `Confirmed in current source`.
* **Root Cause**: CLI enforces arbitrary micro-limits to keep console output brief, starving the retrieval pipeline.
* **User Impact**: Valid search results pushed to ranks 6–10 by conversational noise are discarded, causing false "Insufficient Evidence" refusals.
* **Technical Impact**: Bounded investigation cannot expand relationships from high-quality secondary candidates.
* **Possible Solutions**:
  1. Maintain a candidate reservoir of $k = 50$ internally.
  2. Pass top 15 investigated candidates to Evidence Assembler and Context Builder.
  3. Display top 3–5 items in CLI output, but allow downstream reasoning to access the full 15-item evidence pool.
* **Tradeoffs**: Evaluating 15 items in ContextBuilder takes $< 1\text{ ms}$; no noticeable latency penalty.
* **Priority**: **P0 (Immediate)**
* **Recommended Next Action**: Align CLI options with Phase 9.3B parameters (`search_opts.max_results = 50`, `invest_opts.max_total_candidates = 15`, `ctx_opts.max_primary_items = 5`).

---

## Issue Group 3: Ephemeral State & Missing Persistence

### Issue 3.1: Complete Re-Indexing on Every CLI Invocation
* **Original Concern**: Running `amoeba ask` or `amoeba search` re-scans the repository, re-parses every file via Tree-sitter, re-computes embeddings, and rebuilds graphs every time.
* **Historical Context**: Tracked in `docs/known-issues.md` (§3) and `docs/doubts.md` (§1).
* **Current Implementation**: [`apps/cli/main.cpp:L270-L305`](file:///d:/amoeba/apps/cli/main.cpp#L270-L305):
  ```cpp
  scanner.scan(repo_path);
  // for file in files: parser.parse_file(file); index.add_parsed_file(parsed);
  RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
  PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider);
  ```
* **Verification Status**: `Confirmed in current source`.
* **Root Cause**: No persistent repository state manager or index cache directory (`.amoeba/`).
* **User Impact**: 10-second startup latency on moderate codebases (1,000 files) and multi-minute latency on large codebases for every single question asked.
* **Technical Impact**: CPU cycles and RAM are wasted regenerating identical ASTs and embeddings.
* **Possible Solutions**:
  1. Add `.amoeba/` directory containing SQLite database storing file paths, mtimes, hashes, symbol metadata, inverted index postings, and graph edges.
  2. Implement an incremental update pass: only re-parse files whose `last_write_time` or hash changed since the previous scan.
  3. Load persisted HNSW index via `HnswSemanticIndex::load()` instead of re-embedding every primary unit.
* **Tradeoffs**: Requires designing a robust schema and invalidation protocol, but completely transforms user experience from sluggish to instant.
* **Priority**: **P1 (Core Architectural Need)**
* **Recommended Next Action**: Design `IMetadataStore` and SQLite schema in Phase 10; implement in Phase 11.

---

### Issue 3.2: Missing Serialization for InvertedIndex and RelationshipGraph
* **Original Concern**: Neither `InvertedIndex` nor `RelationshipGraph` support serialization to disk.
* **Historical Context**: Only `HnswSemanticIndex` had `save()` and `load()` implemented in Phase 6.0/8.0.
* **Current Implementation**:
  - [`engine/include/amoeba/index/inverted_index.hpp`](file:///d:/amoeba/engine/include/amoeba/index/inverted_index.hpp) has no `save()` or `load()` methods.
  - [`engine/include/amoeba/graph/relationship_graph.hpp`](file:///d:/amoeba/engine/include/amoeba/graph/relationship_graph.hpp) has no `save()` or `load()` methods.
* **Verification Status**: `Confirmed in current source`.
* **Root Cause**: Components were originally implemented as in-memory data structures during algorithmic research phases.
* **User Impact**: Contributes directly to Issue 3.1.
* **Technical Impact**: Cannot save or load pre-computed indices.
* **Possible Solutions**:
  1. Option A: Binary flat-file serialization for each structure (`index.bin`, `graph.bin`).
  2. Option B: Store postings and graph edges in embedded SQLite relational tables (`postings`, `edges`).
* **Tradeoffs**: SQLite provides transactional safety, crash resilience, and ACID guarantees; raw binary files are slightly faster to dump but prone to corruption on sudden process termination.
* **Priority**: **P1 (High)**
* **Recommended Next Action**: Implement SQLite-backed persistence for symbol metadata, postings, and graph relationships.

---

## Issue Group 4: Query Understanding Flaws & Destructive Heuristics

### Issue 4.1: Interrogative Inversion Trap
* **Original Concern**: Natural language questions starting with "Where", "What", or "How" are misclassified as purely conceptual, dropping lexical weighting.
* **Historical Context**: Added in Phase 8.6 (`docs/phase-08.6-query-subject-normalization.md`) to handle conversational issue queries.
* **Current Implementation**: [`engine/src/retrieval/query_understanding.cpp:L798-L804`](file:///d:/amoeba/engine/src/retrieval/query_understanding.cpp#L798-L804):
  ```cpp
  if (has_interrogative || (stopword_count >= 3 && rep.raw_terms.size() >= 6 && !has_code_punct)) {
      rep.intent = QueryIntent::NaturalLanguage;
      rep.recommended_alpha = 0.25;  // Semantic dominant
  }
  ```
* **Verification Status**: `Confirmed in current source`.
* **Root Cause**: The condition `has_interrogative` is checked **before** checking whether the query contains exact code identifiers, camelCase terms, or technical entities.
* **User Impact**: Asking *"Where is `PromptBuilder` implemented?"* assigns $\alpha = 0.25$ (75% semantic weight), burying the exact identifier match.
* **Technical Impact**: Lexical channel is heavily penalized on queries that contain explicit code symbols simply because they end in `?` or begin with `"Where"`.
* **Possible Solutions**:
  1. Invert check precedence: If `rep.has_code_syntax || !rep.technical_entities.empty()`, classify as `IdentifierOrTechnical` ($\alpha = 0.8$) regardless of interrogatives.
  2. Only classify as `NaturalLanguage` ($\alpha = 0.25$) when technical entities and code syntax are completely absent.
* **Tradeoffs**: Preserves high-precision lexical matches for identifier questions while allowing conceptual questions to use semantic search.
* **Priority**: **P0 (Immediate)**
* **Recommended Next Action**: Invert the intent check order in `QueryUnderstanding::analyze()`.

---

### Issue 4.2: Hardcoded Benchmark Overfitting in Stopwords
* **Original Concern**: Query noise stripping contains hardcoded repository names from the benchmark dataset.
* **Historical Context**: Added during Phase 8.8 (`docs/phase-08.8-repository-noise-investigation.md`) to improve RepoProbe benchmark scores.
* **Current Implementation**: [`engine/src/retrieval/query_understanding.cpp:L77-L78`](file:///d:/amoeba/engine/src/retrieval/query_understanding.cpp#L77-L78):
  ```cpp
  const std::unordered_set<std::string_view> kConversationalNoise = {
      ...
      "pkl", "dokploy", "beszel", "opencloud", "ducklake", "jiff", "walker", "docling", "adk", "better-auth"
  };
  ```
* **Verification Status**: `Confirmed in current source`.
* **Root Cause**: Benchmark dataset leakage into production engine source code.
* **User Impact**: When indexing any real-world repository other than these 10 (e.g. `fastapi`, `react`, `amoeba`), the repository name will NOT be filtered out, polluting query subjects. Conversely, if a codebase has a symbol literally named `pkl` or `walker`, it gets stripped as conversational noise!
* **Technical Impact**: Violates benchmark integrity and harms generalization.
* **Possible Solutions**:
  1. Remove hardcoded repository names from `kConversationalNoise`.
  2. Dynamically register the current target repository's root folder name as an ignored query term at runtime.
* **Tradeoffs**: Dynamic repository name registration solves the problem universally without hardcoding.
* **Priority**: **P0 (High Integrity Fix)**
* **Recommended Next Action**: Remove all benchmark repo strings from `kConversationalNoise`; inject `root_path.filename().string()` as dynamic noise at runtime.

---

### Issue 4.3: Destructive Stripping of Error and Behavioral Terms
* **Original Concern**: Terms like `"error"` and `"behavior"` are categorized as conversational noise, preventing retrieval of error handling logic.
* **Historical Context**: Documented in `docs/known-issues.md` (§4).
* **Current Implementation**: [`engine/src/retrieval/query_understanding.cpp:L58-L67`](file:///d:/amoeba/engine/src/retrieval/query_understanding.cpp#L58-L67):
  `"error"`, `"errors"`, `"failed"`, `"failure"`, `"behavior"`, `"behaviour"` are in `kConversationalNoise`.
* **Verification Status**: `Confirmed in current source`.
* **Root Cause**: Conflating GitHub issue filler ("I encountered an error...") with domain search keywords ("Why does X return an error?").
* **User Impact**: Questions asking about errors or failure paths have their key search terms stripped.
* **Technical Impact**: `clean_technical_query` omits the very concepts needed to find error handling code.
* **Possible Solutions**:
  1. Remove `"error"`, `"errors"`, `"fail"`, `"failure"` from `kConversationalNoise`.
  2. Classify them as `QueryTermRole::Subject` or `Condition` when adjacent to code entities.
* **Tradeoffs**: May occasionally include "error" when user writes "I had an error", but BM25 handles frequent words naturally via IDF downweighting.
* **Priority**: **P1 (High)**
* **Recommended Next Action**: Reclassify error and status terms as searchable domain tokens.

---

## Issue Group 5: Graph Investigation & Traversal Limitations

### Issue 5.1: Intent-Blind Graph Traversal
* **Original Concern**: Bounded investigation traverses 1-hop and 2-hop edges without considering what the user asked (e.g. expanding callees when asked for callers).
* **Historical Context**: Documented in `docs/known-issues.md` (§6) and `docs/phase-09.0-bounded-investigation.md`.
* **Current Implementation**: [`engine/src/investigation/bounded_investigation_service.cpp:L254-L360`](file:///d:/amoeba/engine/src/investigation/bounded_investigation_service.cpp#L254-L360):
  Always traverses `graph_.outgoing_relationships(seed_id)` first, then `incoming_relationships(seed_id)`.
* **Verification Status**: `Confirmed in current source`.
* **Root Cause**: Graph traversal logic has no awareness of `query_rep.action_terms` or query directionality.
* **User Impact**: When asking *"Who calls `parse_file`?"*, Amoeba expands all internal functions called *by* `parse_file`, filling the 15-item candidate budget before callers are reached.
* **Technical Impact**: Candidate budget is polluted with unhelpful opposite-direction edges.
* **Possible Solutions**:
  1. Inspect query intent:
     - If query contains "calls", "who calls", "callers": prioritize **Incoming Calls**.
     - If query contains "implements", "inherits", "subclasses": prioritize **Incoming Inheritance**.
     - If query contains "uses", "depends on": prioritize **Outgoing Calls & Imports**.
* **Tradeoffs**: Simple deterministic direction routing dramatically boosts recall on directional queries with zero extra latency.
* **Priority**: **P1 (High)**
* **Recommended Next Action**: Add intent-guided relationship kind and direction filtering to `BoundedInvestigationService`.

---

### Issue 5.2: Unranked Expansion of Hub Nodes & Utility Functions
* **Original Concern**: Traversing outgoing edges on utility-heavy functions pulls in generic standard library calls or helpers (`std::string::empty()`, `printf()`, `malloc()`), crowding out domain logic.
* **Historical Context**: Analyzed in Phase 8.8 (`docs/phase-08.8-repository-noise-investigation.md`).
* **Current Implementation**: [`engine/src/investigation/bounded_investigation_service.cpp:L256-L260`](file:///d:/amoeba/engine/src/investigation/bounded_investigation_service.cpp#L256-L260):
  Edges are expanded in raw vector order up to `options.max_discovered_per_seed`.
* **Verification Status**: `Confirmed in current source`.
* **Root Cause**: Target nodes are not filtered or prioritized by in-degree (centrality) or lexical relevance.
* **User Impact**: Irrelevant standard helpers appear as primary supporting evidence.
* **Technical Impact**: High-in-degree nodes (hub functions) monopolize the candidate pool.
* **Possible Solutions**:
  1. Downweight or skip target nodes with very high in-degree ($> 50$ callers, e.g. utility logging/formatting functions).
  2. Score discovered candidates using BM25 relevance against query subjects before accepting them into the candidate pool.
* **Tradeoffs**: High-degree nodes are rarely the unique architectural answer a developer is seeking.
* **Priority**: **P2 (Medium)**
* **Recommended Next Action**: Add in-degree threshold filtering and relevance scoring to candidate expansion.

---

## Issue Group 6: ContextBuilder Information Loss & Truncation

### Issue 6.1: Hard Source Line Truncation at Line 50
* **Original Concern**: Any source file or function exceeding 50 lines is hard-truncated at line 50.
* **Historical Context**: Documented in `docs/known-issues.md` (§9.1).
* **Current Implementation**: [`engine/src/context/context_builder.cpp:L107-L136`](file:///d:/amoeba/engine/src/context/context_builder.cpp#L107-L136):
  `truncate_source_lines(exc.text, 50)` stops writing lines once `line_count == 50`.
* **Verification Status**: `Confirmed in current source`.
* **Root Cause**: Naive line-count limiter designed to keep token count low for small LLMs (`qwen2.5-coder:1.5b`).
* **User Impact**: If a function or class definition has 80 lines and its critical error handler or return logic starts at line 55, the LLM literally never sees it and hallucinates that the logic does not exist.
* **Technical Impact**: Truncation cuts off mid-scope, destroying code block semantics.
* **Possible Solutions**:
  1. Implement **Anchor-Centered Windows**: Use the symbol's exact start line from Tree-sitter (`location.start.line`) and center a window around it ($L_{start} - 10$ to $L_{start} + 40$).
  2. Implement **Code Skeletonization**: Render signatures and docstrings of parent classes, only expanding the full body of the focal method.
* **Tradeoffs**: Anchor-centered windows require minimal code changes while guaranteeing the target symbol's body is visible.
* **Priority**: **P1 (High)**
* **Recommended Next Action**: Replace top-of-file line truncation with anchor-centered symbol windows.

---

### Issue 6.2: Greedy All-or-Nothing Item Dropping & Mid-Stream Slicing
* **Original Concern**: If candidate $i$ exceeds the remaining character budget, candidate $i$ and all remaining candidates are completely dropped.
* **Historical Context**: Documented in `docs/known-issues.md` (§9.2, §9.4).
* **Current Implementation**: [`engine/src/context/context_builder.cpp:L254-L276`](file:///d:/amoeba/engine/src/context/context_builder.cpp#L254-L276):
  ```cpp
  if (options_.max_character_budget > 0 && current_markdown.size() + item_text.size() > options_.max_character_budget) {
      pkg.truncated = true;
      if (pkg.selected_items.empty()) {
          current_markdown += std::string(safe_utf8_prefix(item_text, avail));
      }
      break;  // Drops all subsequent items!
  }
  ```
* **Verification Status**: `Confirmed in current source`.
* **Root Cause**: Simple greedy knapsack loop with hard `break`.
* **User Impact**: If Item 1 is large, Item 2 (a critical helper function or caller) is completely omitted. If Item 1 exceeds the budget alone, its text is cut mid-token, producing broken C++ syntax with unclosed braces that confuses LLM reasoning.
* **Technical Impact**: LLM suffers from "dependency orphanage" (sees a function call but cannot see the definition of the callee).
* **Possible Solutions**:
  1. When budget is low, compress candidates into **symbol signatures only** (`[File + Symbol Name + Signature + Docstring]`) rather than dropping them.
  2. Enforce clean AST boundary truncation (cut at function or statement end) instead of raw byte slicing.
* **Tradeoffs**: Compressed signatures provide valuable structural hints without consuming line budget.
* **Priority**: **P1 (High)**
* **Recommended Next Action**: Implement compressed signature fallbacks for overflow candidates in `ContextBuilder`.

---

## Issue Group 7: Reasoning Service & Cross-Platform Limitations

### Issue 7.1: Missing LLM Citation Verification
* **Original Concern**: Engine claims that hallucinated citations are validated and rejected in C++, but the code does not inspect LLM output.
* **Historical Context**: Claimed in historical documentation (`docs/known-issues.md:L324`).
* **Current Implementation**: [`engine/src/reasoning/reasoning_service.cpp:L54-L66`](file:///d:/amoeba/engine/src/reasoning/reasoning_service.cpp#L54-L66):
  Citations are copied blindly from `context_package.selected_items`. The LLM generated response string (`resp.content`) is never parsed or cross-referenced.
* **Verification Status**: `Confirmed in current source` (Discrepancy verified).
* **Root Cause**: The citation validation step was specified in design docs but omitted during implementation.
* **User Impact**: If the LLM generates a hallucinated file name or line range in its explanation, Amoeba outputs it without warning.
* **Technical Impact**: No algorithmic guarantee of citation grounding on the generative output path.
* **Possible Solutions**:
  1. Implement a markdown citation parser that extracts `path:line` patterns from the LLM text.
  2. Verify that every parsed citation corresponds to a file and line range present in `context_package`.
  3. Flag or redact unverified citations in the final output.
* **Tradeoffs**: Post-processing the generated text takes $< 1\text{ ms}$ and provides real grounding integrity.
* **Priority**: **P2 (Medium)**
* **Recommended Next Action**: Add regex-based citation extraction and range verification in `ReasoningService`.

---

### Issue 7.2: Windows-Only Networking in Local LLM Runtime
* **Original Concern**: `LocalLLMRuntime` depends on Windows WinHTTP API and fails on Linux/macOS.
* **Historical Context**: Implemented in Phase 8.1 for fast native Windows REST calls without libcurl.
* **Current Implementation**: [`engine/src/reasoning/local_llm_runtime.cpp:L20-L22, L440-L442`](file:///d:/amoeba/engine/src/reasoning/local_llm_runtime.cpp#L20-L22):
  ```cpp
  #if defined(_WIN32)
  #include <winhttp.h>
  ...
  #else
  sink.on_event(ReasoningEvent::error("Local LLM runtime currently requires Windows WinHTTP."));
  #endif
  ```
* **Verification Status**: `Confirmed in current source`.
* **Root Cause**: Windows-specific network API used without cross-platform fallback.
* **User Impact**: Non-Windows users cannot run `amoeba ask`.
* **Technical Impact**: Breaks cross-platform portability.
* **Possible Solutions**:
  1. Implement POSIX standard sockets for Linux/macOS.
  2. Use a lightweight single-header HTTP client or standard `libcurl`.
* **Tradeoffs**: Standard sockets require minimal boilerplate (~150 lines) to do HTTP POST to localhost:11434.
* **Priority**: **P2 (Portability)**
* **Recommended Next Action**: Implement cross-platform POSIX loopback socket client for Linux/macOS.

---

## Issue Group 8: Semantic Embedding Realism & Vectors

### Issue 8.1: Heuristic Hash Feature Map Masquerading as Pretrained Model
* **Original Concern**: `PretrainedEmbeddingProvider` is not a neural network; it is a hash projection with a small hand-crafted word dictionary.
* **Historical Context**: Implemented in Phase 6.1 (`docs/phases/phase-06.1-real-code-embeddings.md`) as a self-contained local substitute for external models.
* **Current Implementation**: [`engine/src/semantic/pretrained_embedding_provider.cpp:L60-L220`](file:///d:/amoeba/engine/src/semantic/pretrained_embedding_provider.cpp#L60-L220):
  Dual FNV-1a hash of tokens and character 3-grams projected into 384 dimensions with a static 50-word `kLexicon`.
* **Verification Status**: `Confirmed in current source`.
* **Root Cause**: Intentionally built as a zero-dependency local baseline, but named "Pretrained" which creates false expectations.
* **User Impact**: Conceptual questions where the user does not use exact tokens or `kLexicon` terms fail to match code semantically.
* **Technical Impact**: The semantic channel behaves like a noisy n-gram hash index rather than a true semantic concept space.
* **Possible Solutions**:
  1. Keep the hash provider as `HeuristicEmbeddingProvider` for zero-install, zero-RAM offline environments.
  2. Implement an `OllamaEmbeddingProvider` or `OnnxEmbeddingProvider` that calls local models (e.g. `nomic-embed-text`, `bge-code`, or `all-MiniLM-L6-v2`) when available.
* **Tradeoffs**: Real embeddings provide genuine synonym and concept matching; hash embeddings provide instant, zero-RAM execution.
* **Priority**: **P2 (Semantic Quality Evolution)**
* **Recommended Next Action**: Rename to `HeuristicEmbeddingProvider`; introduce real local model provider via ONNX Runtime or Ollama embeddings.

---

## Issue Summary Matrix

| ID | Issue Description | Category | Status | Priority | Action Phase |
|---|---|---|---|---|---|
| **1.1** | Hardcoded directory exclusions & missing `.gitignore` | Scanner | Confirmed | **P0** | Phase 11 |
| **1.2** | Unbounded file size & missing binary check | Scanner | Confirmed | **P1** | Phase 11 |
| **2.1** | CLI overriding calibrated $\alpha=0.8$ and adaptive fusion | CLI Config | Confirmed | **P0** | Phase 11 |
| **2.2** | Candidate starvation in CLI ($k=5$ vs $k=50$) | CLI Config | Confirmed | **P0** | Phase 11 |
| **3.1** | In-memory index rebuild on every execution | Persistence | Confirmed | **P1** | Phase 11 |
| **3.2** | Missing serialization for InvertedIndex & Graph | Persistence | Confirmed | **P1** | Phase 11 |
| **4.1** | Interrogative prefix forcing $\alpha=0.25$ on symbol queries | Query Understanding | Confirmed | **P0** | Phase 11 |
| **4.2** | Benchmark repo names hardcoded in `kConversationalNoise` | Benchmark Integrity | Confirmed | **P0** | Phase 11 |
| **4.3** | Destructive stripping of error and behavioral terms | Query Understanding | Confirmed | **P1** | Phase 11 |
| **5.1** | Intent-blind graph traversal (outgoing before incoming) | Graph Investigation | Confirmed | **P1** | Phase 11 |
| **5.2** | Unranked hub node expansion & utility function noise | Graph Investigation | Confirmed | **P2** | Phase 12 |
| **6.1** | Hard source line truncation at line 50 | ContextBuilder | Confirmed | **P1** | Phase 11 |
| **6.2** | Greedy item dropping & mid-stream syntax slicing | ContextBuilder | Confirmed | **P1** | Phase 11 |
| **7.1** | Lack of actual LLM citation validation in C++ | Reasoning | Confirmed | **P2** | Phase 12 |
| **7.2** | Windows-only `winhttp.lib` dependency in LLM runtime | Portability | Confirmed | **P2** | Phase 12 |
| **8.1** | Heuristic hash feature map masquerading as pretrained model | Semantic Vector | Confirmed | **P2** | Phase 12 |

