# Phase 6.3 — Hierarchical Retrieval Units & Supporting Evidence

## 1. Purpose

Phase 6.3 establishes a **retrieval-oriented representation** of code elements
that is separate from, but derived from, the structural AST representation
produced by the parser.

---

## 2. Problem

Amoeba's parser extracts fine-grained AST elements across many `ElementKind`
categories:

```
Component          ← 18 in calendar holdout
Function           ← 37
Method             ← 12
Hook               ← 6
Route              ← 14
─────────────────────
UtilityClass       ← 1,280  (Tailwind CSS classnames)
Attribute          ← 511
Call               ← 322
Include            ← 90
JSXElement         ← 490
Selector           ← ...
Property           ← ...
```

When all of these elements are treated as equivalent, independent candidates
in the top-level retrieval result set, fine-grained sub-elements compete with
— and frequently outrank — the architectural symbols a developer is looking for.

**Example** (from Phase 6.2 real-world holdout):

> Query: *"Where is the location destination photo rendered for each month?"*

With a flat element list:
- An `Attribute` element (`className`) inside `getImagePanelData()` matched
  and ranked above the `Component` (`ImagePanel`) the developer wanted.

The problem is not parsing quality — the parser correctly extracted all
elements. The problem is that **the retrieval candidate population is too
large and too flat**. A `className` attribute should not compete equally
with an architectural `Component` as a top-level search result.

---

## 3. Design

```
================================================================================
IMPORTANT CONCEPTUAL BOUNDARY

  CodeElement   = structural/AST representation (produced by SourceParser)
  RetrievalUnit = retrieval-oriented representation (built by SupportingEvidenceResolver)

  These are related, but intentionally NOT the same abstraction.
  A CodeElement is never deleted or modified by Phase 6.3.
  A RetrievalUnit is a search-oriented view over existing elements.
================================================================================
```

```
Source Files
    │
    ▼ SourceParser
    │
CodeElement[]          ← Full structural representation; unchanged
    │
    ▼ RetrievalUnitClassifier
    │
    ├── Primary                ── Class, Struct, Interface, Function, Method,
    │                             Component, Hook, Route
    │
    └── Supporting             ── Call, Attribute, JSXElement, JSXComponent,
                                  UtilityClass, Include, Property, Selector, Unknown
                  │
                  ▼ SupportingEvidenceResolver
                  │
                  └── attributed to nearest enclosing Primary
                      (or left unresolved if no confident match)
                                │
                                ▼
                        RetrievalUnit[]
                        ├── primary_element (CodeElement)
                        └── supporting_elements ([]CodeElement)
```

---

## 4. Classification Policy

`RetrievalUnitClassifier` is a deterministic, stateless, `constexpr` policy class.

### Primary

| ElementKind | Rationale |
| :--- | :--- |
| `Class` | Top-level data/service definition |
| `Struct` | Value type or data contract |
| `Interface` | Type contract or API shape |
| `Function` | Standalone logic unit |
| `Method` | Method of a class or struct |
| `Component` | React/UI component |
| `Hook` | React hook |
| `Route` | Next.js / framework route |

### Supporting

| ElementKind | Rationale |
| :--- | :--- |
| `Call` | Call site within a function body |
| `Attribute` | JSX/HTML attribute |
| `JSXElement` | Rendered HTML element |
| `JSXComponent` | Rendered component reference |
| `UtilityClass` | Tailwind/CSS utility class name |
| `Include` | ES/TS import statement |
| `Property` | CSS property or object property |
| `Selector` | CSS selector |
| `Unknown` | Default for unclassified nodes |

**Default policy**: Any future or unknown `ElementKind` defaults to `Supporting`
via the `default:` case in the classifier switch. This prevents unclassified
nodes from masquerading as primary retrieval results.

---

## 5. Evidence Resolution Rules

`SupportingEvidenceResolver` attributes supporting elements to their owning
primary unit using these rules **in priority order**:

1. **`parent_context` match** (authoritative):
   If `child.parent_context` is non-empty and equals a primary unit's `name`,
   the child is attributed to that unit. Tree-sitter populates `parent_context`
   reliably during parsing.

2. **Source range containment**:
   If the child's source range `[start.line, end.line]` is fully enclosed
   within a primary unit's source range, the child is attributed to the
   **innermost** (smallest-span) enclosing primary.

3. **Unresolved** (no fallback to unrelated symbols):
   If neither rule matches, the element is left **unresolved**.
   It is **not** silently assigned to any other primary symbol in the file.
   This is critical for files with multiple independent top-level symbols.

### File-Level Module Unit

If a source file contains **no primary symbols** (e.g., `globals.css`,
pure-config files), a synthetic module-level `Route` primary is created
with `detail = "File Module"`. All elements in such a file are attributed
to this single unit, since the attribution is unambiguous (there is no
other primary to misassign to).

### Candidate Population

The resolver reduces the **top-level retrieval candidate population** from
all `CodeElement[]` to `RetrievalUnit[]` (primary symbols only). On the
Phase 6.2 real-world holdout (2,820 elements across 27 files), this
produced 183 primary `RetrievalUnit`s.

> **Terminology note**: This is a **reduction in the top-level retrieval
> candidate population**, not "compression". The full `CodeElement`
> representation in `ParsedFile` and `InvertedIndex` remains intact.
> Phase 6.3 does not remove or discard any structural information.

---

## 6. Phase Scope Boundary

```
Phase 6.3 — Retrieval Unit Model (THIS PHASE)
    Establishes and validates the RetrievalUnit abstraction.
    Does NOT modify the production retrieval path.

Phase 6.4 — Live Retrieval Integration (NEXT PHASE)
    Integrates RetrievalUnit into SearchEngine, InvertedIndex,
    SemanticRetriever, and HybridRetriever.
```

> [!IMPORTANT]
> **Phase 6.3 does NOT change production `SearchEngine` behavior.**
>
> `SearchEngine`, `InvertedIndex`, `SemanticRetriever`, and `HybridRetriever`
> continue to operate over raw `CodeElement[]` exactly as in Phase 6.2.
>
> The `RetrievalUnit` abstraction is available as a standalone representation
> layer, validated by its own test suite, and ready for Phase 6.4 integration.

---

## 7. Components

### `RetrievalUnitRole` (enum class, `retrieval_unit.hpp`)

```cpp
enum class RetrievalUnitRole : uint8_t { Primary, Supporting };
```

### `RetrievalUnitClassifier` (class, `retrieval_unit.hpp`)

```cpp
class RetrievalUnitClassifier {
    static constexpr RetrievalUnitRole classify(parser::ElementKind) noexcept;
    static constexpr bool is_primary(parser::ElementKind) noexcept;
    static constexpr bool is_supporting(parser::ElementKind) noexcept;
};
```

### `RetrievalUnit` (struct, `retrieval_unit.hpp`)

```cpp
struct RetrievalUnit {
    index::ElementId primary_element_id;
    parser::CodeElement primary_element;
    std::filesystem::path file_path;
    std::string language;
    RetrievalUnitRole role;
    std::vector<uint32_t> supporting_element_ids;
    std::vector<parser::CodeElement> supporting_elements;
};
```

### `EvidenceResolution` (struct, `supporting_evidence_resolver.hpp`)

```cpp
struct EvidenceResolution {
    uint32_t supporting_element_idx;
    std::optional<uint32_t> owner_unit_index;  // nullopt = unresolved
};
```

### `SupportingEvidenceResolver` (class, `supporting_evidence_resolver.hpp`)

```cpp
class SupportingEvidenceResolver {
    static std::vector<RetrievalUnit> resolve_units(const ParsedFile&);
    static std::vector<RetrievalUnit> resolve_units(std::span<const ParsedFile>);
    static void resolve_with_diagnostics(const ParsedFile&,
                                         std::vector<RetrievalUnit>&,
                                         std::vector<EvidenceResolution>&);
    static bool is_enclosed_by(const CodeElement& child,
                                const CodeElement& parent) noexcept;
};
```

---

## 8. LLD / SOLID Notes

| Principle | Application |
| :--- | :--- |
| **SRP** | Classifier classifies. Resolver resolves. RetrievalUnit stores. No mixing. |
| **OCP** | Adding a new `ElementKind` to Primary requires only a new `case` in `classify()`. Resolver, RetrievalUnit, tests are unchanged. |
| **DIP** | Resolver depends on `ParsedFile` and `CodeElement` — Amoeba's own stable abstractions. Does not depend on Tree-sitter directly. |
| **Composition** | RetrievalUnit holds `CodeElement` by value. No raw owning pointers. |
| **constexpr** | `classify()`, `is_primary()`, `is_supporting()` are all `constexpr`. |
| **Rule of Zero** | All types use default constructors and `operator==`. |

---

## 9. Evaluation Note

The Phase 6.2 holdout benchmark demonstrated that replacing the flat
`CodeElement` candidate set with `RetrievalUnit[]` substantially improved
ranking quality (e.g., Hybrid α=0.5 P@1: 0.70 vs 0.10 on the frozen N=10 set).

**This result was produced by a Python representation-level evaluation**
that mirrors the `SupportingEvidenceResolver` policy in Python.

> This is a **representation-level validation**. It demonstrates that the
> classification and ownership policy produces the right candidate population.
> It does **not** reflect production `C++ SearchEngine` behavior, which is
> unchanged in Phase 6.3. The before/after production comparison belongs to
> Phase 6.4 / Phase 6.5.

---

## 10. Test Suite

- **15 tests** across 2 test suites:
  - `RetrievalUnitClassifierTest` (5 tests):
    Primary kinds, Supporting kinds, mutual exclusivity, Unknown default, to_string
  - `SupportingEvidenceResolverTest` (10 tests):
    1. Class → Method → Call nesting
    2. Component → JSX → Attribute
    3. Multiple independent functions (no false ownership)
    4. Multiple independent components (no evidence leakage)
    5. Nested functions (innermost primary wins)
    6. Unresolved element (owner_unit_index == nullopt)
    7. Cross-file independence
    8. Overlapping source ranges (innermost span wins)
    9. Deterministic ownership (same input → same output)
    10. Pure CSS file gets synthetic module-level unit

---

## 11. Files

| File | Purpose |
| :--- | :--- |
| `engine/include/amoeba/retrieval/retrieval_unit.hpp` | Role enum, classifier, RetrievalUnit struct |
| `engine/include/amoeba/retrieval/supporting_evidence_resolver.hpp` | EvidenceResolution, SupportingEvidenceResolver interface |
| `engine/src/retrieval/retrieval_unit.cpp` | (minimal; all classifier logic is constexpr in header) |
| `engine/src/retrieval/supporting_evidence_resolver.cpp` | Resolution implementation |
| `engine/tests/retrieval/retrieval_unit_test.cpp` | 15-test validation suite |
| `docs/phases/phase-06.3-hierarchical-retrieval-units.md` | This document |
