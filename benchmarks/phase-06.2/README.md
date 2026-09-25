# Phase 6.2 Benchmark Artifacts & Research Plots

This directory contains experimental benchmark data, research-quality evaluation plots, per-query win/loss tables, and metadata manifests for **Amoeba Phase 6.2: Hybrid Lexical + Semantic Retrieval**.

---

## Directory Structure

```text
benchmarks/phase-06.2/
├── data/
│   ├── phase-06.2-results.json      # Complete machine-readable experimental metrics & configuration
│   ├── phase-06.2-results.csv       # Aggregate metric summary across evaluated retrieval methods
│   ├── phase-06.2-per-query.csv     # Granular query-level ranking, scores, and win/loss classification
│   └── phase-06.2-weight-sweep.csv  # Alpha sweep records from α=0.0 (semantic) to α=1.0 (lexical)
│
├── plots/
│   ├── P6.2-G01-fusion-weight-vs-quality.png       # Fusion weight α vs P@1, MRR, NDCG@5
│   ├── P6.2-G02-method-quality-comparison.png      # Method comparison: Baseline, BM25, CodeAware, Semantic, Hybrid
│   ├── P6.2-G03-category-quality-comparison.png    # Category breakdown (1 Query per Category)
│   ├── P6.2-G04-query-win-loss.png                 # Outcome classification (Improved, Preserved, Regressed)
│   ├── P6.2-G05-latency-comparison.png             # End-to-end query latency by retrieval pipeline
│   └── P6.2-G06-candidate-depth-analysis.png       # Candidate pool depth vs ranking quality
│
├── manifests/
│   └── phase-06.2-plot-manifest.json # Complete metadata schema for each generated plot
│
└── README.md                         # Reproduction guide and plot catalog
```

---

## Reproducing the Benchmarks & Research Plots

### Prerequisites
- Python 3.10+
- `sentence-transformers`, `torch`, `numpy`, `matplotlib`

### Step 1: Run Hybrid Benchmark & Data Export
Execute the hybrid evaluation script to run lexical and semantic retrievers across all fusion weights, candidate depths, and query categories:

```bash
python benchmarks/scripts/generate_phase_06_2_data.py
```

This exports all JSON and CSV data files into `benchmarks/phase-06.2/data/`.

### Step 2: Render Research Plots
Execute the plot generation script to produce publication-grade 300 DPI graphs:

```bash
python benchmarks/scripts/plot_phase_06_2.py
```

This updates all plots in `benchmarks/phase-06.2/plots/`.

---

## Summary of Research Graphs

| ID | Title | Metric / Chart | Key Observation |
| :--- | :--- | :--- | :--- |
| **P6.2-G01** | Fusion Weight α vs. Retrieval Quality | α vs. P@1, MRR, NDCG@5 | Quality remains stable at P@1: 1.00 for α in [0.0, 0.8], dropping to 0.90 at α=1.0 (pure lexical). |
| **P6.2-G02** | Ranking Quality Across Methods | Method vs. P@1, MRR, NDCG | Hybrid matches semantic at P@1: 1.00 and MRR: 1.00 while preserving syntactic token precision. |
| **P6.2-G03** | Category Quality (1 Query per Category) | 10 Categories vs. P@1 | Hybrid preserves 1.00 across all 9 lexical categories while resolving the conceptual query miss. |
| **P6.2-G04** | Query Outcome Classification | Outcome vs. Count | 9 queries preserved (90%), 1 query improved (10%), 0 queries regressed (0%). |
| **P6.2-G05** | Latency Comparison by Pipeline | Pipeline vs. Latency (ms) | Lexical: 0.08ms; Semantic: 9.69ms; Hybrid: 9.80ms (dominated by query embedding inference). |
| **P6.2-G06** | Candidate Depth vs. Quality | Depth K vs. NDCG@5 / MRR | Top-5 semantic candidate depth is sufficient to capture relevant entities on this validation suite. |

For complete architectural details and research analysis, see [`docs/phases/phase-06.2-hybrid-retrieval.md`](file:///d:/amoeba/docs/phases/phase-06.2-hybrid-retrieval.md).
