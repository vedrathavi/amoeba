# Phase 3.2 — Basic Retrieval & Query Engine

## 1. Objective

Provide a high-level query engine (`SearchEngine`) capable of evaluating single-term and multi-term queries against an `InvertedIndex` and producing rich `SearchResult` objects containing the matching `CodeElement`, `file_path`, and source location.

---

## 2. Match Modes

* **`MatchMode::AnyTerm` (OR)**: Collects candidate elements matching any of the tokens in the query.
* **`MatchMode::AllTerms` (AND)**: Requires candidate elements to match all tokens in the query.

---

## 3. Search Result Model

```cpp
struct SearchResult {
    parser::CodeElement element;
    path file_path;
    string language;
    uint32_t match_count{0};
    bool exact_name_match{false};
};
```

---

## 4. Deterministic Ordering

Matches are sorted deterministically:
1. Exact identifier matches first (`exact_name_match == true`).
2. Elements matching more query terms (`match_count` descending).
3. File path alphabetical order.
4. Source line number ascending.
