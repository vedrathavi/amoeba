# Phase 4.0 — Ranking Foundation & Baseline Implementation

## 1. Overview & Objective

Phase 4.0 marks the transition of Amoeba from purely binary boolean candidate retrieval ("what matches the query?") to relevance-ordered result presentation ("what is most relevant to the developer's intent?").

This phase introduces:
1. Architectural separation of Candidate Retrieval from Candidate Ranking.
2. In-memory collection statistics tracking in [InvertedIndex](file:///d:/amoeba/engine/include/amoeba/index/inverted_index.hpp).
3. The [BaselineRanker](file:///d:/amoeba/engine/include/amoeba/rank/baseline_ranker.hpp) implementing a deterministic baseline heuristic scoring model.
4. Comprehensive ranking test suite validating score monotonicity, tie-breaking, and cross-language consistency.

---

## 2. Core Concepts: TF, DF, and IDF

While a standalone ad-hoc TF-IDF engine is not implemented as a production algorithm (Phase 4.1 establishes Okapi BM25), the foundational IR concepts were formalized:

* **Term Frequency ($TF(t, d)$)**: The count of occurrences of term $t$ in document/element $d$. In code search, an identifier appearing multiple times or across multiple structural fields indicates higher relevance.
* **Document Frequency ($DF(t)$)**: The number of documents/elements in the corpus containing term $t$. Common terms (e.g. `get`, `string`, `index`) have high $DF$, while domain terms (e.g. `RepositoryScanner`) have low $DF$.
* **Inverse Document Frequency ($IDF(t)$)**: A logarithmic measure of term specificity:
  $$\text{IDF}(t) = \ln\left(1 + \frac{N - DF(t) + 0.5}{DF(t) + 0.5}\right)$$
  IDF provides principled down-weighting of frequent boilerplate tokens without hand-crafted stopword lists.

---

## 3. Searchable Document Representation in Ranking

In Amoeba, the atomic unit of retrieval and ranking is the [CodeElement](file:///d:/amoeba/engine/include/amoeba/parser/code_element.hpp), not the entire source file. Each element encapsulates rich semantic properties:
- `name`: Identifier token (e.g. class name, function name, variable name)
- `kind`: Structural classification (`Function`, `Class`, `Method`, `Route`, `Interface`, etc.)
- `parent_name`: Enclosing scope or class hierarchy
- `path`: File location and directory structure
- `doc_comment`: Documentation strings and inline commentary
- `source_code`: Raw implementation body

---

## 4. Baseline Ranking Heuristic Formulation

The [BaselineRanker](file:///d:/amoeba/engine/include/amoeba/rank/baseline_ranker.hpp) computes a composite score $S_{\text{baseline}}(e, Q)$ for element $e$ and query terms $Q$:

$$S_{\text{baseline}}(e, Q) = S_{\text{identifier}} + S_{\text{field\_coverage}} + S_{\text{query\_coverage}} + S_{\text{kind\_bias}}$$

Where:
1. **Identifier Exact Match Bonus**: $+100.0$ if normalized element name matches query string exactly.
2. **Identifier Substring/Prefix Bonus**: $+25.0$ if query is a prefix or whole-word subword of element name.
3. **Structural Field Occurrences**:
   - Element Name term match: $+10.0$ per term occurrence
   - Parent Name term match: $+5.0$ per term occurrence
   - File Path term match: $+3.0$ per term occurrence
   - Doc Comment term match: $+1.5$ per term occurrence
   - Signature / Body term match: $+1.0$ per term occurrence
4. **Query Term Coverage**:
   $$\text{Coverage}(Q) = \frac{|\{t \in Q \mid t \in \text{Terms}(e)\}|}{|Q|} \times 20.0$$
5. **Deterministic Tie-Breaking**: Elements with identical scores are ordered by file path ascending, then start line ascending, and finally element ID ascending.

---

## 5. Architectural Verification & Results

- Verified on 12 programming languages and web frameworks (C, C++, Python, Java, Go, Rust, JS, TS, TSX/React, HTML, CSS, Next.js).
- 100% test pass rate across unit tests and integration pipelines.
