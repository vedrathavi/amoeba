# Phase 5.5 — Cross-File Relationship Integration

## 1. Overview & Objective

Phase 5.5 introduces **unified repository-level cross-file relationship integration** to Amoeba's graph subsystem.

This phase combines:
1. **Multi-level containment hierarchies** (`CONTAINS`: File $\rightarrow$ Class, Class $\rightarrow$ Method/Field).
2. **File & module dependencies** (`IMPORTS`, `INCLUDES` from Phase 5.2).
3. **Type hierarchies** (`INHERITS_FROM`, `IMPLEMENTS` from Phase 5.3).
4. **Call graph & symbol references** (`CALLS`, `REFERENCES` from Phase 5.4).

This creates a cohesive multi-level graph enabling multi-hop queries across repository boundaries without redundant extraction or edge duplication.

---

## 2. Element Identity Strategy

Amoeba assigns a compact, integer-based [`ElementId`](file:///d:/amoeba/engine/include/amoeba/parser/code_element.hpp) to each indexed code structure:
- **File-Level Scope**: Each element belongs to a distinct `FileId`.
- **Structural Scope**: Enclosing classes, namespaces, and functions are distinguished via `parent_context`.
- **Identity Assumptions & Limitations**:
  - Current `ElementId`s are valid for the in-memory lifetime of the `InvertedIndex`.
  - Re-indexing creates fresh `ElementId` mappings (non-incremental).
  - Cross-file symbol resolution uses layered scoping (same-file, namespace context, imported files, unique global declaration) with conservative ambiguity rejection.

---

## 3. Unified Coordinator Architecture ([RepositoryGraphBuilder](file:///d:/amoeba/engine/include/amoeba/graph/repository_graph_builder.hpp))

```text
                               InvertedIndex
                       (Indexed Files & CodeElements)
                                     │
                                     ▼
                          RepositoryGraphBuilder
       ┌──────────────────┬──────────┴──────────┬──────────────────┐
       ▼                  ▼                     ▼                  ▼
 Structural Hierarchy  Imports & Includes   Type Hierarchy    Call Graph & Refs
 (CONTAINS edges)      (Phase 5.2)          (Phase 5.3)       (Phase 5.4)
       │                  │                     │                  │
       └──────────────────┴──────────┬──────────┴──────────────────┘
                                     │
                                     ▼
                             RelationshipGraph
                          (Unified Directed Graph)
```

---

## 4. Multi-Level Cross-File Traversal Examples

### 1. Service $\rightarrow$ Repository Pipeline (TypeScript)
```text
src/controllers/auth_controller.ts (AuthController.login)
   │
   │ CALLS
   ▼
src/services/auth_service.ts (AuthService.authenticate)
   │
   │ CALLS
   ▼
src/repository/user_repository.ts (UserRepository.findById)
```

### 2. Multi-Level Containment & Inheritance (C++)
```text
BaseRepository (include/base_repository.hpp)
   ▲
   │ INHERITS_FROM
SqlRepository (include/sql_repository.hpp)
   │
   │ CONTAINS
SqlRepository::execute
   │
   │ CALLS
BaseRepository::connect
```

---

## 5. Storage Metrics & Graph-to-Source Ratio

On deterministic multi-language project fixtures:
- **`Relationship` Size**: 16 bytes (trivially copyable).
- **Adjacency Overhead**: $\approx 64\text{ bytes per relationship} + 40\text{ bytes per active node}$.
- **Graph to Source Ratio**: $\approx 1.2\% - 3.8\%$ of raw source byte volume.
- **Cycle Safety**: Depth-bounded BFS traversal gracefully terminates on circular file imports (`FileA` $\leftrightarrow$ `FileB`) with $O(V)$ auxiliary space.

---

## 6. Verification Suite & Test Coverage

The test suite in [`engine/tests/graph/repository_graph_builder_test.cpp`](file:///d:/amoeba/engine/tests/graph/repository_graph_builder_test.cpp) verifies:
1. **Multi-Tier Service-Repository Pipeline**: End-to-end Controller $\rightarrow$ Service $\rightarrow$ Repository cross-file calls.
2. **Cross-File Inheritance & Calls**: Header inclusion, subtyping, and inherited method invocation.
3. **Circular Dependencies Resilience**: Bi-directional module imports without infinite recursion.
4. **Unresolved External Dependencies**: Accurate tracking of unindexed packages (`os`, `sys`, `<vector>`).
5. **Storage Metrics Integrity**: Validating source byte aggregation, memory estimation, and graph ratios.

---

## 7. Status Sign-off & Verdict

- **Total Test Cases**: **133 / 133 passing (100%)**.
- **Compiler Warnings**: **0 warnings** with `-Wall -Wextra -Wpedantic` in C++20.
- **Code Style**: 100% compliant with `.clang-format`.
- **Verdict**: **READY FOR PHASE 5.6**
