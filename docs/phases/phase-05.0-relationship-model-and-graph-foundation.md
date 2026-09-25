# Phase 5.0 — Relationship Model & Graph Foundation

## 1. Overview & Objective

Phase 5.0 begins Amoeba's code relationship and structural connectivity subsystem.
Where Phase 3 established lexical indexing and Phase 4 established relevance ranking, Phase 5 addresses the fundamental structural question:

> **How is code connected across declarations, definitions, invocations, and modules?**

The governing architectural principle is:

> **Store fundamental facts; derive higher-level knowledge.**

Phase 5.0 establishes the in-memory relationship domain model and directed graph foundation only. It deliberately avoids premature extraction logic, AST queries, symbol resolution, or graph persistence.

---

## 2. The Relationship Model

A **Relationship** is a directed typed connection between two code elements identified by their unique, compact [ElementId](file:///d:/amoeba/engine/include/amoeba/graph/relationship.hpp):

$$\text{Source Element} \xrightarrow{\quad\text{RelationshipKind}\quad} \text{Target Element}$$

```text
AuthController (ElementId: 101)
       │
       │ CALLS
       ▼
AuthService (ElementId: 202)
       │
       │ CALLS
       ▼
UserRepository (ElementId: 303)
```

*(Note: The above diagram illustrates conceptual call relationships represented by ElementIds; extraction of these edges will be introduced in subsequent phases).*

### Key Properties:
- **Zero Data Duplication**: The relationship stores strictly `(source: ElementId, target: ElementId, kind: RelationshipKind)`. It never duplicates source text, AST tokens, or file metadata.
- **Trivially Copyable & Compact**: [Relationship](file:///d:/amoeba/engine/include/amoeba/graph/relationship.hpp) is a lightweight value type ($\le 16\text{ bytes}$) with default three-way comparison (`<=>`) and hash support.

---

## 3. Fundamental Relationship Kinds

Amoeba models primitive code semantics via [RelationshipKind](file:///d:/amoeba/engine/include/amoeba/graph/relationship_kind.hpp):

| RelationshipKind | Description | Typical Example |
| :--- | :--- | :--- |
| `Contains` | Structural containment / lexical hierarchy | `Class` contains `Method`, `File` contains `Function` |
| `Imports` | Module or package dependency | `import { useState } from 'react'` |
| `Includes` | Preprocessor file inclusion | `#include <vector>` |
| `Calls` | Function or method invocation | `auth_service->login(user)` |
| `References` | Variable, field, or symbol reference | Reading `config.port`, passing a constant |
| `InheritsFrom` | Object-oriented subtyping | `class Derived : public Base` |
| `Implements` | Interface or contract implementation | `class Service implements IService` |

### Why Fundamental Facts Instead of High-Level Patterns?
We strictly avoid encoding synthetic or speculative edge kinds such as `FACTORY_PATTERN`, `STRATEGY_PATTERN`, `MVC_PATTERN`, or `AUTHENTICATION_FLOW`. 
High-level design patterns and architectural flows are subjective, multi-element topological motifs that should be **derived** via graph queries over primitive facts rather than hardcoded as edge types.

---

## 4. Directed Graph Architecture ([RelationshipGraph](file:///d:/amoeba/engine/include/amoeba/graph/relationship_graph.hpp))

### 4.1 Why a Directed Graph?
Code relationships are inherently asymmetric:
- If function $A$ calls function $B$ ($A \xrightarrow{\text{CALLS}} B$), $B$ does not call $A$.
- If class $D$ inherits from $B$ ($D \xrightarrow{\text{INHERITS\_FROM}} B$), $B$ does not inherit from $D$.

Bidirectional query support is essential:
- **Outgoing Queries**: "What does this function call?", "What does this file import?"
- **Incoming Queries**: "Who calls this function?", "Who implements this interface?", "What depends on this module?"

### 4.2 Separation of Inverted Index and Relationship Graph
```text
                  CodeElementStore
                         │
         ┌───────────────┴───────────────┐
         ▼                               ▼
    InvertedIndex                RelationshipGraph
(Term -> ElementIds)          (ElementId -> ElementId)
         │                               │
         └───────────────┬───────────────┘
                         ▼
             Hybrid Search & Analysis
```
- **InvertedIndex**: Optimized for text tokenization, postings lists, BM25 term frequencies, and document lengths.
- **RelationshipGraph**: Optimized for structural adjacency lookups, directed connectivity, and topological traversal.

---

## 5. Storage Strategy & Edge Case Policies

### Data Structures:
- `edges_`: `std::unordered_set<Relationship, RelationshipHash>` ($O(1)$ average-case edge existence checks and uniqueness guarantees).
- `outgoing_adj_`: `std::unordered_map<ElementId, std::vector<Relationship>>` ($O(1)$ average-case bucket lookup, $O(\text{deg}(v))$ to return/traverse outgoing edges).
- `incoming_adj_`: `std::unordered_map<ElementId, std::vector<Relationship>>` ($O(1)$ average-case bucket lookup, $O(\text{deg}(v))$ to return/traverse incoming edges).
- `node_degree_`: `std::unordered_map<ElementId, std::size_t>` (tracks node participation and total distinct nodes).

> [!NOTE]
> **Storage Redundancy & Future Optimization**:
> Maintaining `std::unordered_set<Relationship>` alongside `outgoing_adj_` and `incoming_adj_` duplicates edge storage across three data structures. This provides clean, unambiguous semantics and fast operations during foundation phases. In Phase 5.9 (Graph Audit & Optimization), we will benchmark memory consumption across large codebases to measure whether this structural duplication is significant before performing any optimization.

### Edge Case Policies:
1. **Duplicate Relationships**: Set semantics. Attempting to add an identical directed edge `(source, target, kind)` returns `false` and does not duplicate entries.
2. **Self Relationships**: Supported. Recursive calls (e.g. $F \xrightarrow{\text{CALLS}} F$) or self-references are valid.
3. **Removing Relationships**: Removing an edge cleans up outgoing and incoming adjacency lists; if a node has no remaining edges, its node degree is pruned. Returns `false` if not found.
4. **Unknown Element IDs**: Queries for unknown element IDs return empty vectors without throwing or crashing.

---

## 6. Explicit Non-Goals for Phase 5.0

Phase 5.0 intentionally does NOT implement:
- Relationship extraction from ASTs or files
- Tree-sitter query patterns for relationships
- Symbol resolution or cross-file linkers
- Call graph builders or type inference
- Graph database persistence or serialization
- Graph visualization / UI rendering
- Embeddings or semantic vector search

---

## 7. Verification & Status

- **Unit Tests**: 13 test cases covering all edge cases, self-loops, duplicates, bidirectional queries, kind filtering, and memory contracts.
- **Compiler Compliance**: 0 warnings under `-Wall -Wextra -Wpedantic` in C++20.
- **Formatting**: 100% compliant with `.clang-format`.
- **Suite Pass Rate**: 94/94 total engine and graph tests passing.
