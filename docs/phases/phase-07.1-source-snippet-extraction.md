# Phase 7.1 — Source Snippet Extraction

**ACCURATE, ISOLATED SOURCE CODE EXCERPT EXTRACTION FROM SOURCERANGE SPECIFIERS**

---

## 1. Goal

Amoeba's retrieval pipeline identifies relevant code structures and attaches precise `SourceRange` metadata, but the raw file buffers are intentionally discarded after AST parsing to maintain an in-memory footprint bounded by metadata size.

Phase 7.1 introduces `SourceSnippetReader` and `SourceExcerpt` — the smallest clean, self-contained mechanism to extract exact, verbatim source code snippets corresponding to a `SourceRange` from disk files or in-memory source buffers.

```text
    Source File Path (or in-memory string) + SourceRange
                            │
                            ▼
                   SourceSnippetReader
                            │
                            ▼
                      SourceExcerpt
```

---

## 2. Existing SourceRange Semantics

Inspection of Amoeba's core AST model in `SourceLocation`, `SourceRange`, and Tree-sitter extractors reveals:

| Field | Representation | Semantics |
|---|---|---|
| `start.line` | `uint32_t` (1-indexed) | 1-indexed row matching standard IDE and compiler diagnostics. |
| `start.column` | `uint32_t` (1-indexed) | 1-indexed column position within the start line. |
| `start.byte_offset` | `uint32_t` (0-indexed) | Exact 0-indexed byte offset within the UTF-8 encoded source buffer. |
| `end.line` | `uint32_t` (1-indexed) | 1-indexed row where the syntactic element ends. |
| `end.column` | `uint32_t` (1-indexed) | 1-indexed column position where the syntactic element ends. |
| `end.byte_offset` | `uint32_t` (0-indexed) | 0-indexed exclusive byte offset (`[start.byte_offset, end.byte_offset)` half-open interval). |

---

## 3. Design & Responsibilities

`SourceSnippetReader` is a stateless, concrete component with zero external dependencies on retrieval, ranking, graph models, or LLM abstractions.

### Responsibilities:
1. Open and read source files from disk (binary-safe stream reading).
2. Direct, exact slice extraction via UTF-8 byte offsets `[start.byte_offset, end.byte_offset)`.
3. Fallback line-indexed extraction when byte offsets are unpopulated.
4. Optional context lines expansion around the target range.
5. Deterministic and safe error handling for missing files, invalid ranges, and empty files.

---

## 4. Data Model: `SourceExcerpt`

[`SourceExcerpt`](file:///d:/amoeba/engine/include/amoeba/source/source_excerpt.hpp) is a lightweight value type:

```cpp
struct SourceExcerpt {
    std::filesystem::path file_path;
    parser::SourceRange requested_range;
    std::string text;                    ///< Verbatim extracted source code
    uint32_t start_line{1};              ///< 1-indexed start line of text
    uint32_t end_line{1};                ///< 1-indexed end line of text
    uint32_t context_lines_before{0};    ///< Leading context lines included
    uint32_t context_lines_after{0};     ///< Trailing context lines included
    bool has_context_lines{false};       ///< True if context was expanded

    [[nodiscard]] bool operator==(const SourceExcerpt&) const = default;
};
```

---

## 5. UTF-8 & Range Slicing Correctness

- **Byte Offset Slicing**: Slicing occurs via `source.substr(start.byte_offset, end.byte_offset - start.byte_offset)` directly on the UTF-8 byte representation. Multibyte sequences (Hindi, Japanese, Greek, emojis) preceding the target symbol do not perturb byte offsets because Tree-sitter byte offsets measure raw bytes, not UTF-16 code units or Unicode character counts.
- **Line Index Construction**: Built via single-pass scan for `\n` characters, indexing the byte start of every line in $O(N)$ time and $O(\text{lines})$ space.
- **Verbatim Preservation**: Indentation (tabs/spaces), whitespace, comments, and original line endings (`\n` or `\r\n`) are preserved without normalization.

---

## 6. Context Line Expansion

When `context_lines = K` ($K > 0$):
- Start line expands to `std::max(1, range.start.line - K)`.
- End line expands to `std::min(total_lines, range.end.line + K)`.
- `text` captures full lines across `[start_line, end_line]`.
- Excerpt metadata records `context_lines_before` and `context_lines_after` accurately.

---

## 7. Error Behavior

| Scenario | `read_range` / `read_range_from_text` | `try_read_range` / `try_read_range_from_text` |
|---|---|---|
| Non-existent file path | Throws `std::invalid_argument` | Returns `std::nullopt` |
| Non-regular file (directory, socket) | Throws `std::invalid_argument` | Returns `std::nullopt` |
| Inverted range (`start > end`) | Throws `std::invalid_argument` | Returns `std::nullopt` |
| Out of bounds range (`end > length`) | Throws `std::out_of_range` | Returns `std::nullopt` |
| Empty file with `range = (0,0)` | Returns `SourceExcerpt` with `text = ""` | Returns `SourceExcerpt` with `text = ""` |
| Empty file with `range > 0` | Throws `std::out_of_range` | Returns `std::nullopt` |

---

## 8. Test Coverage

Unit tests in [`engine/tests/source/source_snippet_reader_test.cpp`](file:///d:/amoeba/engine/tests/source/source_snippet_reader_test.cpp) verify 18 comprehensive scenarios:
1. `CppClassAndFunctionExtraction` — C++ class and method extraction from AST ranges.
2. `TypeScriptFunctionExtraction` — TypeScript function extraction.
3. `PythonFunctionAndClassExtraction` — Python classes and methods.
4. `CSSSelectorExtraction` — CSS rulesets and selectors.
5. `ExactSingleLineExtraction` — Single-line exact boundary extraction.
6. `MultiLineExtractionPreservesWhitespace` — Preserves indentation and mixed tabs/spaces.
7. `ContextLineExtractionBeforeAndAfter` — Leading and trailing context line expansion.
8. `FirstLineRangeContextClampsToBeginning` — Bounds clamping on line 1.
9. `LastLineRangeContextClampsToEnd` — Bounds clamping on EOF.
10. `MissingFileThrowsAndTryReturnsNullopt` — Safe missing file handling.
11. `InvertedRangeThrowsAndTryReturnsNullopt` — Range inversion validation.
12. `OutOfBoundsRangeThrowsAndTryReturnsNullopt` — Range overflow protection.
13. `EmptySourceReturnsEmptyExcerptSafely` — Empty buffer safety.
14. `FileWithoutTrailingNewline` — Files lacking terminal `\n`.
15. `MultibyteUtf8CharactersBeforeTargetRange` — UTF-8 safety with preceding Hindi, Japanese, and Greek comments.
16. `DeterministicRepeatedExtraction` — Repeatability and equality operator verification.
17. `DiskFileExtractionRoundTrip` — Disk file write/parse/read round-trip.
18. `ExtractionBenchmarkObservation` — Latency measurement for 1, 5, and 10 snippet extractions.

---

## 9. Performance Observations

Micro-benchmark results from `ExtractionBenchmarkObservation` on disk files:
- **1 snippet extraction**: ~149 microseconds
- **5 snippet extractions**: ~428 microseconds (~85 us / snippet)
- **10 snippet extractions**: ~708 microseconds (~70 us / snippet)

Reading small byte windows from disk leverages the OS page cache with sub-millisecond execution time, confirming that premature caching layers are completely unnecessary.

---

## 10. Explicitly Deferred Work

The following items are intentionally **not implemented** in Phase 7.1 and deferred to subsequent phases:
- `EvidenceBundle` and `EvidenceAssembler` (Phase 7.3)
- Graph relationship evidence resolution (Phase 7.2)
- `ContextBuilder` and Markdown/JSON context packaging (Phase 7.4)
- Token budgeting and character truncation (Phase 7.4)
- LLM inference or reasoning integration (Phase 7.5+)
- Persistent source caching or memory mapping
- Any modifications to Phase 6 retrieval, ranking, or indexing
