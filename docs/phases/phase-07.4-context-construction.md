# Amoeba — Phase 7.4: Context Construction & Budgeting

## 1. Goal

Phase 7.3 introduced `EvidenceBundle`, aggregating all raw evidence Amoeba collected during retrieval (primary ranking metadata, exact source excerpts, supporting AST nodes, and direct structural relationship evidence).

Phase 7.4 implements the **Context Construction & Budgeting** layer:
```
EvidenceBundle  ──►  ContextBuilder  ──►  ContextPackage
```

This layer answers the fundamental downstream question:
> *"Given all available evidence, what bounded, prioritized, and formatted representation should actually be presented to a downstream consumer (CLI, IDE, or future LLM)?"*

---

## 2. Evidence vs Context Distinction

| Aspect | Evidence Layer (`evidence::EvidenceBundle`) | Context Layer (`context::ContextPackage`) |
| :--- | :--- | :--- |
| **Concept** | *What Amoeba knows* | *What Amoeba presents* |
| **Completeness** | Complete and unconstrained | Bounded, selected, and truncated |
| **Mutability** | Input bundle remains immutable | Self-contained output value object |
| **Budgeting** | None (holds all gathered facts) | Explicit character, item, and line limits |
| **Formatting** | Raw structured objects | Formatted Markdown + selected structured slice |

---

## 3. Package & Architecture Layout

The dedicated context module lives under:
- `engine/include/amoeba/context/context_package.hpp`
- `engine/include/amoeba/context/context_builder.hpp`
- `engine/src/context/context_builder.cpp`
- `engine/tests/context/context_builder_test.cpp`

The architectural flow is clean and strictly unidirectional:
```
retrieval/  (Which code is relevant?)
    │
source/     (What source text corresponds to this range?)
    │
graph/      (How are code entities connected?)
    │
evidence/   (What evidence do we have?)
    │
context/    (What evidence should we present?)
```

`ContextBuilder` consumes `EvidenceBundle` **only**. It has:
- **NO** filesystem access (does not call `SourceSnippetReader` or disk)
- **NO** graph access (does not call `RelationshipGraph` or `RelationshipEvidenceResolver`)
- **NO** retrieval access (does not call `SearchEngine`, `InvertedIndex`, or rankers)
- **NO** external LLM dependencies or tokenizers

---

## 4. ContextPackage Design

`ContextPackage` is a lightweight, self-contained value type containing:

```cpp
struct ContextPackage {
    std::string query;
    std::vector<evidence::EvidenceItem> selected_items;
    std::string rendered_markdown;

    // Budget & Observability accounting
    std::size_t total_available_items = 0;
    std::size_t selected_item_count = 0;
    std::size_t max_character_budget = 0;
    std::size_t used_characters = 0;
    bool truncated = false;
    std::string truncation_reason;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    bool operator==(const ContextPackage& other) const = default;
};
```

---

## 5. ContextBuilder & Selection Policy

`ContextBuilder` applies deterministic selection and budgeting rules configured via `ContextBuilderOptions`:

```cpp
struct ContextBuilderOptions {
    std::size_t max_primary_items = 3;
    std::size_t max_source_lines_per_item = 50;
    std::size_t max_relationships_per_item = 5;
    std::size_t max_supporting_elements_per_item = 5;
    std::size_t max_character_budget = 4000;  // 0 = unlimited

    bool include_source = true;
    bool include_supporting_evidence = true;
    bool include_relationships = true;
    bool include_scores = true;
};
```

### Deterministic Priority Ordering

1. **Retrieval Rank Priority**: Primary items are processed strictly in upstream ranking order (`items[0]`, `items[1]`, ...). No secondary ranking is performed.
2. **Within an Evidence Item**:
   1. Symbol header & location metadata
   2. Source excerpt (bounded by `max_source_lines_per_item`)
   3. Supporting AST elements (bounded by `max_supporting_elements_per_item`)
   4. Direct relationships with arrows (bounded by `max_relationships_per_item`)
3. **Total Budget Truncation**: When the character limit is reached, lower-ranked items are excluded, or if the first item exceeds budget, a valid UTF-8 prefix is sliced with a clear truncation footer.

---

## 6. Truncation & UTF-8 Safety

To prevent emitting corrupted multibyte sequences:
- `safe_utf8_prefix(std::string_view str, std::size_t max_bytes)` inspects lead bytes and continuation bytes (`0x80..0xBF`).
- If `max_bytes` lands in the middle of a 2-, 3-, or 4-byte UTF-8 sequence, truncation cuts cleanly before the split code point.
- Valid UTF-8 output is 100% guaranteed.

---

## 7. Model-Independent Markdown Rendering Format

Rendered markdown is clean, structured, and model-independent:

```markdown
# Context: calculator add

## Result 1: `Calculator` (Score: 0.9200)
- **Kind**: Class
- **File**: src/calc.cpp
- **Range**: L2:C1 - L8:C2

### Source Excerpt (Lines 2-8):
```cpp
class Calculator {
public:
    int add(int a, int b) {
        logOperation("add");
        return a + b;
    }
};
```

### Supporting AST Elements:
- Method `add` (L4:C5 - L7:C6)
- Call `logOperation` (L5:C9 - L5:C23)

### Direct Relationships:
- Calls → `logOperation`

---
```

---

## 8. Missing & Partial Evidence Handling

`ContextBuilder` gracefully handles incomplete bundles:
- Empty bundle: Emits `# Context: <query>\n\nNo relevant code elements were retrieved.\n`
- Missing source excerpt: Renders `*[Source code unavailable]*` without failing.
- Missing relationships / supporting AST: Cleanly omitted without emitting empty headers.
- Unresolved relationship targets: Renders whatever target name or ID is recorded.

---

## 9. Verification & Test Results

A dedicated test suite `ContextBuilderTest` in `engine/tests/context/context_builder_test.cpp` covers:
- **A**: Empty `EvidenceBundle` handling
- **B**: Single primary item rendering
- **C–F**: Multiple primary items ranking order and `max_primary_items` (1, 3, 5) limits
- **G**: Source rendering & `max_source_lines_per_item` truncation
- **H**: Supporting AST elements rendering & limits
- **I–K**: Direct relationship rendering with directional arrows (`Calls →`, `Called By ←`, `Inherits From →`, `Implements →`, etc.)
- **L–N**: Graceful degradation on missing source, relationships, or supporting elements
- **O–Q**: Total character budget enforcement & multibyte UTF-8 safety
- **R**: Deterministic repeated builds (`pkg1 == pkg2`)
- **S–W**: Immutability of input `EvidenceBundle`, ranking preservation, and no evidence fabrication
- **Integration**: Full end-to-end pipeline test (`PrimarySearchResult` → `EvidenceAssembler` → `EvidenceBundle` → `ContextBuilder` → `ContextPackage`)

### Test Suite Execution
```
Total Test Suites: 51
Total Tests: 342 / 342 (100% passing)
Compiler Warnings: 0
clang-format: clean
git diff --check: clean
```

---

## 10. Performance Observations

Context building operates purely in-memory via string stream formatting and string view operations:
- 1 item: < 0.01 ms
- 3 items: < 0.02 ms
- 5 items: < 0.03 ms
- Total overhead is negligible compared to retrieval and parsing.

---

## 11. Explicitly Deferred Work

The following items are intentionally **deferred** to subsequent phases:
- LLM API integration (OpenAI, Anthropic, Gemini, local llama.cpp)
- Model-specific tokenizers (BPE, tiktoken, SentencePiece)
- Prompt templates and prompt engineering
- RAG frameworks / multi-hop agent reasoning
- Vector databases / external index persistence
