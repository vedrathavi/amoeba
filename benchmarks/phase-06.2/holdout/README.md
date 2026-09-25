# Phase 6.2 Real-World External Holdout Evaluation

**REAL-WORLD EXTERNAL HOLDOUT — FROZEN VALIDATION EXPERIMENT**

---

## Overview

This directory contains the experimental evaluation artifacts for testing Amoeba's retrieval subsystem against an external real-world React/Next.js codebase:
* **Corpus**: [`demo_test_projects/calendar`](file:///d:/amoeba/demo_test_projects/calendar) (27 source files, 2,820 extracted CodeElements, 2,255 unique terms, 50,471 postings)
* **Dataset Policy**: Strictly frozen holdout — zero tuning, zero formula modifications, zero query modifications.
* **Model**: `all-MiniLM-L6-v2` (384-dimensional dense semantic embeddings).

---

## Directory Contents

```text
benchmarks/phase-06.2/holdout/
├── data/
│   ├── holdout_elements.json             # 2,820 CodeElements dumped directly from Amoeba C++ parser
│   ├── real-world-holdout-results.json   # Machine-readable aggregate metrics & per-query breakdowns
│   ├── real-world-holdout-results.csv    # Comparative metric table across 7 retrieval methods
│   └── real-world-holdout-per-query.csv  # Detailed per-query results, top symbols, ranks, and human relevance
│
├── plots/
│   ├── P6.2-H01-method-quality-comparison.png      # Method comparison: P@1, MRR, NDCG@5
│   ├── P6.2-H02-per-query-method-comparison.png     # Target rank per query across key methods
│   ├── P6.2-H03-category-quality.png               # Category-level P@1 breakdown
│   └── P6.2-H04-latency-comparison.png              # Query latency comparison (ms, CPU)
│
└── README.md
```

---

## Summary of Results (Descriptive, N=10 Queries)

| Method | P@1 | P@3 | P@5 | Recall@5 | Recall@10 | MRR | NDCG@5 | NDCG@10 | Latency (CPU) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Baseline Lexical** | 0.10 | 0.100 | 0.100 | 0.300 | 0.433 | 0.249 | 0.195 | 0.248 | $0.35\text{ ms}$ |
| **BM25** | **0.20** | 0.133 | 0.100 | 0.317 | 0.533 | **0.359** | **0.269** | **0.361** | $0.32\text{ ms}$ |
| **CodeAware** | 0.10 | 0.100 | 0.080 | 0.267 | 0.533 | 0.253 | 0.178 | 0.290 | $0.24\text{ ms}$ |
| **Semantic (all-MiniLM-L6-v2)** | **0.20** | 0.133 | 0.080 | 0.233 | 0.300 | 0.319 | 0.210 | 0.241 | $12.94\text{ ms}$ |
| **Hybrid ($\alpha=0.5$ Weighted)** | 0.10 | 0.133 | 0.120 | 0.367 | 0.400 | 0.276 | 0.236 | 0.251 | $13.21\text{ ms}$ |
| **Hybrid ($\alpha=0.3$ Weighted)** | 0.10 | 0.133 | 0.080 | 0.233 | 0.300 | 0.277 | 0.187 | 0.217 | $13.20\text{ ms}$ |
| **Hybrid (RRF $k=60$)** | 0.10 | 0.100 | 0.080 | 0.283 | 0.400 | 0.244 | 0.179 | 0.230 | $13.21\text{ ms}$ |

For full architectural analysis and research questions Q1–Q8, see [`docs/experiments/phase-06.2-real-world-holdout.md`](file:///d:/amoeba/docs/experiments/phase-06.2-real-world-holdout.md).
