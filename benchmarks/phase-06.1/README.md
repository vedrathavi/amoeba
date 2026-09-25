# Phase 6.1 Benchmark Artifacts & Research Plots

This directory contains experimental benchmark data, research-quality evaluation plots, and metadata manifests for **Amoeba Phase 6.1: Real Code Embeddings & Semantic Retrieval**.

---

## Directory Structure

```text
benchmarks/phase-06.1/
├── data/
│   ├── phase-06.1-results.json      # Complete machine-readable experimental records & metrics
│   └── phase-06.1-results.csv       # Tabular comparative metric & performance summary
│
├── plots/
│   ├── P6.1-G01-p01-method-comparison.png             # Precision@1 on Phase 6.1 Validation Set
│   ├── P6.1-G02-ranking-quality-comparison.png        # MRR, NDCG@5, NDCG@10 comparison
│   ├── P6.1-G03-category-quality.png                  # Quality by Category (1 Query per Category)
│   ├── P6.1-G04-retrieval-latency.png                 # Index retrieval latency across methods
│   ├── P6.1-G05-embedding-latency.png                 # Neural embedding generation latency
│   ├── P6.1-G06-vector-memory-scaling.png             # Vector index memory footprint scaling
│   └── P6.1-G07-corpus-size-vs-retrieval-latency.png  # Measured scan latency vs. corpus size
│
├── manifests/
│   └── phase-06.1-plot-manifest.json # Complete metadata schema for each generated plot
│
└── README.md                         # Reproduction instructions and documentation
```

---

## Reproducing the Benchmarks & Research Plots

### Prerequisites
- Python 3.10+
- `sentence-transformers`, `torch`, `numpy`, `matplotlib`

### Step 1: Run Benchmark Evaluation & Data Export
Execute the evaluation harness to run the standardized validation queries against lexical and pretrained semantic models (`all-MiniLM-L6-v2`):

```bash
python benchmarks/scripts/generate_phase_06_1_data.py
```

This exports:
- `benchmarks/phase-06.1/data/phase-06.1-results.json`
- `benchmarks/phase-06.1/data/phase-06.1-results.csv`

### Step 2: Render Research Plots
Execute the plot generation script to produce publication-grade 300 DPI graphs:

```bash
python benchmarks/scripts/plot_phase_06_1.py
```

This updates all plots in `benchmarks/phase-06.1/plots/`.

---

## Summary of Research Graphs

| ID | Title | Metric / Chart | Key Observation |
| :--- | :--- | :--- | :--- |
| **P6.1-G01** | Retrieval Precision@1 on Validation Set | Method vs. P@1 (N=10) | Semantic correctly handles the single conceptual query while matching lexical on others. |
| **P6.1-G02** | Ranking Quality Across Methods | Method vs. MRR / NDCG | Semantic retrieval achieves 1.00 MRR and 0.997 NDCG on the validation set. |
| **P6.1-G03** | Category Quality (1 Query per Category) | 10 Categories vs. P@1 | Lexical succeeds on exact/identifier queries (1.00) but fails on conceptual (0.00). Semantic succeeds on both. |
| **P6.1-G04** | Retrieval Latency | Method vs. Latency (ms) | Sub-millisecond index lookup times across lexical and semantic cosine scans. |
| **P6.1-G05** | Pretrained Embedding Latency | Item Type vs. Latency (ms) | 11.57ms single element; 9.68ms single query; 40.20ms for 50-item batch (~0.80ms/item). |
| **P6.1-G06** | Vector Memory Scaling | Vector Count vs. Memory (MB) | Linear scaling: 1.55 MB for 1,000 vectors; 77.3 MB for 50,000 vectors. |
| **P6.1-G07** | Measured Brute-Force Retrieval Latency | Vector Count vs. Latency (ms) | Scan latency remained below 6.4 ms on 50,000 vectors on CPU, below the 10ms interactive budget. |

For detailed engineering analysis and architectural documentation, see [`docs/phases/phase-06.1-real-code-embeddings.md`](file:///d:/amoeba/docs/phases/phase-06.1-real-code-embeddings.md).
