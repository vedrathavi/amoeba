# Phase 5.6 — Incremental Graph Update Foundation

## 1. Overview & Objective

Phase 5.6 establishes the **foundation for incremental single-file graph updates** in Amoeba.

The architectural objective is ensuring that changing or deleting a single file does not require discarding and manually rebuilding the repository graph from scratch, while guaranteeing transactional safety and zero stale edges.

---

## 2. Core Mutation Model & Transactional Safety

```text
                               File Mutation (replaceFile)
                                           │
                                           ▼
                                  Parse New Source
                                           │
                        ┌──────────────────┴──────────────────┐
                        │ Success                             │ Error / Malformed
                        ▼                                     ▼
             1. Remove old elements & edges          Preserve Previous Valid State
             2. Insert new elements into index       (Index & Graph untouched)
             3. Re-extract file & cross-file edges
             4. Update RelationshipGraph
```

### Safety Guarantees:
1. **Transactional Rollback**: If parsing fails (e.g. unsupported language or unhandled parser error), the updater preserves the existing valid graph without partial corruption.
2. **Stale Edge Eradication**: Removing an element or file automatically cleans up all associated outgoing and incoming directed relationships via [`RelationshipGraph::remove_relationships_for_element`](file:///d:/amoeba/engine/include/amoeba/graph/relationship_graph.hpp).
3. **Decoupled from Git**: Changes are driven via standard path strings, content buffers, and AST models.

---

## 3. Incremental Update Architecture ([IncrementalGraphUpdater](file:///d:/amoeba/engine/include/amoeba/graph/incremental_graph_updater.hpp))

### Mutation Operations:
- `add_file(path, source)`: Parses and indexes a newly created file.
- `replace_file(path, new_source)`: Atomically updates an existing file, removing old elements and re-linking cross-file relationships.
- `remove_file(path)`: Eradicates a deleted file's elements and all attached relationship edges.
- `has_file(path)` & `tracked_files()`: Querying tracked repository state.

---

## 4. Renames, Moves & Dependency Invalidation Analysis

| Event | Current Phase 5.6 Behavior | Future Incremental Work Required |
| :--- | :--- | :--- |
| **File Rename / Move** | Treated as `remove_file(old_path)` + `add_file(new_path)`. | Persistent symbol identifiers to avoid re-linking unchanged call edges. |
| **Function Signature Change** | File replaced; caller edges across other files re-evaluated. | Fine-grained dirty dependency propagation to only re-resolve direct callers. |
| **Class Renamed** | Inheritance and class-method edges in the file re-extracted. | Cascade invalidation to subclasses in dependent files. |
| **Import Target Removed** | Target elements deleted; dependent calls become `Unresolved` on next pass. | Direct notification to inbound import neighbors. |

---

## 5. Verification Suite & Test Coverage

The test suite in [`engine/tests/graph/incremental_graph_updater_test.cpp`](file:///d:/amoeba/engine/tests/graph/incremental_graph_updater_test.cpp) verifies:
1. **Incremental Add File**: Incrementally adding a file correctly populates elements and call edges.
2. **Incremental Modify File**: Modifying a function call updates the graph and cleans up old symbol index entries (`helperA` $\rightarrow$ `helperB`).
3. **Incremental Remove File**: Deleting a file eradicates all attached relationships without stale edge residue.
4. **Cross-File References Update**: Modifying a caller file properly removes obsolete calls to imported headers.
5. **Unchanged File No-Op**: Identical content produces an immediate no-op result.
6. **State Preservation on Parse Failure**: Invalid or unsupported files leave the existing index and graph intact.

---

## 6. Status Sign-off & Verdict

- **Total Test Cases**: **139 / 139 passing (100%)**.
- **Compiler Warnings**: **0 warnings** with `-Wall -Wextra -Wpedantic` in C++20.
- **Code Style**: 100% formatted via `.clang-format`.
- **Verdict**: **READY FOR PHASE 5.7**
