# Phase 4.3 — Ranking Architecture, LLD & Replaceability Audit

## 1. Architectural Architecture & Dependency Flow

Phase 4 establishes clean modular boundaries between index retrieval and ranking strategies:

```text
Query String
     │
     ▼
CodeTokenizer (Normalization & CamelCase / SnakeCase splitting)
     │
     ▼
InvertedIndex (Candidate Retrieval: Any-term or All-term postings union/intersection)
     │
     ▼
std::vector<CandidateMatch> (Unranked candidate elements with match metadata)
     │
     ▼
Ranker Strategy (BaselineRanker | BM25Ranker | CodeAwareRanker)
     │
     ▼
std::vector<SearchResult> (Deterministic score ordering & tie-breaking)
```

---

## 2. Low-Level Design (LLD) Components

### 2.1 CandidateMatch
[CandidateMatch](file:///d:/amoeba/engine/include/amoeba/rank/candidate_match.hpp) encapsulates the retrieved element reference alongside token matching metadata:
- `element_id`: Unique index element identifier
- `matched_terms`: Set of distinct query tokens present in this element
- `term_frequencies`: Map/vector of term occurrence counts within element fields
- `element_length`: Total searchable token count for document length normalization

### 2.2 CorpusStats
[CorpusStats](file:///d:/amoeba/engine/include/amoeba/rank/bm25_ranker.hpp) provides corpus-wide summary statistics needed by probabilistic rankers:
- `total_elements`: Total number of indexed CodeElements ($N$)
- `total_token_count`: Sum of all element token lengths
- `avg_element_length`: Average searchable token length ($\text{avgdl}$)
- `doc_frequencies`: Collection document frequencies $DF(t)$ per term

### 2.3 SearchEngine Orchestration
[SearchEngine](file:///d:/amoeba/engine/include/amoeba/index/search_engine.hpp) coordinates candidate retrieval and delegates scoring to the configured `RankerType` enum (`RankerType::Baseline`, `RankerType::BM25`, `RankerType::CodeAware`).

---

## 3. Extensibility and Replaceability Verification

1. **Pluggable Rankers**: New ranking algorithms (e.g. TF-IDF variants, semantic neural rankers, candidate fusion engines) can be added by implementing the ranking signature without modifying the underlying inverted index or tokenizer.
2. **Deterministic Stability**: All ranking operations guarantee reproducible ordering using deterministic tie-breaking rules.
