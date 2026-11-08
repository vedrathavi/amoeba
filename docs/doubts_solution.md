# Amoeba Architecture & Technical Doubts Solution Reference

This document provides comprehensive, senior-engineer-level answers, trade-off analyses, and code citations corresponding to every question in [`docs/doubts.md`](file:///d:/amoeba/docs/doubts.md).

---

## 1. Root & System Architecture

### Q1.1: What external tools / libraries are used at each stage?
* **Scanning**: **Zero external libraries**. Built entirely on modern C++20 `std::filesystem` ([`repository_scanner.cpp`](file:///d:/amoeba/engine/src/scanner/repository_scanner.cpp)).
* **AST Parsing**: **Tree-sitter** C core runtime (`v0.22.6`) + 11 official Tree-sitter C language grammars compiled directly via CMake `FetchContent` ([`CMakeLists.txt:L28-L137`](file:///d:/amoeba/CMakeLists.txt#L28-L137)).
* **Lexical Indexing (BM25)**: **Zero external libraries**. Custom in-memory inverted index, subword tokenizer, and Okapi BM25 implementation ([`inverted_index.cpp`](file:///d:/amoeba/engine/src/index/inverted_index.cpp), [`bm25_ranker.cpp`](file:///d:/amoeba/engine/src/rank/bm25_ranker.cpp)).
* **Semantic Indexing**: **`hnswlib`** (header-only C++ library for Hierarchical Navigable Small World graphs) + internal embedding provider ([`hnsw_semantic_index.cpp`](file:///d:/amoeba/engine/src/semantic/hnsw_semantic_index.cpp)).
* **Graph & Investigation**: **Zero external libraries**. Custom directed graph using `std::unordered_map` ([`relationship_graph.cpp`](file:///d:/amoeba/engine/src/graph/relationship_graph.cpp)).
* **Evidence Sufficiency Gate**: **Zero external libraries**. Pure algorithmic rule checks ([`evidence_sufficiency.cpp`](file:///d:/amoeba/engine/src/evidence/evidence_sufficiency.cpp)).
* **Local LLM Runtime**: **Native OS Networking** (`winhttp.lib` on Windows / POSIX sockets on Linux) making HTTP REST calls over local loopback to an **Ollama** server running `qwen2.5-coder:1.5b` ([`local_llm_runtime.cpp`](file:///d:/amoeba/engine/src/reasoning/local_llm_runtime.cpp)).
* **Testing**: **GoogleTest** (`v1.14.0`).

---

### Q1.2: If we make decisions dynamically, does it involve LLMs or rules? Does it increase querying cost?
* **Rules vs. LLMs**: In Amoeba, all routing, pruning, seed selection, and sufficiency decisions are made using **deterministic C++ algorithmic rules**, **not LLMs**.
* **Why**:
  1. **Latency**: A C++ rule executes in **$< 10\text{ microseconds}$**. An LLM inference call takes **$500\text{ ms} - 2\text{ seconds}$**.
  2. **Cost & Computation**: Calling an LLM at every decision point consumes GPU VRAM, exhausts local compute, and would cost money if using cloud APIs.
  3. **Determinism**: Rule-based gates provide a 100% reproducible guarantee (e.g. 100% negative refusal) that cannot be derailed by stochastic LLM temperature sampling.
* If dynamic LLM routing were introduced (e.g. an LLM agent deciding whether to search deeper), latency would spike by $10\times$ to $50\times$.

---

### Q1.3: Can each stage be used as a microservice or tool? Can an LLM call whichever tool is required? Does this lay the foundation for multiple agents?
* **Yes**: Amoeba already defines an explicit Tool abstraction layer in [`engine/src/tools/`](file:///d:/amoeba/engine/src/tools/):
  * `SearchCodeTool` ([`search_code_tool.cpp`](file:///d:/amoeba/engine/src/tools/search_code_tool.cpp))
  * `FindSymbolTool` ([`find_symbol_tool.cpp`](file:///d:/amoeba/engine/src/tools/find_symbol_tool.cpp))
  * `ReadFileTool` ([`read_file_tool.cpp`](file:///d:/amoeba/engine/src/tools/read_file_tool.cpp))
  * `GetRelationshipsTool` ([`get_relationships_tool.cpp`](file:///d:/amoeba/engine/src/tools/get_relationships_tool.cpp))
  * Managed by `ToolRegistry` ([`tool_registry.cpp`](file:///d:/amoeba/engine/src/tools/tool_registry.cpp)) with a strict `RepositoryAccessPolicy`.
* **Multi-Agent Foundation**: An autonomous agent loop (like ReAct or an MCP server) can expose these C++ tools as function calls (JSON-RPC). An agent can query symbols, inspect callers, read snippets, and decide its next step dynamically.

---

### Q1.4: How do competitors (GitHub Copilot, Codex, Cursor) do this?
* **GitHub Copilot**: Uses a combination of local editor heuristics (recently opened tabs, cursor proximity, prompt prefix/suffix) and server-side embeddings with small bi-encoder models.
* **Cursor / Sourcegraph Cody**: Operates very similarly to Amoeba. They parse ASTs (using Tree-sitter), build an index of codebase symbols, use a vector database + lexical search (like Tantivy or Lucene), traverse code graphs (LSIF / SCIP / call graphs), and feed the pruned context window to an LLM.
* **Codex**: Was an early raw foundation model without a retrieval engine; modern code assistants all wrap foundation models in retrieval and graph-navigation pipelines like Amoeba.

---

### Q1.5: Which part is memory-intensive and which part can be distributed?
* **Memory-Intensive**:
  1. **HNSW Vector Graph**: High-dimensional float vectors (e.g. 384 or 768 floats per symbol) plus multi-layer graph link pointers require significant RAM.
  2. **Inverted Index**: Posting lists for tens of thousands of unique tokens across millions of lines of code.
* **What can be distributed**:
  * **Embedding Generation**: Offloadable to background worker pools or GPUs.
  * **Repository Sharding**: Large enterprise repos (millions of files) can be sharded by top-level directories or services across separate index nodes.
  * **LLM Inference**: Can run on a dedicated central inference server or GPU cluster.

---

### Q1.6: What about error handling?
* **Filesystem level**: Uses `std::error_code` with `skip_permission_denied` so locked or unreadable files do not crash the scan ([`repository_scanner.cpp:L95`](file:///d:/amoeba/engine/src/scanner/repository_scanner.cpp#L95)).
* **Parsing level**: `try { parser.parse_file(...) } catch (...)` skips malformed or corrupted files during batch ingestion ([`apps/cli/main.cpp:L293`](file:///d:/amoeba/apps/cli/main.cpp#L293)).
* **Networking level**: Handles HTTP 404 (model not pulled), 500, or socket disconnections gracefully with user-friendly error messages ([`local_llm_runtime.cpp:L397`](file:///d:/amoeba/engine/src/reasoning/local_llm_runtime.cpp#L397)).
* **Sufficiency level**: Instead of letting the LLM hallucinate or throwing an unhandled exception on out-of-domain queries, it catches the condition algorithmically and formats a clean grounded refusal ([`evidence_sufficiency.cpp:L43`](file:///d:/amoeba/engine/src/evidence/evidence_sufficiency.cpp#L43)).

---

### Q1.7: How do we test each stage and accuracy?
* **Unit & Integration Testing**: **539 GoogleTests** in [`engine/tests/`](file:///d:/amoeba/engine/tests/) testing each component in isolation (e.g. `InvertedIndexTest`, `BoundedInvestigationServiceTest`, `EvidenceSufficiencyTest`).
* **Empirical Retrieval Benchmarks**: Standalone C++ benchmark harnesses (`repoprobe_benchmark`, `ann_benchmark`) measuring Information Retrieval metrics:
  * **Hit@k** (Hit@1, Hit@5, Hit@10, Hit@50)
  * **MRR** (Mean Reciprocal Rank)
  * **NDCG@k** (Normalized Discounted Cumulative Gain)
* **Real-World Repository Suite**: Evaluated against **RepoProbe v1** (70 canonical ground-truth tasks across 10 diverse production repositories: `ducklake`, `adk-python`, `better-auth`, `dokploy`, `pkl`, `beszel`, `opencloud`, `jiff`, `walker`, `docling`).

---

### Q1.8: Is it a RAG? Why and how is this different from any RAG?
* **Yes, it is a Retrieval-Augmented Generation system**, but it is fundamentally different from **naïve/generic document RAG**:

| Naïve / Generic RAG (Python / LangChain) | Amoeba Code Intelligence Engine |
| :--- | :--- |
| Chunks text by raw characters or line counts (e.g. 500 characters with 50-token overlap). | Chunks code along **AST syntax boundaries** (classes, functions, methods) via Tree-sitter. |
| Breaks functions in half; splits comments from signatures. | Keeps cohesive parent-child scope units (`RetrievalUnit`). |
| Pure dense vector cosine search. Fails on exact identifier lookups (e.g. `HnswSemanticIndex`). | **Dual-Channel Hybrid Fusion ($\alpha=0.8$)**: BM25 inverted index + HNSW dense vector graphs. |
| Zero code graph awareness. | **Bounded Graph Investigation**: Traverses 1-hop & 2-hop caller $\to$ dispatcher $\to$ executor edges. |
| Feeds whatever was retrieved into the LLM; LLM hallucinates if evidence is irrelevant. | **Evidence Sufficiency Gate**: Programmatically validates coverage; **100% negative refusal invariant**. |
| High latency (Python interpreter, GIL, remote cloud API calls). | **Native C++20**: In-memory retrieval and graph walk in **$< 15\text{ ms}$**. |

---

### Q1.9: Why don't we embed code elements using a 137M embedding model and turn all into embeddings instead of keeping BM25 lexical terms?
* **Empirical Proof**: Evaluated in Phase 8.4 ([`docs/phase-08.4-hybrid-fusion-recalibration.md`](file:///d:/amoeba/docs/phase-08.4-hybrid-fusion-recalibration.md)):
  * Pure Semantic Embeddings ($\alpha = 0.0$): **Hit@1 = 10.6%** (7/66)
  * Pure Lexical BM25 ($\alpha = 1.0$): **Hit@1 = 28.8%** (19/66)
  * Calibrated Hybrid ($\alpha = 0.8$): **Hit@1 = 28.8%, Hit@50 = 81.8%**
* **The 4 Fatal Flaws of Pure Embeddings on Code**:
  1. **The Exact Identifier Problem (Cosine Collapse)**: `process_payment_v1` and `process_payment_v2` have a cosine similarity of $0.99$. Embeddings cannot reliably prioritize `v2` over `v1`. BM25 knows with 100% certainty which file contains `"v2"`.
  2. **"Topic Vibe" Drift**: Embeddings match on general topical similarity (e.g., matching 15 different files discussing "auth" when the user asked for a specific token validation function).
  3. **Computational Overhead**: Generating embeddings for 20,000 code units on CPU takes 30–60 seconds; BM25 indexes in ~350 ms.
  4. **The Long-Tail Rare Token Problem**: Technical acronyms and hashes are shredded by neural BPE tokenizers, smearing the vector.
* **Why Both Together**: BM25 provides razor-sharp Top-1 identifier precision; dense embeddings provide a safety net at depth (ranks 10–50) for conceptual queries where keywords mismatch.

---

## 3. Multi-Language AST Parsing

### Q3.1: How can we map different languages with different grammars to a common representation?
* All modern programming languages share common structural abstractions:
  * Scopes / Types (`Class`, `Struct`, `Interface`, `Enum`)
  * Executable blocks (`Function`, `Method`, `Constructor`)
  * Dependency links (`Import`, `Include`, `Use`, `Require`)
  * Execution flow (`Call`, `Instantiate`, `Inherit`)
* Tree-sitter generates a concrete syntax tree per language. Language-specific extractors in [`engine/src/parser/extractors/`](file:///d:/amoeba/engine/src/parser/extractors/) map grammar-specific AST node types to the universal enum **`ElementKind`** ([`code_element.hpp:L15-L35`](file:///d:/amoeba/engine/include/amoeba/parser/code_element.hpp#L15-L35)).

### Q3.2: How is `CodeElement` generated? What does it consider and what does it ignore?
* **Defined in**: [`engine/include/amoeba/parser/code_element.hpp`](file:///d:/amoeba/engine/include/amoeba/parser/code_element.hpp)
* **What it considers (Captured)**:
  * `kind`: Class, Struct, Function, Method, Import, Call, Route, etc.
  * `name`: Identifier name (e.g. `PromptBuilder`).
  * `location`: 1-indexed `start` line/col and `end` line/col.
  * `parent_context`: Enclosing namespace or class (e.g. `amoeba::reasoning`).
  * `detail`: Signature or return type (e.g. `std::string build_system_prompt()`).
  * `doc_comment`: Bound documentation comments directly preceding the definition.
* **What it ignores (Filtered out)**:
  * Local variables inside function bodies (e.g., `int x = 5;`).
  * Basic arithmetic and loop control statements (`for`, `while`, `if`).
  * Pure whitespace, syntax punctuation, and interior method braces.

---

## 4. Tokenizer & Inverted Index

### Q4.1: Why only two types of tokens (CamelCase / snake_case)? Why not BPE (Byte-Pair Encoding) or n-grams?
* **BPE is designed for neural language models, not lexical exact matching**:
  * BPE chops identifiers into statistical sub-syllables (e.g. `PromptBuilder` might become `["Pro", "mpt", "Bu", "ilder"]`). This destroys lexical precision because programmers search for whole conceptual terms like `"prompt"` or `"builder"`.
* **Word n-grams explode the index**: Indexing character or word n-grams across a million lines of code increases index size by $5\times - 10\times$ without boosting code search accuracy.
* **CamelCase + snake_case subword splitting** directly mirrors how software engineers name variables and symbols.

### Q4.2: Doesn't it create too much complexity and waste memory?
* No. `CodeTokenizer` normalizes strings in-place. The inverted index stores postings as `std::vector<Posting>` with compact 32-bit `ElementId` integers.

### Q4.3: Which library is used?
* **Zero external libraries**. Written from scratch in [`engine/src/index/code_tokenizer.cpp`](file:///d:/amoeba/engine/src/index/code_tokenizer.cpp) using C++ string algorithms.

### Q4.4: Is it scalable? Does the inverted index create a mesh topology?
* An inverted index is **not a mesh topology**; it is an **inverted dictionary (bipartite mapping)**:
  $$\text{Term} \longrightarrow \text{Array of [ElementId, TermFrequency, FieldWeight]}$$
* It is the same architecture powering Apache Lucene, Elasticsearch, and Google Search. It scales to millions of postings with $O(1)$ term hash lookup and linear array intersection.

---

## 6. Relationship Graph

### Q6.1: Which library is used?
* **Zero external graph libraries** (no Neo4j, no Boost.Graph). Custom in-memory adjacency list built in [`engine/src/graph/relationship_graph.cpp`](file:///d:/amoeba/engine/src/graph/relationship_graph.cpp) using `std::unordered_map<ElementId, vector<RelationshipEdge>>`.

### Q6.2: Do we create graphs between `CodeElement`s? Are `Include`, `Call`, `InheritsFrom` code elements?
* Yes. In [`code_element.hpp`](file:///d:/amoeba/engine/include/amoeba/parser/code_element.hpp), `Call`, `Include`, and `InheritsFrom` are extracted as AST `CodeElement` objects during parsing.
* In [`repository_graph_builder.cpp`](file:///d:/amoeba/engine/src/graph/repository_graph_builder.cpp), the builder connects these elements:
  * Node: Definition of function `A` (`ElementId 10`)
  * Directed Edge (`RelationshipKind::Calls`): Target function `B` (`ElementId 20`)

### Q6.3: Why don't we use something like Graphify?
* External graph databases require running separate database daemons, network serialization (JSON/Cypher queries), and high memory overhead. Amoeba's custom C++ graph resolves edges in **$< 1\text{ microsecond}$** directly in RAM using flat memory lookups.

### Q6.4: How does the graph handle variables, functions, and classes?
* **Classes & Functions**: Stored as primary nodes.
* **Calls & Inherits**: Stored as directed edges between functions/classes.
* **Variables**: Local variables inside function bodies are deliberately **not** stored as graph nodes to prevent graph bloat; member variables/fields of classes are stored as `Field` elements.

### Q6.5: Walkthrough of the example:
```c
main() {
    int x, y;
    a(); 
    b();
    return 0;
}
a() {}
b() {}
```
* **Step 1 (Parser)**: Extracts three function elements: `main` (ElementId 1), `a` (ElementId 2), and `b` (ElementId 3). Local variables `int x, y` are ignored. It extracts two call nodes inside `main`: `Call: a` and `Call: b`.
* **Step 2 (Graph Builder)**: Resolves symbol names:
  * `main` $\xrightarrow{\text{Calls}}$ `a` (Outgoing edge from 1 to 2; Incoming edge at 2 from 1)
  * `main` $\xrightarrow{\text{Calls}}$ `b` (Outgoing edge from 1 to 3; Incoming edge at 3 from 1)
* When a user investigates `a()`, the graph can immediately answer: *"Who calls `a()`? `main()` calls `a()`."*

---

## 7. User Query Understanding

### Q7.1: Why is noise stripping done? What if important info is lost?
* Natural language queries contain syntactic filler: `"where is the"`, `"how does"`, `"can you find"`.
* If conversational stopwords are left in the query, dense vector embeddings drift towards generic conversational text, and lexical BM25 scores get diluted by common English words.
* **Protection against loss**: In [`query_understanding.cpp`](file:///d:/amoeba/engine/src/retrieval/query_understanding.cpp), stripping is strictly limited to an explicit list of conversational question prefixes and non-semantic action verbs (`describe`, `explain`). Identifiers, capitalized words, camelCase tokens, and domain terms are strictly preserved.

### Q7.2: Who does this stage: LLM or our service? Why is it hardcoded? How to make it dynamic?
* **Our C++ service does this**, not an LLM.
* **Why**: It executes in **$< 0.1\text{ ms}$** without calling an external model.
* **How to make it dynamic**: Rather than hardcoded string tables, pass an intent classifier configuration, or use a tiny on-device token classifier.

### Q7.3: Why not POS tagging, Seq2Seq RNNs, or LLM query expansion?
* **Latency & Simplicity**: A Seq2Seq RNN or POS tagger (like spaCy or NLTK) adds heavy dependencies and 20–100ms latency.
* **Code is structured**: Software queries are heavily identifier-driven (e.g. `PromptBuilder`, `HNSW`, `FedAvg`). Simple rule-based camelCase and subword extraction outperforms general NLP models on code tokens.
* **LLM Query Expansion**: LLM expansion is powerful for broad topics, but adds 1–2 seconds before search even starts.

### Q7.4: Where does query embedding happen?
* In [`primary_retrieval_pipeline.cpp:L130-L140`](file:///d:/amoeba/engine/src/retrieval/primary_retrieval_pipeline.cpp), immediately before searching the HNSW vector graph. The query string is passed to `PretrainedEmbeddingProvider::compute_embedding()`.

---

## 8. Dual-Channel Hybrid Search & Fusion

### Q8.1: Why hardcode $\alpha = 0.8$? How to make it dynamic?
* **Empirical Grounding**: In Phase 8.4 ([`docs/phase-08.4-hybrid-fusion-recalibration.md`](file:///d:/amoeba/docs/phase-08.4-hybrid-fusion-recalibration.md)), a grid search evaluated $\alpha$ from 0.0 to 1.0 across 70 tasks:
  * Pure Semantic ($\alpha=0.0$): 42.8% Hit@10
  * Balanced ($\alpha=0.5$): 54.3% Hit@10
  * **Calibrated ($\alpha=0.8$): 62.1% Hit@10** (Highest recall).
* Code search is fundamentally identifier-exact; lexical BM25 deserves higher weight (80%) than soft semantic concepts (20%).
* **Making it dynamic**: [`query_understanding.hpp:L106`](file:///d:/amoeba/engine/include/amoeba/retrieval/query_understanding.hpp#L106) already includes `recommended_alpha`. If the query has zero code symbols and is purely conceptual (*"how does error handling work"*), it can dynamically adjust $\alpha \to 0.3$.

### Q8.2: Why not weight other factors? Where is ranking done?
* Ranking is done inside [`FusionStrategy::fuse_weighted()`](file:///d:/amoeba/engine/src/hybrid/fusion_strategy.cpp#L8-L45) in `amoeba::hybrid`.
* Amoeba *does* weight other factors: `CodeAwareRanker` applies symbol-type multipliers (e.g. Class declarations get $1.5\times$, function declarations get $1.3\times$, and comments get $0.5\times$).

---

## 9. Bounded Graph Investigation

### Q9.1: How are the 5 primary seeds picked?
* In [`bounded_investigation_service.cpp`](file:///d:/amoeba/engine/src/investigation/bounded_investigation_service.cpp), candidates from the Hop-0 Top-50 pool are scored using multi-signal scoring:
  $$\text{Score}(c) = S_{\text{rank\_decay}}(c) + S_{\text{entity\_match}}(c) + S_{\text{subject\_grounding}}(c) + S_{\text{ast\_connectivity}}(c)$$
* Up to 5 candidates with the highest scores above `min_seed_relevance` are chosen as anchors for graph expansion.

### Q9.2: What does snippet extraction do?
* [`SourceSnippetReader`](file:///d:/amoeba/engine/src/source/source_snippet_reader.cpp) opens the source file on disk, locates the symbol's `start.line` and `end.line`, expands by a small padding window (e.g. 5 lines above and below), and extracts the clean, line-numbered source text.

### Q9.3: Do we re-rank the whole list? What is considered for the next stage?
* Yes. Seeds, discovered 1-hop relationships, discovered 2-hop relationships, and original high-ranking fallbacks are merged into a unified candidate pool.
* **Pool limit**: The pool is strictly capped at **$\le 15$ items** ([`investigation_types.hpp:L78`](file:///d:/amoeba/engine/include/amoeba/investigation/investigation_types.hpp#L78)) to fit safely inside the context budget.

---

## 10. Evidence Sufficiency Verification Gate

### Q10.1: What are the rules? Are they hardcoded? Can we make them dynamic?
* Implemented in [`engine/src/evidence/evidence_sufficiency.cpp`](file:///d:/amoeba/engine/src/evidence/evidence_sufficiency.cpp):
  * **Rule A**: At least one core query subject concept MUST be matched in the retrieved code candidates. Action verbs alone never establish sufficiency.
  * **Rule A.1**: Secondary graph hops cannot manufacture sufficiency when primary retrieval matched zero query subjects.
  * **Rule E**: If the query requests a distinguishing domain entity (e.g. `FedAvg`, `JWT`), it must be grounded in primary candidates.
* **Why hardcoded rules**: They represent **mathematical invariants**. An invariant should never be dynamic or fuzzy; it guarantees that out-of-domain queries are rejected with **100% precision**.

---

## 11. Context Building & Prompt Engineering

### Q11.1: How is ContextBuilder built? What does it expect and deliver?
* Defined in [`context_builder.cpp`](file:///d:/amoeba/engine/src/context/context_builder.cpp).
* **Input**: An `EvidenceBundle` containing the top $\le 15$ investigated code items and relationship edges.
* **Output**: A `ContextPackage` containing formatted Markdown text, total character count, and estimated token count.

### Q11.2: How does it handle multiple files and fit under the 4,000-character budget?
* **Greedy Knapsack Packing**: It sorts items by relevance score and iterates through them:
  1. Appends the primary symbol declaration and file header.
  2. Appends bounded source excerpt lines.
  3. Appends structural relationship tags (`Calls → ...`).
  4. Checks `used_characters + item_characters <= 4000`.
  5. If an item would exceed 4,000 characters, it truncates using `safe_utf8_prefix()` and stops packing further items.

---

## 12. Chat History & Session Context

### Q12.1: How can we add chat history and make it remember past conversations?
To turn Amoeba from a single-shot Q&A tool into a conversational chat assistant:
1. **Session State Memory**: Add a `ConversationSession` struct holding a rolling window of recent turns:
   ```cpp
   struct ChatTurn {
       std::string user_question;
       std::string assistant_answer;
       std::vector<ElementId> referenced_elements;
   };
   ```
2. **Coreference Query Resolution**:
   When the user asks follow-up questions like *"How is it initialized?"* or *"Where is that called?"*, the query understanding module resolves `"it"` and `"that"` to `referenced_elements` from the previous turn.
3. **Sliding Context Window**:
   In `PromptBuilder`, append the last $N$ turns (e.g., 2–3 turns) as conversational history before the current evidence context, keeping total tokens within the model's budget.
