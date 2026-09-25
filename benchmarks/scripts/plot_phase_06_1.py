"""
Phase 6.1 Research Plot Generator (Refined Research-Quality Pass)
Consumes benchmarks/phase-06.1/data/phase-06.1-results.json and renders 7 publication-quality graphs:
  - P6.1-G01-p01-method-comparison.png
  - P6.1-G02-ranking-quality-comparison.png
  - P6.1-G03-category-quality.png
  - P6.1-G04-retrieval-latency.png
  - P6.1-G05-embedding-latency.png
  - P6.1-G06-vector-memory-scaling.png
  - P6.1-G07-corpus-size-vs-retrieval-latency.png
"""

import os
import json
import matplotlib.pyplot as plt
import numpy as np

# Global styling configuration
plt.rcParams.update({
    "font.family": "sans-serif",
    "font.sans-serif": ["DejaVu Sans", "Helvetica", "Arial"],
    "font.size": 10,
    "axes.labelsize": 11,
    "axes.titlesize": 12.5,
    "xtick.labelsize": 9.5,
    "ytick.labelsize": 9.5,
    "legend.fontsize": 9.5,
    "figure.titlesize": 13.5,
    "grid.color": "#e2e8f0",
    "grid.linestyle": "--",
    "grid.alpha": 0.7,
    "axes.edgecolor": "#cbd5e1",
    "axes.linewidth": 1.0
})

COLORS = {
    "Baseline Lexical": "#94a3b8",
    "BM25": "#3b82f6",
    "CodeAware": "#10b981",
    "Semantic (all-MiniLM-L6-v2)": "#8b5cf6",
    "Single": "#6366f1",
    "Batch": "#06b6d4",
    "Query": "#ec4899",
    "Memory": "#0284c7",
    "Latency": "#f43f5e"
}

def load_data(json_path):
    with open(json_path, "r", encoding="utf-8") as f:
        return json.load(f)

def plot_g01(data, output_dir):
    """P6.1-G01 — Retrieval Precision@1 on Phase 6.1 Validation Set"""
    methods = list(data["methods"].keys())
    p1_scores = [data["methods"][m]["metrics"]["P@1"] for m in methods]
    bar_colors = [COLORS.get(m, "#475569") for m in methods]

    fig, ax = plt.subplots(figsize=(8, 5), dpi=300)
    bars = ax.bar(methods, p1_scores, color=bar_colors, width=0.55, edgecolor="#334155", linewidth=0.8)
    
    ax.set_title("P6.1-G01: Retrieval Precision@1 on Phase 6.1 Validation Set (N=10 queries)", pad=15, fontweight="bold")
    ax.set_ylabel("Precision@1 (P@1)")
    ax.set_xlabel("Retrieval Method")
    ax.set_ylim(0, 1.15)
    ax.grid(axis="y")

    for bar in bars:
        h = bar.get_height()
        ax.annotate(f"{h:.2f}",
                    xy=(bar.get_x() + bar.get_width() / 2, h),
                    xytext=(0, 4), textcoords="offset points",
                    ha="center", va="bottom", fontweight="bold", fontsize=10)

    plt.tight_layout()
    out_path = os.path.join(output_dir, "P6.1-G01-p01-method-comparison.png")
    plt.savefig(out_path)
    plt.close()
    print(f"Rendered: {out_path}")

def plot_g02(data, output_dir):
    """P6.1-G02 — Ranking Quality Comparison on Phase 6.1 Validation Set"""
    methods = list(data["methods"].keys())
    mrr = [data["methods"][m]["metrics"]["MRR"] for m in methods]
    ndcg5 = [data["methods"][m]["metrics"]["NDCG@5"] for m in methods]
    ndcg10 = [data["methods"][m]["metrics"]["NDCG@10"] for m in methods]

    x = np.arange(len(methods))
    width = 0.25

    fig, ax = plt.subplots(figsize=(10, 5.5), dpi=300)
    rects1 = ax.bar(x - width, mrr, width, label="MRR", color="#6366f1", edgecolor="#334155", linewidth=0.8)
    rects2 = ax.bar(x, ndcg5, width, label="NDCG@5", color="#10b981", edgecolor="#334155", linewidth=0.8)
    rects3 = ax.bar(x + width, ndcg10, width, label="NDCG@10", color="#f59e0b", edgecolor="#334155", linewidth=0.8)

    ax.set_title("P6.1-G02: Ranking Quality Across Retrieval Methods (N=10 queries)", pad=15, fontweight="bold")
    ax.set_ylabel("Score")
    ax.set_xticks(x)
    ax.set_xticklabels(methods)
    ax.set_ylim(0, 1.15)
    ax.legend(frameon=True, facecolor="white", edgecolor="#cbd5e1")
    ax.grid(axis="y")

    def autolabel(rects):
        for rect in rects:
            height = rect.get_height()
            ax.annotate(f"{height:.2f}",
                        xy=(rect.get_x() + rect.get_width() / 2, height),
                        xytext=(0, 3), textcoords="offset points",
                        ha="center", va="bottom", fontsize=8.5)

    autolabel(rects1)
    autolabel(rects2)
    autolabel(rects3)

    plt.tight_layout()
    out_path = os.path.join(output_dir, "P6.1-G02-ranking-quality-comparison.png")
    plt.savefig(out_path)
    plt.close()
    print(f"Rendered: {out_path}")

def plot_g03(data, output_dir):
    """P6.1-G03 — Retrieval Quality by Query Category (1 Query per Category)"""
    methods = list(data["methods"].keys())
    first_method = methods[0]
    categories = list(data["methods"][first_method]["metrics"]["category_P@1"].keys())
    
    y = np.arange(len(categories))
    height = 0.2

    fig, ax = plt.subplots(figsize=(10.5, 7.2), dpi=300)
    for i, method in enumerate(methods):
        scores = [data["methods"][method]["metrics"]["category_P@1"][cat] for cat in categories]
        ax.barh(y + (i - 1.5) * height, scores, height, label=method, 
                color=COLORS.get(method, "#64748b"), edgecolor="#334155", linewidth=0.8)

    ax.set_title("P6.1-G03: Retrieval Quality (P@1) by Category (1 Query per Category, N=10 total)", pad=15, fontweight="bold")
    ax.set_xlabel("Precision@1 (P@1)")
    ax.set_yticks(y)
    ax.set_yticklabels(categories)
    ax.set_xlim(0, 1.2)
    ax.invert_yaxis()
    ax.legend(loc="lower right", frameon=True, facecolor="white", edgecolor="#cbd5e1")
    ax.grid(axis="x")

    plt.tight_layout()
    out_path = os.path.join(output_dir, "P6.1-G03-category-quality.png")
    plt.savefig(out_path)
    plt.close()
    print(f"Rendered: {out_path}")

def plot_g04(data, output_dir):
    """P6.1-G04 — Retrieval Latency across Methods"""
    methods = list(data["methods"].keys())
    latencies = [data["methods"][m]["retrieval_time_ms"] for m in methods]
    bar_colors = [COLORS.get(m, "#475569") for m in methods]

    fig, ax = plt.subplots(figsize=(8, 5), dpi=300)
    bars = ax.bar(methods, latencies, color=bar_colors, width=0.55, edgecolor="#334155", linewidth=0.8)

    ax.set_title("P6.1-G04: Retrieval Latency by Method (Excluding Query Embedding)", pad=15, fontweight="bold")
    ax.set_ylabel("Retrieval Latency (ms)")
    ax.set_xlabel("Retrieval Method")
    ax.set_ylim(0, max(latencies) * 1.35)
    ax.grid(axis="y")

    for bar in bars:
        h = bar.get_height()
        ax.annotate(f"{h:.3f} ms",
                    xy=(bar.get_x() + bar.get_width() / 2, h),
                    xytext=(0, 4), textcoords="offset points",
                    ha="center", va="bottom", fontweight="bold", fontsize=9.5)

    plt.tight_layout()
    out_path = os.path.join(output_dir, "P6.1-G04-retrieval-latency.png")
    plt.savefig(out_path)
    plt.close()
    print(f"Rendered: {out_path}")

def plot_g05(data, output_dir):
    """P6.1-G05 — Embedding Generation Latency"""
    perf = data["performance_profile"]
    labels = [
        "Single Element\n(1 item)",
        "Query Embedding\n(1 query)",
        "Batch Embedding\n(50 items)"
    ]
    times = [
        perf["single_code_element_embedding_time_ms"],
        perf["query_embedding_time_ms"],
        perf["batch_50_embedding_time_ms"]
    ]
    bar_colors = ["#6366f1", "#ec4899", "#06b6d4"]

    fig, ax = plt.subplots(figsize=(8, 5), dpi=300)
    bars = ax.bar(labels, times, color=bar_colors, width=0.5, edgecolor="#334155", linewidth=0.8)

    ax.set_title("P6.1-G05: Pretrained Model Embedding Latency (all-MiniLM-L6-v2, Median)", pad=15, fontweight="bold")
    ax.set_ylabel("Latency (ms)")
    ax.set_ylim(0, max(times) * 1.25)
    ax.grid(axis="y")

    for bar in bars:
        h = bar.get_height()
        ax.annotate(f"{h:.2f} ms",
                    xy=(bar.get_x() + bar.get_width() / 2, h),
                    xytext=(0, 4), textcoords="offset points",
                    ha="center", va="bottom", fontweight="bold", fontsize=10)

    plt.tight_layout()
    out_path = os.path.join(output_dir, "P6.1-G05-embedding-latency.png")
    plt.savefig(out_path)
    plt.close()
    print(f"Rendered: {out_path}")

def plot_g06(data, output_dir):
    """P6.1-G06 — Vector Memory Scaling"""
    sizes = data["scaling_data"]["corpus_sizes"]
    mem_kb = data["scaling_data"]["memory_usage_kb"]
    mem_mb = [k / 1024.0 for k in mem_kb]

    fig, ax = plt.subplots(figsize=(8.5, 5.2), dpi=300)
    ax.plot(sizes, mem_mb, marker="o", color=COLORS["Memory"], linewidth=2.2, markersize=6, label="Measured Vector Memory (384-d float32 + meta)")

    ax.set_title("P6.1-G06: Vector Store Memory Scaling (384-d Dense Index)", pad=15, fontweight="bold")
    ax.set_xlabel("Number of CodeElement Embeddings")
    ax.set_ylabel("Memory Footprint (MB)")
    ax.grid(True)
    ax.legend(frameon=True, facecolor="white", edgecolor="#cbd5e1")

    for idx in [2, 4, 7]:
        ax.annotate(f"{sizes[idx]:,} vectors\n{mem_mb[idx]:.1f} MB",
                    xy=(sizes[idx], mem_mb[idx]),
                    xytext=(-35, 12), textcoords="offset points",
                    arrowprops=dict(arrowstyle="->", color="#475569", lw=0.8),
                    fontsize=8.5, fontweight="bold")

    plt.tight_layout()
    out_path = os.path.join(output_dir, "P6.1-G06-vector-memory-scaling.png")
    plt.savefig(out_path)
    plt.close()
    print(f"Rendered: {out_path}")

def plot_g07(data, output_dir):
    """P6.1-G07 — Measured Brute-Force Retrieval Latency vs. Corpus Size"""
    sizes = data["scaling_data"]["corpus_sizes"]
    latencies = data["scaling_data"]["scan_latency_ms"]

    fig, ax = plt.subplots(figsize=(8.5, 5.2), dpi=300)
    ax.plot(sizes, latencies, marker="s", color=COLORS["Latency"], linewidth=2.2, markersize=6, label="Measured Scan Latency (CPU Dot-Product)")

    ax.set_title("P6.1-G07: Measured Brute-Force Retrieval Latency vs. Corpus Size", pad=15, fontweight="bold")
    ax.set_xlabel("Corpus Size (Number of 384-d Vectors)")
    ax.set_ylabel("Scan Latency (ms)")
    ax.grid(True)
    ax.axhline(y=10.0, color="#64748b", linestyle=":", linewidth=1.2, label="10ms Interactive Threshold")
    ax.legend(frameon=True, facecolor="white", edgecolor="#cbd5e1")

    for idx in [2, 5, 7]:
        ax.annotate(f"{sizes[idx]:,} vectors\n{latencies[idx]:.3f} ms",
                    xy=(sizes[idx], latencies[idx]),
                    xytext=(-40, 10), textcoords="offset points",
                    arrowprops=dict(arrowstyle="->", color="#475569", lw=0.8),
                    fontsize=8.5, fontweight="bold")

    plt.tight_layout()
    out_path = os.path.join(output_dir, "P6.1-G07-corpus-size-vs-retrieval-latency.png")
    plt.savefig(out_path)
    plt.close()
    print(f"Rendered: {out_path}")

def main():
    json_path = "benchmarks/phase-06.1/data/phase-06.1-results.json"
    output_dir = "benchmarks/phase-06.1/plots"
    os.makedirs(output_dir, exist_ok=True)

    print(f"Loading benchmark results from {json_path}...")
    data = load_data(json_path)

    plot_g01(data, output_dir)
    plot_g02(data, output_dir)
    plot_g03(data, output_dir)
    plot_g04(data, output_dir)
    plot_g05(data, output_dir)
    plot_g06(data, output_dir)
    plot_g07(data, output_dir)

    print("Successfully generated all 7 research plots in benchmarks/phase-06.1/plots/.")

if __name__ == "__main__":
    main()
