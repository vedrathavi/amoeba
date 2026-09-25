# Phase 5.1 — Graph Storage & Traversal

## 1. Overview & Objective

Phase 5.1 extends Amoeba's relationship graph subsystem by introducing **efficient, composable graph traversal capabilities**.

While Phase 5.0 established the domain model (`RelationshipKind`, `Relationship`) and basic adjacency storage, Phase 5.1 provides the foundational algorithms to query structural code neighborhoods, such as:
- *"What are all functions called directly or transitively by `AuthController` up to depth 3?"*
- *"What modules import `DatabaseClient` (reverse incoming traversal)?"*
- *"What is the inheritance chain of `SearchEngine`?"*

---

## 2. Traversal Domain Model & Configuration

The traversal interface is defined in [`engine/include/amoeba/graph/traversal.hpp`](file:///d:/amoeba/engine/include/amoeba/graph/traversal.hpp):

### 2.1 TraversalDirection
```cpp
enum class TraversalDirection : uint8_t {
    Outgoing = 0, ///< Traverse forward along directed edges (source -> target)
    Incoming      ///< Traverse backward against directed edges (target -> source)
};
```

### 2.2 TraversalOptions
```cpp
struct TraversalOptions {
    TraversalDirection direction{TraversalDirection::Outgoing};
    std::size_t max_depth{1};                                  ///< Depth limit (hops)
    std::optional<RelationshipKind> kind_filter{std::nullopt}; ///< Optional filter by relationship kind
};
```

### 2.3 TraversalStep
```cpp
struct TraversalStep {
    ElementId node_id{0};
    std::size_t depth{0};
};
```

---

## 3. Traversal API ([RelationshipGraph](file:///d:/amoeba/engine/include/amoeba/graph/relationship_graph.hpp))

```cpp
// 1. Immediate 1-hop neighbor queries (unique ElementIds)
std::vector<ElementId> outgoing_neighbors(ElementId source) const;
std::vector<ElementId> outgoing_neighbors(ElementId source, RelationshipKind kind) const;
std::vector<ElementId> incoming_neighbors(ElementId target) const;
std::vector<ElementId> incoming_neighbors(ElementId target, RelationshipKind kind) const;

// 2. Multi-hop depth-bounded BFS traversal (level-order steps with depth metadata)
std::vector<TraversalStep> traverse(ElementId start_node, const TraversalOptions& options = {}) const;

// 3. Reachable node extraction helper
std::vector<ElementId> reachable_nodes(ElementId start_node, const TraversalOptions& options = {}) const;
```

---

## 4. Algorithm & Complexity Analysis

### 4.1 Algorithm Choice: Bounded Breadth-First Search (BFS)
- Traversal uses standard **level-order BFS** with an explicit `std::queue<TraversalStep>` and a `std::unordered_set<ElementId>` tracking visited nodes.
- **Cycle and Self-Loop Safety**: The visited set guarantees that cycles ($A \rightarrow B \rightarrow C \rightarrow A$) and self-referential recursive calls ($A \rightarrow A$) terminate immediately without infinite recursion or duplicate visitations.
- **Depth Bounding**: Nodes at `current_depth >= max_depth` are recorded but their outgoing/incoming edges are not explored.

### 4.2 Computational Complexity
| Metric | Worst-Case Complexity | Description |
| :--- | :---: | :--- |
| **Time Complexity** | $O(V_{\text{sub}} + E_{\text{sub}})$ | Linear in the number of vertices and edges within the traversed subgraph up to `max_depth`. |
| **Space Complexity** | $O(V_{\text{sub}})$ | Auxiliary space for `visited` set and FIFO traversal `queue` proportional to visited vertices. |
| **Neighbor Lookup** | $O(1 + \text{deg}(v))$ | $O(1)$ average-case hash bucket lookup for node $v$, plus $O(\text{deg}(v))$ scan over incident edges. |

> [!NOTE]
> Bounded level-order BFS is the single standard traversal algorithm required for Phase 5 workflows (reachability, dependency trees, and neighborhood extraction). No additional speculative traversal algorithms are introduced.

---

## 5. Verification Suite & Test Coverage

The test suite in [`engine/tests/graph/relationship_graph_test.cpp`](file:///d:/amoeba/engine/tests/graph/relationship_graph_test.cpp) covers 22 dedicated test cases:

1. **Chain Traversal**: Linear sequences ($A \rightarrow B \rightarrow C \rightarrow D$) verified across depths 0, 1, 2, and 10.
2. **Branching Trees**: Root nodes with multi-level hierarchies validated in BFS level order.
3. **Cycle Safety**: Circular dependencies ($1 \rightarrow 2 \rightarrow 3 \rightarrow 1$) terminate with each node visited once.
4. **Self-Loops**: Recursive functions ($1 \rightarrow 1$) handled gracefully.
5. **Disconnected Components**: Traversing node $A$ does not leak into disjoint component $B$.
6. **Reverse / Incoming Traversal**: Caller chains ($C \leftarrow B \leftarrow A$) resolved symmetrically.
7. **Kind Filtering**: Restricting traversal to `CALLS` ignores extraneous `IMPORTS` or `REFERENCES` edges.
8. **Edge Cases**: Empty graphs and non-existent element IDs return empty results without throwing.

---

## 6. Explicit Non-Goals for Phase 5.1

Phase 5.1 intentionally does NOT implement:
- Relationship extraction from source code / ASTs
- Tree-sitter query patterns
- Symbol resolution or cross-file linkers
- Advanced graph algorithms (SCC, PageRank, Dijkstra shortest path)
- Graph visualization or serialization
- Embeddings or vector search

---

## 7. Status Sign-off

- **Build Status**: 0 warnings under `-Wall -Wextra -Wpedantic` in C++20.
- **Formatting**: 100% compliant with `.clang-format`.
- **Test Suite**: **103 / 103 tests passing** (22 graph tests, 81 engine/ranking/parser/scanner tests).
