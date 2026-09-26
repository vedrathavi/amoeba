# Phase 6.8 — CLI Retrieval Integration

**INTEGRATION OF PRIMARY RETRIEVAL PIPELINE INTO AMOEBA CLI**

---

## 1. Objective & Scope

Phase 6.8 bridges the gap between the internal production retrieval pipeline (`PrimaryRetrievalPipeline`) and the command-line interface (`amoeba_cli`).

Prior to Phase 6.8, `amoeba_cli` invoked `InvertedIndex` and `SearchEngine` directly, resulting in fine-grained supporting AST elements (such as `Call`, `Include`, `Attribute`, etc.) surfacing as independent top-level search results.

In Phase 6.8:
- `apps/cli/main.cpp` consumes `PrimaryRetrievalPipeline`.
- Top-level search results are exclusively primary architectural symbols (`Class`, `Function`, `Method`, `Component`, `Hook`, `Route`, `Struct`, `Interface`).
- Fine-grained supporting AST nodes are presented cleanly as nested context/evidence under their owning primary unit.
- All CLI search modes (`code_aware`, `bm25`, `baseline`, `semantic`, `hybrid`) map directly onto `PrimarySearchOptions`.

---

## 2. Architecture & Data Flow

```text
                               CLI (amoeba_cli search)
                                          │
                                          ▼
                             RepositoryScanner & SourceParser
                                          │
                        ┌─────────────────┴─────────────────┐
                        ▼                                   ▼
                  InvertedIndex                  std::vector<ParsedFile>
                        │                                   │
                        └─────────────────┬─────────────────┘
                                          │
                                          ▼
                              PrimaryRetrievalPipeline
                        ├── QueryUnderstanding
                        ├── Lexical Retrieval (SearchEngine)
                        ├── Semantic Retrieval (SemanticIndex)
                        └── Intent-Adaptive Hybrid Fusion
                                          │
                                          ▼
                            std::vector<PrimarySearchResult>
                                          │
                        ┌─────────────────┴─────────────────┐
                        ▼                                   ▼
                 Primary Unit Header               Supporting Evidence
           [1] Class: QueryUnderstanding             - Call: normalize_text
               score: 1.0, provenance: hybrid        - Call: collapse_identifier
               File: path:line:col (C++)             - ...
```

---

## 3. CLI Search Modes & Options Mapping

| CLI Argument | Mode Identifier | `alpha` | `lexical_ranker` | `adaptive_fusion` | Description |
|---|---|---|---|---|---|
| `code_aware` / `--ranker=code_aware` | `CodeAware` | `1.0` | `RankerType::CodeAware` | `false` | Code-aware lexical ranking over primary units |
| `bm25` / `--ranker=bm25` | `BM25` | `1.0` | `RankerType::BM25` | `false` | Standard BM25 lexical ranking over primary units |
| `baseline` / `--ranker=baseline` | `Baseline` | `1.0` | `RankerType::Baseline` | `false` | Term-frequency lexical ranking over primary units |
| `semantic` / `--ranker=semantic` | `Semantic` | `0.0` | N/A | `false` | Dense MiniLM embedding semantic retrieval |
| `hybrid` / `--ranker=hybrid` | `Hybrid` | `0.5` | `RankerType::CodeAware` | `true` | Intent-adaptive fusion of lexical + semantic results |

---

## 4. Output Presentation

Search results display primary architectural units at the top level, with provenance and score, followed by nested supporting evidence:

```text
Search results (10 primary matches found in 3326 us):

[1] Class: QueryUnderstanding (in amoeba::retrieval) [score: 1, provenance: hybrid]
    File: D:\amoeba\engine\include\amoeba\retrieval\query_understanding.hpp:61:1 (C++)

[2] Method: QueryUnderstanding::normalize_text (in amoeba::retrieval) [score: 0.4, provenance: hybrid]
    File: D:\amoeba\engine\src\retrieval\query_understanding.cpp:27:1 (C++)
    Supporting evidence:
      - Call: empty (Line 28)
      - Call: reserve (Line 33)
      - Call: std::isspace (Line 37)
      - Call: push_back (Line 39)
```

---

## 5. Verification & Test Coverage

- **Suite**: `CLIRetrievalIntegrationTest` in `engine/tests/retrieval/cli_retrieval_integration_test.cpp`
- **Tests Added**:
  1. `ExactPrimaryIdentifierAppearsAsPrimaryResult`
  2. `SpacedCompoundQueryMatchesPrimaryRetrievalPipeline`
  3. `SupportingElementsNeverAppearAsTopLevelResults`
  4. `MultipleSupportingHitsCollapseIntoOnePrimaryResult`
  5. `DeterministicOrderingAcrossRuns`
- **Full Test Suite Status**: 283/283 tests passing across 47 test suites.
- **Code Quality**: Clean build with 0 compiler warnings, 100% `clang-format` compliance.
