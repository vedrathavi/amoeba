# Phase 4.1 — Okapi BM25 Lexical Ranking

## 1. Overview & Motivation

Phase 4.1 establishes Okapi BM25 as Amoeba's classical probabilistic information retrieval ranker.

BM25 addresses key limitations of naive term frequency heuristics:
1. **Term Frequency Saturation**: Non-linear sublinear scaling via hyperparameter $k_1 \in [1.2, 2.0]$. Repeated occurrences of a term provide diminishing marginal relevance gains.
2. **Document Length Normalization**: Penalization of excessively long elements and normalization against corpus average element length ($\text{avgdl}$) via hyperparameter $b \in [0.5, 0.75]$.
3. **Probabilistic IDF**: Robertson-Spärck Jones IDF formula providing natural term weighting based on collection-wide rarity.

---

## 2. Mathematical Formulation

For query $Q = \{q_1, q_2, \dots, q_n\}$ and code element document $D$ with length $|D| = \sum_{t} TF(t, D)$, the Okapi BM25 score is computed as:

$$\text{BM25}(D, Q) = \sum_{i=1}^n \text{IDF}(q_i) \cdot \frac{TF(q_i, D) \cdot (k_1 + 1)}{TF(q_i, D) + k_1 \cdot \left(1 - b + b \cdot \frac{|D|}{\text{avgdl}}\right)}$$

Where:
* **Robertson-Spärck Jones IDF (with floor safeguard $\epsilon = 0.01$)**:
  $$\text{IDF}(q_i) = \max\left(\epsilon, \ln\left(1 + \frac{N - DF(q_i) + 0.5}{DF(q_i) + 0.5}\right)\right)$$
* **Default Hyperparameters**:
  - $k_1 = 1.2$ (controls TF saturation rate)
  - $b = 0.75$ (controls degree of document length normalization)
  - $\text{avgdl} = \frac{\sum_{D \in \mathcal{C}} |D|}{N}$ (average searchable token count across all indexed elements)

---

## 3. Architecture & Integration

* [BM25Ranker](file:///d:/amoeba/engine/include/amoeba/rank/bm25_ranker.hpp) implements a stateless scoring interface accepting candidate matches and corpus statistics.
* [CorpusStats](file:///d:/amoeba/engine/include/amoeba/rank/bm25_ranker.hpp) tracks $N$ (total elements), $\text{avgdl}$ (average element length), and term document frequencies $DF(t)$.
* Full parameterization via `BM25Params` struct allowing custom $k_1$, $b$, and IDF flooring during benchmarks and tuning.

---

## 4. Verification Suite

1. **Analytical Hand Calculations**: Unit tests validating exact numerical equivalence against theoretical hand calculations.
2. **TF Saturation Tests**: Asserting that doubling $TF$ from 5 to 10 yields a smaller incremental delta than from 1 to 2.
3. **Length Normalization Tests**: Asserting that shorter elements with equal $TF$ achieve higher BM25 density scores than verbose implementations.
