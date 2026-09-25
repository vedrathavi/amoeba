# Phase 6.3 Benchmark Artifacts & Research Plots

This directory contains experimental benchmark data, diagnostic metrics, per-query rank progression, and publication-grade evaluation plots for **Amoeba Phase 6.3: Hierarchical Retrieval Units & Supporting Evidence**.

---

## Directory Structure

```text
benchmarks/phase-06.3/
├── data/
│   ├── phase-06.3-results.json      # Complete experimental metrics & diagnostic data
│   ├── phase-06.3-results.csv       # Aggregate ranking metrics across 7 retrieval methods
│   └── phase-06.3-per-query.csv     # Granular query-level ranking, target symbols, and evidence
│
├── plots/
│   ├── P6.3-G01-before-vs-after-quality.png      # Before vs After Phase 6.3 P@1 and MRR
│   ├── P6.3-G02-primary-top1-distribution.png     # Primary vs Distractor Top-1 Result Composition
│   ├── P6.3-G03-per-query-rank-progression.png    # Per-query rank progression
│   ├── P6.3-G04-latency-comparison.png            # End-to-end query latency by retrieval pipeline
│   └── P6.3-G05-diagnostic-metrics.png            # Structural vs retrieval representation footprint
│
├── manifests/
│   └── phase-06.3-plot-manifest.json # Complete metadata schema for each generated plot
│
└── README.md
```

---

## Reproducing the Benchmark & Research Plots

```bash
python benchmarks/scripts/evaluate_phase_06_3.py
```

---

## Summary of Results (Holdout N=10 Queries)

| Method | P@1 | P@3 | P@5 | Recall@5 | Recall@10 | MRR | NDCG@5 | NDCG@10 | Latency (CPU) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Baseline Lexical** | 0.20 | 0.233 | 0.180 | 0.518 | 0.677 | 0.423 | 0.375 | 0.419 | $0.07\text{ ms}$ |
| **BM25** | 0.50 | 0.300 | 0.220 | 0.668 | 0.718 | 0.622 | 0.572 | 0.572 | $0.04\text{ ms}$ |
| **CodeAware** | 0.30 | 0.267 | 0.220 | 0.627 | 0.677 | 0.487 | 0.483 | 0.479 | $0.03\text{ ms}$ |
| **Semantic (all-MiniLM-L6-v2)** | 0.40 | 0.367 | 0.260 | 0.636 | **0.945** | 0.602 | 0.554 | 0.634 | $9.81\text{ ms}$ |
| **Hybrid ($\alpha=0.5$ Weighted)** | **0.70** | **0.367** | **0.280** | **0.736** | 0.895 | **0.768** | **0.702** | **0.727** | $9.86\text{ ms}$ |
| **Hybrid ($\alpha=0.3$ Weighted)** | 0.60 | 0.367 | 0.300 | 0.786 | 0.905 | 0.731 | 0.692 | 0.706 | $9.85\text{ ms}$ |
| **Hybrid (RRF $k=60$)** | 0.60 | 0.267 | 0.260 | 0.768 | 0.918 | 0.717 | 0.648 | 0.683 | $9.86\text{ ms}$ |

For full architectural documentation, see [`docs/phases/phase-06.3-hierarchical-retrieval-units.md`](file:///d:/amoeba/docs/phases/phase-06.3-hierarchical-retrieval-units.md).
