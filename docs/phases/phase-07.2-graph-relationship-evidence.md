# Amoeba — Phase 7.2: Graph Relationship Evidence Resolution

## 1. Goal

Following the completion of **Phase 7.0 (Evidence Architecture Investigation)** and **Phase 7.1 (Source Snippet Extraction)**, Phase 7.2 implements the relationship projection bridge: resolving direct structural and graph relationships connecting retrieved primary retrieval units to other nodes in the relationship graph.

Phase 7.2 transforms:
```
Primary Retrieval Unit / Primary Element ID
                    +
            RelationshipGraph
                    ↓
        Direct Relationship Evidence
```

This checkpoint is strictly a graph projection component. It does NOT build a new graph, alter graph storage or construction, change retrieval or ranking, expand relationships recursively, or assemble evidence bundles.

---

## 2. Existing Graph Architecture Reused

Phase 7.2 reuses existing Amoeba graph infrastructure without modification:

- `RelationshipGraph`: Adjacency structure storing nodes (`ElementId`) and directed edges (`Relationship`).
- `Relationship`: Contains `source_id`, `target_id`, and `RelationshipKind`.
- `RelationshipKind`: `Contains`, `Imports`, `Includes`, `Calls`, `References`, `InheritsFrom`, `Implements`.
- `RelationshipGraph::outgoing_relationships(ElementId)`: Retrieves outgoing edges ($O(1)$ lookup via unordered adjacency map).
- `RelationshipGraph::incoming_relationships(ElementId)`: Retrieves incoming edges ($O(1)$ lookup via reverse adjacency map).
- `InvertedIndex::get_element(ElementId)`: Resolves element metadata (`name`, `kind`, `file_path`, `range`).

No changes were made to graph construction, edge extraction, edge generation, or node indexing.

---

## 3. RelationshipEvidence Design

`RelationshipEvidence` is a model-independent value struct declared in `engine/include/amoeba/graph/relationship_evidence.hpp`:

```cpp
enum class RelationshipDirection {
    Outgoing,  // Source -> Target (e.g., A calls B, A inherits from B)
    Incoming   // Target -> Source (e.g., B called by A, B inherited by A)
};

struct RelationshipEvidence {
    RelationshipDirection direction;
    RelationshipKind kind;

    // Source element ID (the primary symbol being resolved)
    uint64_t source_element_id{0};

    // Target element identity and metadata
    uint64_t target_element_id{0};
    std::string target_name;
    ElementKind target_kind{ElementKind::Unknown};
    std::string target_file_path;
    SourceRange target_range;

    bool target_resolved{false};
};
```

Key characteristics:
- Encapsulates relationship kind and direction explicitly (`Outgoing` vs `Incoming`).
- Exposes target metadata cleanly without leaking graph pointers, adjacency tables, ranking scores, or embeddings.
- Gracefully indicates whether the target node was resolved in the index via `target_resolved`.

---

## 4. RelationshipEvidenceResolver Responsibility

`RelationshipEvidenceResolver` is a concrete class declared in `engine/include/amoeba/graph/relationship_evidence_resolver.hpp` and implemented in `engine/src/graph/relationship_evidence_resolver.cpp`.

Its responsibilities are:
1. Accept a primary symbol identifier (`ElementId`), `RetrievalUnit`, or `PrimarySearchResult`.
2. Inspect direct outgoing and incoming relationships from `RelationshipGraph`.
3. Resolve target node metadata via `InvertedIndex`.
4. Apply deterministic ordering.
5. Enforce bounded fanout limits per relationship kind and direction.
6. Return a deterministic list of `RelationshipEvidence` items.

It does NOT perform any inference, heuristic edge generation, ranking adjustments, or deep traversal.

---

## 5. ID Mapping

As validated during Phase 7.0 and Phase 7.2 inspection:
- `RetrievalUnit.primary_element_id` is reconciled to `InvertedIndex::ElementId` during `PrimaryRetrievalPipeline` execution.
- `RelationshipGraph` nodes are indexed directly by `InvertedIndex::ElementId`.
- Therefore:
  $$\text{RetrievalUnit.primary\_element\_id} \equiv \text{InvertedIndex::ElementId} \equiv \text{RelationshipGraph Node ElementId}$$

No ID translation, mapping tables, or synthetic node generation are required.

---

## 6. Relationship Direction

Relationship direction is strictly preserved:
- **Outgoing relationships** ($A \to B$):
  - `A Calls B` $\to$ `Outgoing Calls` (Callee)
  - `A Imports B` $\to$ `Outgoing Imports`
  - `A InheritsFrom B` $\to$ `Outgoing InheritsFrom` (Base class)
  - `A Implements B` $\to$ `Outgoing Implements` (Interface)
  - `A References B` $\to$ `Outgoing References`
  - `A Includes B` $\to$ `Outgoing Includes`
  - `A Contains B` $\to$ `Outgoing Contains`
- **Incoming relationships** ($B \to A$):
  - `B Calls A` $\to$ `Incoming Calls` (Caller)
  - `B InheritsFrom A` $\to$ `Incoming InheritsFrom` (Derived class)
  - `B References A` $\to$ `Incoming References`
  - `B Contains A` $\to$ `Incoming Contains` (Enclosing scope)

Direction is never collapsed into undirected edges.

---

## 7. Supported Relationship Kinds

All 7 primitive relationship kinds in Amoeba are supported without modification:
1. `Contains`
2. `Imports`
3. `Includes`
4. `Calls`
5. `References`
6. `InheritsFrom`
7. `Implements`

No speculative kinds or pattern-level abstractions (e.g. Factory, Strategy, MVC) are introduced.

---

## 8. One-Hop Limitation

Resolution is strictly direct (1-hop):
- For a primary symbol $A$, only immediate neighbors $N(A)$ directly connected by existing edges are evaluated.
- No recursive expansion ($A \to B \to C$).
- No neighborhood graph expansion or depth-$N$ traversal.
- Bounded and localized.

---

## 9. Fanout Limit

To prevent context explosion from high-degree hub nodes (such as standard library headers, base classes, or common utility functions), the resolver applies configurable fanout caps:

- `max_outgoing_per_kind` (default: 5)
- `max_incoming_per_kind` (default: 5)

Evidence items are grouped by `(direction, kind)` and capped after deterministic sorting.

---

## 10. Deterministic Ordering

To ensure reproducible context assembly regardless of internal hash map bucket ordering, all candidate relationship evidence items are sorted prior to fanout truncation according to a stable multi-level comparator:

1. **Direction**: `Outgoing` before `Incoming`
2. **RelationshipKind**: Integer enum order (`Contains` $\to$ `Imports` $\to$ `Includes` $\to$ `Calls` $\to$ `References` $\to$ `InheritsFrom` $\to$ `Implements`)
3. **Target File Path**: Lexicographical ascending
4. **Target Start Line**: Numerical ascending
5. **Target Start Column**: Numerical ascending
6. **Target Name**: Lexicographical ascending
7. **Target Element ID**: Numerical ascending (deterministic tie-breaker)

---

## 11. Cycle Handling

Cycles (e.g., mutual recursion $A \text{ calls } B \text{ and } B \text{ calls } A$, or circular imports) are handled naturally because traversal is strictly 1-hop direct edge lookup. There is no recursion stack, no depth expansion, and no visited-set backtracking needed.

---

## 12. Target Resolution

When resolving relationship targets:
- If target `ElementId` exists in `InvertedIndex`, the target's `name`, `kind`, `file_path`, and `range` are extracted directly.
- If target `ElementId` is unresolved (e.g., an external symbol or missing metadata), `target_resolved` is set to `false`, `target_element_id` is preserved, and default fields are retained.
- No source files are reparsed, and no synthetic nodes are fabricated.

---

## 13. Tests

A comprehensive unit test suite was added in `engine/tests/graph/relationship_evidence_resolver_test.cpp` covering:

1. `OutgoingCalls`: Verifies outgoing callee discovery and metadata resolution.
2. `IncomingCalls`: Verifies incoming caller discovery (who calls this function).
3. `BothCallersAndCallees`: Verifies simultaneous incoming callers and outgoing callees.
4. `Imports`: Verifies module and namespace imports.
5. `Includes`: Verifies header inclusion relationships.
6. `Inheritance`: Verifies class inheritance (`Base` $\to$ `Derived`).
7. `Implements`: Verifies interface implementation.
8. `References`: Verifies symbol reference relationships.
9. `Contains`: Verifies parent-child scope containment.
10. `MultipleRelationshipKinds`: Verifies mixed outgoing/incoming relationships across multiple kinds.
11. `NoRelationships`: Verifies safe empty vector return on disconnected symbols.
12. `MissingOrUnresolvableTarget`: Verifies unresolvable target handling without crashing or fabricating ASTs.
13. `HighDegreeNodeFanoutLimit`: Verifies that high fanout (e.g., 10 callees) is capped cleanly at configured limit (e.g., 5).
14. `DeterministicOrdering`: Verifies stable lexicographical and spatial sorting across multiple runs.
15. `CyclicRelationships`: Verifies mutual recursive calls resolve safely with 1-hop precision without looping.
16. `CrossFileRelationships`: Verifies relationships spanning multiple files.
17. `RetrievalUnitAndSearchResultOverloads`: Verifies convenient overloads for `RetrievalUnit` and `PrimarySearchResult`.

**Result**: 17 / 17 test cases pass. Total engine tests: **318 / 318 passing across 49 test suites**.

---

## 14. Performance Observations

- Resolving 1-hop direct relationships relies exclusively on $O(1)$ hash table lookups in `RelationshipGraph` and `InvertedIndex`.
- In unit benchmarks, resolving direct relationships for a symbol with multiple relationships takes $< 10\ \mu\text{s}$, which is negligible compared to embedding inference and lexical retrieval.
- Memory overhead is minimal: only small value structs are instantiated on demand.

---

## 15. Explicitly Deferred Work

The following components and concepts are intentionally NOT implemented in Phase 7.2 and deferred to subsequent phases:

- **EvidenceBundle & EvidenceAssembler** (deferred to Phase 7.3)
- **ContextBuilder & ContextPackage** (deferred to Phase 7.4)
- **Recursive graph expansion / depth $> 1$ traversal**
- **Graph clustering / community detection / neighborhood summarization**
- **Graph storage or edge generation redesign**
- **LLM integration or prompt construction**
- **Retrieval ranking or scoring modifications**
