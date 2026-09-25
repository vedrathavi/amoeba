# Phase 5.7 — Relationship Queries & Focused Subgraphs

## 1. Overview & Objective

Phase 5.7 creates the **relationship query and focused subgraph foundation** in Amoeba.

The architectural objective is to expose contextual slices of the repository graph (e.g. surrounding a file, class, function, or search result) rather than dumping unmanageable global graphs, while avoiding frontend rendering or visualization dependencies.

---

## 2. Full Graph vs Focused Graph

| View Mode | Scope & Intent | Typical Consumption |
| :--- | :--- | :--- |
| **Full Repository Graph** | All known files, symbols, and directed relationships. | Global index verification, dependency analytics, repo metrics. |
| **Focused Subgraph** | Contextual neighborhood around a focal element within bounded depth. | Contextual code search, IDE side panels, code intelligence. |
| **Explanation Subgraph** | Targeted slices answering specific architectural questions (e.g. "What does `AuthService` call?"). | Search result ranking signals, code explanations, refactoring impact. |

---

## 3. Query Engine Architecture ([GraphQueryService](file:///d:/amoeba/engine/include/amoeba/graph/graph_query_service.hpp))

```text
                           RelationshipGraph
                                   │
                                   ▼
                           GraphQueryService
       ┌───────────────────────────┴───────────────────────────┐
       ▼                                                       ▼
Composable Queries                                    Focused Subgraphs
- callers / callees                                  - focused_subgraph(root, depth)
- parents / children                                 - neighborhood_subgraph(root, depth)
- base_types / derived_types                         - call_graph_subgraph(root, depth)
- dependencies / dependents                          - type_hierarchy_subgraph(root, depth)
                                                     - induced_subgraph(element_ids)
```

### Key Query APIs:
- `callers(element_id)`: Direct callers invoking a function or method.
- `callees(element_id)`: Direct functions/methods invoked by an element.
- `dependencies(element_id, depth)`: Transitive dependencies along outgoing edges.
- `dependents(element_id, depth)`: Inbound elements depending on this element.
- `parents(element_id)` & `children(element_id)`: Containment structural hierarchy.
- `base_types(element_id)` & `derived_types(element_id)`: Inheritance and interface implementation hierarchies.
- `focused_subgraph(root, options)`: Returns a [`FocusedSubgraph`](file:///d:/amoeba/engine/include/amoeba/graph/focused_subgraph.hpp) slice with all participating nodes and connecting edges.

---

## 4. Subgraph Representation ([FocusedSubgraph](file:///d:/amoeba/engine/include/amoeba/graph/focused_subgraph.hpp))

```cpp
struct FocusedSubgraph {
    ElementId root_node{0};
    std::vector<ElementId> nodes;    // Sorted, unique element IDs
    std::vector<Relationship> edges; // Directed relationship records

    std::size_t node_count() const;
    std::size_t edge_count() const;
    std::vector<Relationship> outgoing_edges(ElementId source) const;
    std::vector<Relationship> incoming_edges(ElementId target) const;
    std::vector<Relationship> edges_by_kind(RelationshipKind kind) const;
};
```

---

## 5. Verification Suite & Test Coverage

The test suite in [`engine/tests/graph/graph_query_service_test.cpp`](file:///d:/amoeba/engine/tests/graph/graph_query_service_test.cpp) verifies:
1. **Chain Graph Depth-Limited Traversal**: Verifying precise depth 1, 2, and 3 slices on linear call chains.
2. **Branching Graph Neighborhood**: Extracting combined callers and callees in a 1-hop neighborhood.
3. **Cycle Graph Termination**: Safe BFS termination on circular graphs ($1 \rightarrow 2 \rightarrow 3 \rightarrow 1$) with all 3 edges captured.
4. **Relationship Kind Filters**: Selective subgraphs filtering for `Calls`, `Contains`, or `InheritsFrom`.
5. **Type Hierarchy Subgraphs**: Multi-hop base type and derived type hierarchies.
6. **Realistic Cross-File Queries**: Neighborhood and call subgraphs across multi-file TypeScript repositories.

---

## 6. Status Sign-off & Verdict

- **Total Test Cases**: **145 / 145 passing (100%)**.
- **Compiler Warnings**: **0 warnings** with `-Wall -Wextra -Wpedantic` in C++20.
- **Code Style**: 100% compliant with `.clang-format`.
- **Verdict**: **READY FOR PHASE 5.8**
