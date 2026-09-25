# Phase 5.2 — Imports & Includes Relationships

## 1. Overview & Objective

Phase 5.2 introduces **syntactic import and include dependency extraction** to Amoeba's relationship graph subsystem.

This phase connects the multi-language parser representation to the relationship graph, enabling structural queries on file and module dependencies across the codebase:
- C/C++ `#include` $\longrightarrow$ `RelationshipKind::Includes`
- Python `import` / `from ... import` $\longrightarrow$ `RelationshipKind::Imports`
- Java `import` $\longrightarrow$ `RelationshipKind::Imports`
- Go `import` $\longrightarrow$ `RelationshipKind::Imports`
- Rust `use` / `mod` $\longrightarrow$ `RelationshipKind::Imports`
- JavaScript / TypeScript `import ... from ...` / `require(...)` $\longrightarrow$ `RelationshipKind::Imports`
- HTML / CSS `<link rel="stylesheet">` / `@import` $\longrightarrow$ `RelationshipKind::Includes`

---

## 2. Representation Boundary: Syntax vs Resolved Semantic Dependency

A fundamental architectural tenet of Phase 5.2 is:

> **Do not confuse source syntax with resolved semantic dependency.**

For example, `import React from 'react'` or `#include <vector>` is a valid syntactic import, but `react` and `<vector>` are external dependencies outside the local repository.

### Resolution Principles:
1. **Repository-Internal Resolution**: If an import target matches an indexed file/element within the repository (via relative path resolution, package root matching, or filename stem lookup), Amoeba establishes a directed edge:
   $$\text{Source Include Element} \xrightarrow{\quad\text{Imports / Includes}\quad} \text{Target File Primary Element}$$
2. **Unresolved Dependencies**: If an import targets standard library components or unindexed third-party packages, it is recorded in the extraction metadata as `is_resolved = false` without fabricating synthetic or fake graph nodes.

---

## 3. Extractor Architecture ([ImportExtractor](file:///d:/amoeba/engine/include/amoeba/graph/import_extractor.hpp))

```text
                  InvertedIndex
          (Parsed Files & CodeElements)
                        │
                        ▼
                 ImportExtractor
     ┌──────────────────┴──────────────────┐
     ▼                                     ▼
Internal Matches                      External / Unresolved
(Resolved File Target)                (Stdlib / Third-Party)
     │                                     │
     ▼                                     ▼
RelationshipGraph                     ImportExtractionResult
(Directed Edges Added)                (Metadata Tracking)
```

### Resolution Matching Stages:
1. **Relative Path Resolution**: `./` and `../` paths resolved against the source file's directory with extension inference (`.ts`, `.tsx`, `.js`, `.py`, `.hpp`, `.h`, `.cpp`, `/index.ts`, `/mod.rs`).
2. **Exact / Suffix Path Match**: Full path or root-relative directory suffix (e.g. `amoeba/scanner/repository_scanner.hpp`).
3. **Package Directory Match**: Go package directories (e.g. `pkg/logger` matching `pkg/logger/logger.go`).
4. **Filename & Module Stem Match**: Java packages (`com.example.model.Account` $\rightarrow$ `Account.java`), Python modules (`math_utils` $\rightarrow$ `math_utils.py`), and Rust modules.

---

## 4. Performance, Memory Footprint & Storage Ratio

In benchmarks on multi-language project fixtures:
- **Graph Element Size**: [`Relationship`](file:///d:/amoeba/engine/include/amoeba/graph/relationship.hpp) is $\le 16\text{ bytes}$ (trivially copyable).
- **Storage Ratio**: The relationship graph memory represents a small fraction ($< 2\%$) of the indexed source repository size.
- **Extraction Latency**: $< 0.1\text{ ms}$ for typical project fixtures (linear scan over indexed `Include` elements).

---

## 5. False-Positive & False-Negative Risk Analysis

| Risk Category | Cause | Mitigation in Amoeba |
| :--- | :--- | :--- |
| **False Positive (Ambiguous Stem)** | Two files with identical names in different folders (e.g. `a/utils.py` and `b/utils.py`). | Prioritize relative directory and path matches before falling back to module stem lookup. |
| **False Negative (Dynamic Imports)** | Dynamic imports at runtime (e.g., `import(variable)`). | Syntactic extractors strictly extract static string literals; dynamic runtime loading is intentionally omitted. |
| **Unresolved External** | Third-party dependencies (`react`, `numpy`, `<vector>`). | Accurately recorded as unresolved in `ImportExtractionResult` without generating dangling graph edges. |

---

## 6. Verification Suite & Test Coverage

The test suite in [`engine/tests/graph/import_extractor_test.cpp`](file:///d:/amoeba/engine/tests/graph/import_extractor_test.cpp) covers:
1. C/C++ local header inclusion vs system library include separation.
2. TypeScript relative imports and React external package separation.
3. Python relative and stem module imports vs standard library (`os`, `sys`).
4. Java package import resolution.
5. Go package folder imports.
6. Mixed-language repository fixture with memory metrics.
7. Graceful degradation on malformed or incomplete syntax.

---

## 7. Status Sign-off

- **Compiler Compliance**: **0 compiler warnings** under `-Wall -Wextra -Wpedantic` in C++20.
- **Formatting**: 100% compliant with `.clang-format`.
- **Test Suite**: **110 / 110 tests passing** across all modules.
