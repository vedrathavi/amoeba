# Phase 2.3 — Code Representation & Parser API Stabilization

## 1. Phase Objective

The objective of **Phase 2.3** is to stabilize and freeze the boundary between the parser and the future indexer:
* Define and freeze the `CodeElement` data model.
* Encapsulate Tree-sitter completely behind standard-library C++ types.
* Ensure dependency direction flows strictly from consumers to Amoeba abstractions.

```text
RepositoryScanner
       ↓
    FileInfo
       ↓
  SourceParser (Public API)
       ↓
   ParsedFile
       ↓
  CodeElement[]
       ↓
 [Phase 3 Indexer]
```

---

## 2. Core Code Representation

### 2.1 `CodeElement` Model
```cpp
struct CodeElement {
    ElementKind kind{ElementKind::Unknown};
    string name;
    SourceRange location;
    string parent_context;
    string detail;

    [[nodiscard]] bool operator==(const CodeElement&) const = default;
};
```

| Field | Purpose & Semantics |
| :--- | :--- |
| `kind` | Categorization enum (`Class`, `Function`, `Method`, `Include`, `Call`, `JSXElement`, `Attribute`, `Selector`, `Property`, `UtilityClass`, `Component`, `Hook`, `Route`) |
| `name` | The primary identifier name extracted from the AST (e.g. `"User"`, `"login"`, `"useState"`, `".card"`) |
| `location` | 1-indexed start/end line, column, and byte offset in source code |
| `parent_context` | Syntactic enclosing scope (e.g. `"User"`, `"amoeba::core"`, `"className"`) |
| `detail` | Secondary lightweight metadata string (e.g. `"React Component"`, `"Next.js Page"`, `"display"`) |

### 2.2 `ParsedFile` Model
```cpp
struct ParsedFile {
    path file_path;
    string language;
    bool success{false};
    bool has_syntax_errors{false};
    vector<CodeElement> elements;

    [[nodiscard]] vector<CodeElement> get_elements_by_kind(ElementKind kind) const;
};
```
* **Single Responsibility:** Represents the isolated syntactic parse result of a single source file.
* **No Premature Indexing:** Contains zero inverted index tokens, term frequencies, BM25 scores, vector IDs, or persistence logic.

---

## 3. Tree-sitter Encapsulation & API Decoupling

* **Zero Leakage:** Public headers in `engine/include/amoeba/parser/` contain **zero** references to `tree_sitter/api.h`, `TSParser`, `TSTree`, or `TSNode`.
* **PIMPL Pattern:** `SourceParser` manages the underlying `TSParser*` via an internal `unique_ptr<Impl>`, handling memory lifecycle safely through RAII.
* **Clean Extensibility:** Adding a new language requires only registering a grammar getter and an AST extraction function without modifying public interfaces.

---

## 4. Phase Boundary & Indexing Preparation

In Phase 3, the Indexer will consume `ParsedFile` and `CodeElement[]` from `SourceParser`. The structural representation cleanly supports:
* File-level indexing (`ParsedFile.file_path`, `ParsedFile.language`).
* Symbol-level indexing (`CodeElement.name`, `CodeElement.kind`, `CodeElement.location`).
* Context-aware retrieval (`CodeElement.parent_context`, `CodeElement.detail`).

---

## 5. Checkpoint Summary

- [x] `CodeElement` and `ParsedFile` frozen and validated for Phase 3 consumption.
- [x] Tree-sitter fully encapsulated behind PIMPL boundary.
- [x] All 34 tests passing with zero compiler warnings.
- [x] Zero Phase 3 components implemented.
