# Phase 2 — Parsing & Code Structure

## 1. Phase Objective

The objective of **Phase 2** is to implement the first code-understanding layer of Amoeba:
* Integrate **Tree-sitter** as our foundational parsing infrastructure.
* Parse source code into concrete syntax trees (CST).
* Traverse the syntax tree to extract basic, language-agnostic structural elements (classes, structs, functions, methods, includes, function calls, and source locations).
* Establish clean engine boundaries without prematurely coupling future engine layers to raw Tree-sitter types.

```text
Source File ──► SourceParser ──► Tree-sitter CST ──► Tree Traversal ──► ParsedFile (CodeElement[])
```

---

## 2. Why Parsing is Necessary

While Phase 1 (Repository Scanner) discovers relevant files on disk, text-only search lacks awareness of code structure. Without parsing:
* Comments, strings, and code constructs cannot be differentiated.
* Function definitions cannot be distinguished from function calls.
* Scope boundaries (classes, structs, namespaces) are invisible.

Parsing transforms raw characters into structured syntactic nodes, enabling structural queries, symbol extraction, and precise code intelligence.

---

## 3. Why Amoeba Does Not Build a Custom Lexer/Parser

Amoeba is an indexing and code-search engine, **not a compiler**. Writing and maintaining production-grade C/C++ lexers and grammars from scratch would require thousands of hours of undifferentiated effort and distract from Amoeba's core mission: code representation, indexing, ranking, and retrieval.

Our guiding engineering philosophy dictates:
> **Build the core, reuse mature infrastructure, measure bottlenecks, and replace components only when there is a good engineering reason.**

---

## 4. Why Tree-sitter Was Selected

Tree-sitter was chosen because it provides:
1. **Battle-Tested Grammars**: High-fidelity grammars maintained for all major programming languages.
2. **Robust Fault-Tolerant Parsing**: Generates meaningful syntax trees even for incomplete, syntactically invalid, or edited code.
3. **Concrete Syntax Tree (CST)**: Preserves exact token ranges, source points (line/column), and byte offsets.
4. **Rich Named Fields & Node Navigation**: Enables clear structural traversal (`name`, `declarator`, `path`, `function`, `body`).
5. **High Performance & Portability**: Pure C99 runtime with minimal overhead and zero external dependencies.

---

## 5. Conceptual Distinctions

It is critical to distinguish the different layers of code analysis:

| Concept | Definition | Amoeba Status |
| :--- | :--- | :--- |
| **Token** | Atomic lexical unit (keyword, identifier, punctuation) produced by a lexer. | Handled by Tree-sitter |
| **Grammar** | Formal rules defining valid language syntax. | Handled by Tree-sitter |
| **Parser** | Builds hierarchical tree structures according to grammar rules. | Handled by Tree-sitter |
| **CST (Concrete Syntax Tree)** | Detailed syntactic tree containing all concrete source artifacts. | Produced by Tree-sitter |
| **AST / CodeElement** | Abstracted structural representation of key constructs. | **Phase 2 Implemented** |
| **Semantic Analysis** | Cross-file symbol resolution, type inferencing, call graphs. | *Future Phases* |

> [!IMPORTANT]
> Tree-sitter provides syntactic concrete trees. It does **not** perform semantic analysis or cross-file type resolution.

---

## 6. Language Scope

| Language | Status | Notes |
| :--- | :--- | :--- |
| **C++** | **Supported** | `tree-sitter-cpp` grammar (`.cpp`, `.hpp`, `.cc`, `.hh`, `.cxx`, `.hxx`) |
| **C** | **Supported** | `tree-sitter-c` grammar (`.c`, `.h`) |
| **Python / JS / TS / Java / Go / Rust** | *Planned (Future Phases)* | Will be enabled as language modules expand |

---

## 7. Amoeba Code Representation

Amoeba encapsulates extracted structural data in clean, decoupled data structures:

### `ElementKind`
```cpp
enum class ElementKind {
    Class,
    Struct,
    Function,
    Method,
    Include,
    Call,
    Unknown
};
```

### `CodeElement`
```cpp
struct CodeElement {
    ElementKind kind;
    string name;
    SourceRange location;     // 1-indexed start/end line, column, byte offset
    string parent_context;    // Enclosing class/struct name if applicable
};
```

### `ParsedFile`
```cpp
struct ParsedFile {
    path file_path;
    string language;
    bool success{false};
    bool has_syntax_errors{false};
    vector<CodeElement> elements;
};
```

---

## 8. Tree Traversal & Extraction Approach

A clean depth-first recursive traversal inspects CST nodes:
* `class_specifier` & `struct_specifier`: Extracts type name and sets `parent_context` for inner declarations.
* `function_definition`: Extracts identifier/qualified name from declarator; categorizes as `Method` if within a class context or containing `::`.
* `declaration` / `field_declaration`: Extracts method prototypes declared inside class definitions.
* `preproc_include`: Extracts included header string literals and system library paths (`"..."`, `<...>`).
* `call_expression`: Extracts function or method call invocations (`authenticate(...)`, `obj.login(...)`).

---

## 9. CLI Usage

Use the `parse` or `inspect` command to analyze any C/C++ source file:

```bash
amoeba parse <file-path>
```

### Example Output
```text
Amoeba
Source Code Search & Indexing Engine

File:
  ./engine/include/amoeba/scanner/repository_scanner.hpp

Language:
  C++

Parsing:
  success

Structural elements:
  Class:
    RepositoryScanner (Line 29)

  Struct:
    ScanResult (Line 17)

  Method:
    total_files_included (in ScanResult) (Line 23)
    RepositoryScanner (in RepositoryScanner) (Line 31)
    scan (in RepositoryScanner) (Line 39)
    is_excluded_directory (in RepositoryScanner) (Line 44)
    is_supported_extension (in RepositoryScanner) (Line 49)
    get_language_name (in RepositoryScanner) (Line 54)

  Include:
    "amoeba/scanner/file_info.hpp" (Line 3)
    <filesystem> (Line 5)
    <string_view> (Line 6)
    <vector> (Line 7)

  Call:
    size (in ScanResult) (Line 23)
```

---

## 10. Known Limitations (Phase 2)

* **Syntactic Scope Only**: No semantic symbol resolution or type deduction across files.
* **Initial Languages**: Restricted to C and C++ grammars.
* **In-Memory Single-Pass**: Syntax trees are parsed and released immediately; no persistent AST cache is stored on disk yet.

---

## 11. Phase 2 Checkpoint Criteria

- [x] Tree-sitter core runtime and C/C++ grammars integrated via CMake `FetchContent`.
- [x] C/C++ source text parsed reliably into syntax trees.
- [x] Tree traversal extracts classes, structs, functions, methods, includes, and calls.
- [x] Exact 1-indexed line and column source locations captured.
- [x] Incomplete/malformed source code handled gracefully without crashes (`has_syntax_errors = true`).
- [x] Dedicated GoogleTest suite added (12 unit tests for parser).
- [x] All Phase 0 and Phase 1 tests continue to pass (22/22 total tests pass).
- [x] CLI `parse` command demonstrates structural extraction.
- [x] Code formatting (`clang-format`) and static analysis (`clang-tidy`) pass.
