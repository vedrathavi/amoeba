# Phase 3 — Lexical Indexing & Retrieval Core

## 1. Phase Objective

The objective of **Phase 3** is to provide Amoeba with an efficient, in-memory lexical indexing and retrieval engine that answers:

> *"Given a query, can I efficiently find relevant structural elements in a repository without rescanning and reparsing the entire repository for every search?"*

```text
Repository
    ↓
RepositoryScanner
    ↓
FileInfo[]
    ↓
SourceParser
    ↓
ParsedFile (CodeElement[])
    ↓
CodeTokenizer (Identifier Sub-word Splitting & Normalization)
    ↓
InvertedIndex (Terms ──► PostingList ──► ElementIds)
    ↓
SearchEngine (Query ──► Candidates ──► SearchResult[])
```

---

## 2. Phase 3 Architecture Decomposition

### Phase 3.0 — Searchable Representation & Tokenization
* **Identifier Splitting**: Decomposes code identifiers (`camelCase`, `PascalCase`, `snake_case`, `SCREAMING_SNAKE_CASE`, `kebab-case`, numbers) while preserving the complete original identifier.
* **Normalization**: Case-insensitive lookup dictionary with lowercase normalized terms.
* **Path Tokenization**: Indexes directory components and file stems.

### Phase 3.1 — In-Memory Inverted Index
* **Compact ID Architecture**:
  * `FileId`: Integer identifier for indexed files (`IndexedFile`).
  * `ElementId`: Integer identifier for code elements (`IndexedElement`).
* **Postings Map**: Hash map mapping `Term` (`string`) to unique `vector<ElementId>`.
* **Zero Duplication**: Each element ID is recorded once per term posting list.

### Phase 3.2 — Basic Retrieval & Query Engine
* **Query Tokenization**: Decomposes multi-word queries into search terms.
* **Match Modes**:
  * `MatchMode::AnyTerm` (OR): Matches elements containing any query term.
  * `MatchMode::AllTerms` (AND): Intersects term postings to find elements matching all query terms.
* **Filtering**: Optional `ElementKind` filter (e.g. filter specifically for `Function` or `Class`).
* **Deterministic Ranking**: Exact identifier matches prioritized, followed by match frequency, file path, and line number.

### Phase 3.3 — Validation & Benchmarks
* Comprehensive unit tests across all indexing, tokenization, and search components.
* Hermetic benchmarking measuring indexing throughput, lookup latency, and posting density.

---

## 3. Data Structures & Complexity

| Operation | Component | Data Structure | Time Complexity |
| :--- | :--- | :--- | :--- |
| **Tokenization** | `CodeTokenizer` | Single-pass char scanner | $O(L)$ where $L$ is identifier length |
| **File Indexing** | `InvertedIndex` | `std::unordered_map<string, vector<ElementId>>` | $O(N \cdot T)$ where $N$ is element count, $T$ terms per element |
| **Term Lookup** | `InvertedIndex` | Hash lookup | $O(1)$ average |
| **Multi-Term Search** | `SearchEngine` | Hash lookup + frequency map | $O(Q \cdot P)$ where $Q$ is query terms, $P$ average posting length |

---

## 4. Benchmark Measurements

Benchmarked against a synthetic multi-language repository (20 files, C++, Python, TSX):
* **Files Indexed:** 20 files
* **Symbols Indexed:** 70 elements
* **Distinct Terms:** 45 terms
* **Total Postings:** 320 postings
* **Index Build Time:** ~5.4 ms (including full Tree-sitter multi-language parsing)
* **Query Latency:** ~18–70 microseconds

---

## 5. Strict Phase Boundaries

* ❌ **No Complex Ranking (BM25 / TF-IDF):** Deferred to dedicated ranking phases.
* ❌ **No Embeddings / Vector Search:** Deferred to semantic phases.
* ❌ **No Persistence (SQLite / Disk):** Inverted index operates in-memory.
* ❌ **No Semantic Analysis:** All indexing operates on syntactic AST structures.
