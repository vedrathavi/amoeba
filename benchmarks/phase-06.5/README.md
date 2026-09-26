# Phase 6.5 — Native Retrieval Evaluation & Hardening

**REAL-WORLD EXTERNAL HOLDOUT — NATIVE C++ RETRIEVAL BENCHMARK**

---

## 1. Overview

This directory contains the experimental evaluation artifacts for testing Amoeba's native C++ retrieval subsystem against the external real-world React/Next.js codebase:
* **Corpus**: `demo_test_projects/calendar` (27 source files, 2,820 extracted CodeElements, 2,255 unique terms, 50,471 postings)
* **Dataset Policy**: Strictly frozen holdout — verbatim queries, zero heuristic tuning, zero weight modifications.
* **Model**: `all-MiniLM-L6-v2` (384-dimensional dense semantic embeddings via native C++ `PretrainedEmbeddingProvider`).
* **Executable**: Native C++ binary `amoeba_phase_06_5_benchmark` linking production `amoeba_engine`.

---

## 2. Directory Contents

```text
benchmarks/phase-06.5/
├── README.md
├── benchmark_main.cpp               # Native C++ evaluation entry point
├── queries.json                     # 10 frozen evaluation queries with ground truth metadata
└── results/
    ├── raw_code_elements.json       # Machine-readable aggregate metrics for BEFORE architecture
    ├── retrieval_units.json         # Machine-readable aggregate metrics for AFTER architecture
    ├── comparison.csv               # Direct comparison table across 14 retrieval methods
    └── per_query.json               # Detailed per-query results, top symbols, ranks, and evidence
```

---

## 3. Summary of Results (N=10 Frozen Queries)

| Architecture | Method | P@1 | P@3 | P@5 | Recall@5 | Recall@10 | MRR | NDCG@5 | NDCG@10 | Latency (CPU) |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **BEFORE** | Raw Baseline | 0.400 | 0.367 | 0.300 | 1.400 | 1.800 | 0.558 | 0.916 | 1.067 | 33.6 ms |
| **BEFORE** | Raw BM25 | 0.100 | 0.100 | 0.100 | 0.350 | 0.450 | 0.240 | 0.252 | 0.282 | 34.0 ms |
| **BEFORE** | Raw CodeAware | 0.600 | 0.400 | 0.320 | 1.300 | 1.950 | 0.652 | 0.950 | 1.170 | 34.9 ms |
| **BEFORE** | Raw Semantic (MiniLM-384) | 0.100 | 0.100 | 0.100 | 0.400 | 0.400 | 0.231 | 0.269 | 0.269 | 11.1 ms |
| **BEFORE** | Raw Hybrid ($\alpha=0.5$) | 0.200 | 0.267 | 0.220 | 0.950 | 1.450 | 0.436 | 0.595 | 0.769 | 50.5 ms |
| **BEFORE** | Raw Hybrid ($\alpha=0.3$) | 0.100 | 0.133 | 0.140 | 0.600 | 1.150 | 0.284 | 0.357 | 0.535 | 51.2 ms |
| **BEFORE** | Raw Hybrid RRF | 0.400 | 0.133 | 0.100 | 0.400 | 0.450 | 0.449 | 0.361 | 0.383 | 50.8 ms |
| **AFTER** | Unit Baseline | 0.400 | 0.233 | 0.200 | 0.800 | 0.800 | 0.553 | 0.607 | 0.607 | 36.8 ms |
| **AFTER** | **Unit BM25** | **0.600** | 0.233 | 0.200 | 0.900 | 0.900 | **0.676** | **0.725** | **0.725** | 39.8 ms |
| **AFTER** | **Unit CodeAware** | **0.600** | **0.300** | **0.200** | 0.800 | 0.900 | **0.679** | 0.685 | 0.717 | 42.3 ms |
| **AFTER** | **Unit Semantic (MiniLM-384)**| **0.300** | **0.200** | 0.120 | 0.500 | 0.500 | **0.381** | 0.392 | 0.392 | 41.3 ms |
| **AFTER** | Unit Hybrid ($\alpha=0.5$) | 0.400 | 0.267 | 0.160 | 0.700 | 0.700 | 0.550 | 0.568 | 0.568 | 41.2 ms |
| **AFTER** | Unit Hybrid ($\alpha=0.3$) | 0.300 | 0.267 | 0.160 | 0.700 | 0.700 | 0.498 | 0.531 | 0.531 | 48.3 ms |
| **AFTER** | Unit Hybrid RRF | 0.400 | 0.267 | 0.160 | 0.700 | 0.850 | 0.568 | 0.568 | 0.622 | 42.5 ms |

---

## 4. Key Findings

1. **Massive BM25 Improvement**: BM25 P@1 increased from 0.100 to 0.600 (+500% relative) and MRR from 0.240 to 0.676 (+0.436). Eliminating fine-grained AST elements (`className`, `Attributes`, `Calls`) from competing as documents prevented IDF score dilution and keyword pollution.
2. **Semantic Retrieval Boost**: Semantic P@1 increased from 0.100 to 0.300 (+200% relative) and MRR from 0.231 to 0.381. Enriched semantic representation (`format_unit`) allows natural language and contextual queries to match primary units via their attached supporting evidence.
3. **Supporting Evidence Elimination of Result Pollution**: On queries like Q1 (`CalendarGrid`), Q5 (`formatDate`), and Q10 (`CalendarDay`), supporting elements that previously stole the #1 rank were collapsed into their owning primary symbols.
4. **Representation Reduction**: 2,820 raw CodeElements reduced to 183 primary RetrievalUnits (**93.5% reduction** in candidate search space).
