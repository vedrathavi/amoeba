# Amoeba

> **High-Performance Source Code Search, Indexing & Grounded Reasoning Engine**

---

## Current Status

> [!NOTE]
> **Amoeba is in Phase 9.1 (Broad Candidate Reservoir + Deterministic Seed Pruning).**
> Amoeba provides end-to-end repository indexing, Tree-sitter syntax parsing across 12+ languages, inverted index lexical search (BM25), HNSW dense vector semantic search, hybrid rank fusion ($\alpha=0.8$), Top-50 candidate reservoir generation with multi-signal deterministic seed pruning (`BoundedInvestigationService`), bounded 1-hop graph investigation, evidence sufficiency verification gates with 100% negative refusal accuracy, and offline local LLM code reasoning (via Ollama & Qwen2.5-Coder).
> Full evaluation report: [`docs/phase-09.1-top50-reservoir-investigation.md`](docs/phase-09.1-top50-reservoir-investigation.md).

---

## Project Vision

Amoeba is an open, high-performance source-code search and indexing engine engineered in modern C++20 to scale across large codebases. The project evolves through systematic milestones:

$$\text{Source Scanning} \longrightarrow \text{AST Parsing} \longrightarrow \text{Hybrid Retrieval} \longrightarrow \text{Bounded Investigation} \longrightarrow \text{Evidence Verification} \longrightarrow \text{Grounded Code Intelligence}$$

---

## Core Capabilities

- **Multi-Language AST Parsing**: Full Tree-sitter CST/AST extraction for C, C++, Python, Java, Go, Rust, JavaScript, TypeScript, JSX, TSX, HTML, and CSS.
- **Inverted Index & Lexical Search**: Term frequency, inverse document frequency (TF-IDF/BM25), field-aware symbol weighting, and deterministic ranking.
- **Dense Vector Semantic Index**: Approximate Nearest Neighbor (ANN) vector indexing powered by HNSW graphs (`hnswlib`) with deterministic local embeddings.
- **Calibrated Hybrid Fusion**: Score fusion combining lexical and semantic rankings with tie-breaking determinism.
- **Bounded 1-Hop Investigation**: Deterministic graph and AST vocabulary exploration linking callers, callees, and types in $< 2\text{ ms}$.
- **Evidence Sufficiency Gate**: Algorithmic sufficiency validation enforcing domain term coverage and negative refusal to prevent LLM hallucinations.
- **Offline Grounded Code Reasoning**: Direct natural language question-answering with local LLM integration (Ollama / Qwen2.5-Coder) bounded by verified source context slices.

---

## Engineering Philosophy

> **Build the core, reuse mature infrastructure, measure bottlenecks, and replace components only when there is a good engineering reason.**

* **Build the core**: Implement indexing, retrieval, ranking, and code representation internally to deeply understand and optimize core algorithms.
* **Reuse mature tools**: Leverage established ecosystems (Tree-sitter for AST/CST parsing, hnswlib for ANN vector search, and Ollama for local LLM inference).
* **Measure and iterate**: Ground all optimizations in empirical benchmarks (RepoProbe v1 suite across 10 repositories and 70 real-world benchmark tasks).
* **Zero Hallucination Safety**: Maintain strict invariants guaranteeing grounded refusal when evidence is insufficient or concepts are absent.

---

## Technology Stack

* **Language**: C++20 (ISO/IEC 14882:2020)
* **Build System**: CMake 3.20+ with Ninja
* **Parsing Runtime**: Tree-sitter (C, C++, Python, Java, Go, Rust, JS, TS, HTML, CSS grammars)
* **Vector Index**: HNSW (Hierarchical Navigable Small World graphs via hnswlib)
* **Local LLM Runtime**: Ollama REST Client (`qwen2.5-coder:1.5b` / `qwen2.5-coder:7b`)
* **Testing Framework**: GoogleTest (518 automated unit & integration tests)
* **Code Quality**: clang-format & clang-tidy

---

## Repository Structure

```text
amoeba/
│
├── engine/             # Core C++ engine library
│   ├── include/        # Public C++ API headers
│   │   └── amoeba/
│   │       ├── scanner/     # Repository scanner and file discovery
│   │       ├── parser/      # Tree-sitter multi-language AST parser
│   │       ├── index/       # Inverted index and lexical BM25
│   │       ├── semantic/    # HNSW vector index and embeddings
│   │       ├── retrieval/   # Hybrid fusion and query understanding
│   │       ├── evidence/    # Evidence assembly and sufficiency checking
│   │       └── reasoning/   # Local LLM client and reasoning service
│   ├── src/            # Core engine implementation
│   └── tests/          # Unit & integration test suites (GoogleTest)
│
├── apps/               # Application entry points
│   └── cli/            # Amoeba CLI executable (`amoeba_cli`)
│
├── benchmarks/         # Benchmark fixtures and evaluation harnesses
│   └── repoprobe_v1/   # Canonical 70-question benchmark across 10 repos
│
├── docs/               # System documentation, forensic reports, and ADRs
│   ├── phase-09.0-bounded-investigation.md
│   ├── phase-09.1-top50-reservoir-investigation.md
│   ├── phase-08.6-query-subject-normalization.md
│   └── phase-08.7-post-8.6-failure-forensic-checkpoint.md
│
├── CMakeLists.txt      # Root CMake build specification
├── CMakePresets.json   # Standardized build and test presets
└── README.md           # Project overview and documentation
```

---

## Getting Started

### Prerequisites

* A C++20 compliant compiler (GCC 11+, Clang 13+, or MSVC 2019/2022)
* CMake 3.20 or newer
* Ninja build system
* (Optional for LLM reasoning) [Ollama](https://ollama.ai/) running locally with `qwen2.5-coder:1.5b` (`ollama run qwen2.5-coder:1.5b`)

### Build Instructions

```bash
# Configure build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Compile all targets (amoeba_engine, amoeba_cli, amoeba_engine_tests)
cmake --build build
```

---

## CLI Usage

### 1. Grounded Question Answering (`ask` command)

Ask natural language architectural or implementation questions about any repository:

```bash
./build/amoeba_cli ask <repository-path> "<your-question>"
```

**Example:**
```bash
./build/amoeba_cli ask ./ "Where is HNSW implemented?"
```

**Output:**
```text
Amoeba
Source Code Search & Indexing Engine

Repository:
  d:/amoeba

Question:
  "Where is HNSW implemented?"

Grounding Context:
  Retrieved 2 primary code units (2784 chars) in 14 ms
  Local LLM Model: qwen2.5-coder:1.5b (http://127.0.0.1:11434)

Answer:
HNSW is implemented in the `HnswSemanticIndex` class, which is defined in the `engine/include/amoeba/semantic/hnsw_semantic_index.hpp` file.
```

---

### 2. Multi-Modal Code Search (`search` command)

Perform lexical, semantic, or hybrid search across an indexed repository:

```bash
# Hybrid Search (Default, alpha=0.8)
./build/amoeba_cli search <repository-path> "<query>" --mode hybrid

# Lexical Search (BM25)
./build/amoeba_cli search <repository-path> "<query>" --mode lexical

# Semantic Search (HNSW Vector Index)
./build/amoeba_cli search <repository-path> "<query>" --mode semantic
```

---

### 3. Repository Discovery & Indexing (`index` command)

Scan and build structural indexes for a repository:

```bash
./build/amoeba_cli index <repository-path>
```

---

### 4. AST Syntax Parsing (`parse` command)

Extract structural code symbols (classes, functions, interfaces, methods) from an individual source file:

```bash
./build/amoeba_cli parse <file-path>
```

---

## Running Test Suites

Run the complete 518-assertion GoogleTest suite:

```bash
./build/amoeba_engine_tests
```

Or via CTest:
```bash
ctest --test-dir build --output-on-failure
```

---

## Evaluation & Benchmarks

Amoeba includes the **RepoProbe v1** benchmark evaluation suite testing retrieval recall (Hit@1, Hit@5, Hit@10, MRR), sufficiency verification, and grounded answer correctness across 10 real-world repositories (including `duckdb/ducklake`, `apple/pkl`, `google/adk-python`, `BurntSushi/jiff`, `Dokploy/dokploy`, and `docling-project/docling`).

To run the full evaluation suite:
```bash
python benchmarks/repoprobe_v1/scripts/run_benchmark.py
```

---

## License

The project license selection is currently pending.
