# Phase 2.2 — Parser Quality & Hardening

## 1. Phase Objective

The objective of **Phase 2.2** is to verify and harden the parser implementation across all 12 supported languages and formats, ensuring reliable, fault-tolerant, and deterministic structural extraction from realistic source code.

```text
Source Code (12 Languages)
        ↓
Tree-sitter Parse (CST)
        ↓
Modular Language Extractor
        ↓
Quality Hardening Rules:
  * Accurate 1-indexed source locations & byte offsets
  * Deterministic parent context
  * Zero noisy / duplicate Tree-sitter CST nodes
  * Robust syntax-error fault tolerance
        ↓
ParsedFile (CodeElement[])
```

---

## 2. Hardening & Verification Scope

### 2.1 Language-Specific Structural Verifications

| Language | Real-World Constructs Verified |
| :--- | :--- |
| **C** | Functions, structs, header includes, function calls (`printf`, `malloc`) |
| **C++** | Namespaces (`namespace amoeba::core`), template classes (`template<typename T> class Storage`), constructors (`Storage()`), destructors (`~Storage()`), methods, includes, nested calls |
| **Python** | Classes, methods (`__init__`, `self.*`), decorators, imports (`import os`, `from pathlib import Path`), standalone functions, nested calls |
| **Java** | Interfaces (`public interface Indexer`), classes (`implements Indexer`), constructors, overloaded methods, imports, method invocations |
| **Go** | Interfaces (`type Scanner interface`), structs (`type LocalScanner struct`), receiver methods (`func (s *LocalScanner) Scan() error`), standalone functions, imports, calls |
| **Rust** | Traits (`pub trait Engine`), structs, `impl Trait for Struct` blocks, associated methods (`pub fn new(...)`), standalone functions, `use` declarations, calls |
| **JS / TS** | Interfaces, type aliases (`export type Status`), classes, arrow functions, methods, imports, calls |
| **TSX / JSX** | React functional & arrow components (`UserCard`, `ProfileHeader`), hooks (`useState`, `useEffect`), JSX elements (`<div />`), JSX components (`<Button />`), attributes (`onClick`, `className`), Tailwind utility classes |
| **HTML** | Nested elements (`<html><head>...</head><body>...</body></html>`), attributes (`id`, `class`, `href`), Tailwind utility tokens |
| **CSS** | Selectors (`.card`, `#header`), property declarations (`display`, `padding`, `color`), `@import` at-rules |
| **Next.js** | Route conventions (`app/page.tsx`, `app/[id]/page.tsx`, `app/layout.tsx`, `app/loading.tsx`, `app/error.tsx`, `app/api/users/route.ts`, `pages/api/auth.ts`) |

---

## 3. Location, Context, and Noise Control

### 3.1 Source Location & Byte Offset Precision
Every `CodeElement` captures exact 1-indexed line and column positions alongside exact byte offsets:
* `start.line`, `start.column`, `start.byte_offset`
* `end.line`, `end.column`, `end.byte_offset`
* Tested against exact character substring boundaries in multi-line and single-line elements.

### 3.2 Parent Context Consistency
* Methods within classes retain `parent_context = "ClassName"`.
* Methods within namespaces retain `parent_context = "NamespaceName"`.
* Receiver methods in Go retain the receiver type as context.
* Trait implementations in Rust retain `parent_context = "StructName"`.
* Eliminates internal CST node names from context fields.

### 3.3 Noise & Duplicate Control
* Anonymous Tree-sitter tokens (commas, semicolons, brackets) are ignored.
* Only meaningful structural symbols (`Class`, `Struct`, `Interface`, `Function`, `Method`, `Include`, `Call`, `JSXElement`, `JSXComponent`, `Selector`, `Property`, `Attribute`, `UtilityClass`, `Component`, `Hook`, `Route`) are extracted.

### 3.4 Fault Tolerance for Incomplete/Malformed Code
* Tested with truncated C++, broken TSX tags, incomplete Python defs, and unfinished Rust structs.
* In all cases, parser does **not** crash, returns `ParsedFile` with `has_syntax_errors = true` and `success = true`, capturing whatever valid structural elements Tree-sitter was able to recover.

---

## 4. Checkpoint Summary

- [x] All 12 supported languages tested with realistic constructs.
- [x] 100% CTest pass rate (34/34 tests passing).
- [x] Zero compiler warnings under `-Wall -Wextra -Wpedantic`.
- [x] Strict syntactic framework awareness without semantic creep.
