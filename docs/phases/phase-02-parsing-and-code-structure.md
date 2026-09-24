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

## 6. Supported Languages & Formats

| Language / Format | Extensions | Structural Extractions |
| :--- | :--- | :--- |
| **C** | `.c`, `.h` | Structs, functions, header includes, function calls |
| **C++** | `.cpp`, `.hpp`, `.cc`, `.hh`, `.cxx`, `.hxx` | Classes, structs, functions, methods, includes, calls |
| **Python** | `.py` | Classes, functions, methods, imports (`import`, `from ... import`), calls |
| **Java** | `.java` | Classes, interfaces, methods, imports, method invocations |
| **Go** | `.go` | Structs, interfaces, functions, receiver methods, imports, calls |
| **Rust** | `.rs` | Structs, traits/interfaces, impl methods, functions, `use` declarations, calls |
| **JavaScript** | `.js`, `.jsx` | Classes, functions, arrow components, methods, imports (`import`/`require`), calls, JSX elements, JSX components, attributes, Tailwind utility classes |
| **TypeScript** | `.ts`, `.tsx` | Classes, interfaces, type aliases, functions, arrow components, methods, imports, calls, JSX elements, JSX components, attributes, Tailwind utility classes |
| **HTML** | `.html`, `.htm` | HTML tags/elements, attributes (`id`, `class`, `href`, `src`), Tailwind utility class tokens |
| **CSS** | `.css` | Selectors (`.class`, `#id`, tag), declarations/properties (`display`, `color`, `padding`), `@import` / at-rules |

---

## 7. Web Ecosystem Structural Awareness

| Ecosystem | Current Structural Support | Boundaries / Limitations |
| :--- | :--- | :--- |
| **React** | Component-like declarations (`Function`, `Arrow Component`), JSX components (`<Button />`), JSX elements (`<div />`), props/attributes (`onClick`, `className`), hooks (`useState`, `useEffect`) | Syntactic extraction only; no component lifecycle or cross-file prop-type verification |
| **Next.js** | Route convention awareness based on file paths (`app/page.tsx`, `app/[id]/page.tsx`, `app/layout.tsx`, `pages/api/*.ts`) emitting `ElementKind::Route` metadata (`Next.js Page`, `Next.js Dynamic Page`, `Next.js Layout`, `Next.js API Route`) | File and directory convention detection only; no routing graph execution or SSR resolution |
| **Tailwind CSS** | Tokenizes space-separated utility classes from `className` and `class` attributes into distinct `ElementKind::UtilityClass` items (`flex`, `items-center`, `justify-between`, `px-4`, `py-2`) | Syntactic token extraction only; does NOT execute Tailwind compiler or resolve arbitrary CSS themes |

---

## 8. Amoeba Code Representation

Amoeba encapsulates extracted structural data in clean, decoupled data structures:

### `ElementKind`
```cpp
enum class ElementKind {
    Class,
    Struct,
    Interface,
    Function,
    Method,
    Include,
    Call,
    JSXElement,
    JSXComponent,
    Selector,
    Property,
    Attribute,
    UtilityClass,
    Component,
    Hook,
    Route,
    Unknown,
};
```

### `CodeElement`
```cpp
struct CodeElement {
    ElementKind kind{ElementKind::Unknown};
    string name;
    SourceRange location;     // 1-indexed start/end line, column, byte offset
    string parent_context;    // Enclosing class, struct, component, or rule
    string detail;            // Optional lightweight detail (e.g. prop value, route type)
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

## 9. Tree Traversal & Architecture

The parser architecture is organized around an extensible, composition-based registry:
```text
Source File / Path
        ↓
Language Identification (detect_language)
        ↓
Language Registry Lookup
        ↓
Tree-sitter Grammar Execution
        ↓
Concrete Syntax Tree (CST)
        ↓
Language-Specific Structural Extractor
        ↓
ParsedFile (CodeElement[])
```

Tree-sitter handles (`TSParser*`, `TSTree*`, `TSNode`) remain strictly internal to the engine implementation (PIMPL pattern) and never leak into Amoeba's public APIs.

---

## 10. Known Limitations & Phase Boundary

* **Syntactic Scope Only**: No cross-file semantic symbol resolution, type inferencing, or call-graph resolution.
* **In-Memory Single-Pass**: Syntax trees are parsed and converted immediately into `ParsedFile`; no persistent index is written yet.
* **Phase Boundaries Respected**: Indexing (inverted/trigram index), search, ranking, embeddings, vector search, persistence, and client/server APIs remain strictly deferred to subsequent phases.

---

## 11. Checkpoint Criteria

- [x] Tree-sitter core runtime and 10 language grammars integrated via CMake `FetchContent`.
- [x] Support for C, C++, Python, Java, Go, Rust, JavaScript, TypeScript, JSX, TSX, HTML, and CSS.
- [x] Lightweight React component, hook, and JSX tag extraction.
- [x] Next.js file-convention route classification (`Route` elements).
- [x] Tailwind CSS utility-class extraction from `className` and `class` attributes.
- [x] Exact 1-indexed line and column source locations captured.
- [x] Incomplete/malformed source code handled gracefully without crashes (`has_syntax_errors = true`).
- [x] Comprehensive GoogleTest suite passing (31/31 total tests pass).
- [x] CLI `parse` command demonstrates multi-language and web ecosystem inspection.
- [x] Code formatting (`clang-format`) and static analysis passing with zero warnings.
