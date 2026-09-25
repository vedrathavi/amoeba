# Phase 5.3 — Inheritance & Implements Relationships

## 1. Overview & Objective

Phase 5.3 adds **structural inheritance (`INHERITS_FROM`) and interface implementation (`IMPLEMENTS`) relationship extraction** to Amoeba's relationship graph subsystem.

This phase enables Amoeba to extract type hierarchy relationships reliably represented by the Tree-sitter parsers across:
- **C++**: Class & struct single/multiple inheritance $\longrightarrow$ `RelationshipKind::InheritsFrom`
- **Java**: Class inheritance (`extends`) $\longrightarrow$ `RelationshipKind::InheritsFrom`, interface implementation (`implements`) $\longrightarrow$ `RelationshipKind::Implements`, interface extension $\longrightarrow$ `RelationshipKind::InheritsFrom`
- **Python**: Class single & multiple inheritance $\longrightarrow$ `RelationshipKind::InheritsFrom`
- **TypeScript / JavaScript**: Class extension (`extends`) $\longrightarrow$ `RelationshipKind::InheritsFrom`, interface implementation (`implements`) $\longrightarrow$ `RelationshipKind::Implements`, interface extension (`extends`) $\longrightarrow$ `RelationshipKind::InheritsFrom`

---

## 2. Core Principles & Resolution Boundary

### Soundness Over Artificial Completeness
> **Prefer a correct partial relationship graph over a complete-looking incorrect graph.**

Amoeba does not pretend Tree-sitter provides full compiler-grade type resolution or semantic type-checking.

1. **Confidently Resolved Internal Types**:
   When a base type or interface can be resolved to a known indexed `Class`, `Struct`, or `Interface` element in the repository:
   $$\text{Derived Element} \xrightarrow{\quad\text{InheritsFrom / Implements}\quad} \text{Target Base Element}$$
2. **Explicit Unresolved Representation**:
   If the base/interface name is syntactically known but cannot be resolved to a repository element (e.g. `std::runtime_error`, `React.Component`, `java.lang.Object`, or external libraries), it is tracked in [`InheritanceExtractionResult::resolutions`](file:///d:/amoeba/engine/include/amoeba/graph/inheritance_extractor.hpp) with `is_resolved = false`.
   **Amoeba does NOT create dangling or synthetic placeholder nodes in the graph.**

---

## 3. Extractor Architecture ([InheritanceExtractor](file:///d:/amoeba/engine/include/amoeba/graph/inheritance_extractor.hpp))

```text
                     InvertedIndex
            (Parsed Files & CodeElements)
                          │
                          ▼
                InheritanceExtractor
        ┌─────────────────┴─────────────────┐
        ▼                                   ▼
 Confidently Resolved               Unresolved External
 (Same-File/Context/Repo)         (Stdlib / Third-Party)
        │                                   │
        ▼                                   ▼
 RelationshipGraph               InheritanceExtractionResult
 (Directed Edges Added)              (Metadata Tracking)
```

### Resolution Rules & Precedence:
1. **Same-File Resolution**: Checks if the target class/struct/interface is defined in the same file.
2. **Namespace / Parent Context Resolution**: Prioritizes types sharing the source class's namespace or enclosing context (e.g. `Net::HttpConnection` inheriting from `Net::Connection`).
3. **Exact Qualified / Simple Match**: Searches indexed types across the entire repository matching the qualified or simple name.
4. **Stem Match**: Matches the target stem across the repository (e.g. stripping template/generic parameters like `Base<T>` $\rightarrow$ `Base`).

---

## 4. Resolution Limitations & Non-Goals

| Limitation / Non-Goal | Design Rationale & Boundary |
| :--- | :--- |
| **No Compiler Type Checker** | Complex C++ template metaprogramming (e.g. CRTP with template aliases or SFINAE base selection) and dynamic JavaScript mixins are not resolved beyond syntactic names. |
| **No Speculative Global Tables** | Amoeba avoids guessing type identities across unrelated namespaces if ambiguity exists. |
| **No Synthetic Graph Nodes** | Unresolved standard library base classes (`std::exception`) do not pollute the element ID space. |
| **Non-Goals Preserved** | Call graph extraction, design pattern detection, graph persistence, and semantic embeddings remain explicitly deferred to later phases. |

---

## 5. Verification Suite & Test Coverage

The test suite in [`engine/tests/graph/inheritance_extractor_test.cpp`](file:///d:/amoeba/engine/tests/graph/inheritance_extractor_test.cpp) verifies:
1. **C++ Single & Multiple Inheritance**: `Dog : Animal`, `Duck : Animal, Flyable`.
2. **Java `extends` and `implements`**: `UserService extends BaseService implements Service, AuthProvider`.
3. **TypeScript Class & Interface Hierarchy**: Class inheritance, interface inheritance, and interface implementation.
4. **Python Class Inheritance**: Multi-level inheritance chains (`AdminUser -> User -> BaseModel`).
5. **Cross-File Inheritance Resolution**: Base class in header file, derived class in separate header/source file.
6. **Namespace-Aware Resolution**: Disambiguating same-named classes across different namespaces (`Net::Connection` vs `DB::Connection`).
7. **Unresolved External Types**: Accurate `is_resolved = false` tracking without generating synthetic nodes.
8. **Malformed / Incomplete Source**: Resilient extraction without crashes on syntax errors.

---

## 6. Status Sign-off & Verdict

- **Total Test Cases**: **118 / 118 passing (100%)**.
- **Compiler Warnings**: **0 warnings** with `-Wall -Wextra -Wpedantic` in C++20.
- **Code Style**: 100% formatted via `.clang-format`.
- **Verdict**: **READY FOR PHASE 5.4**
