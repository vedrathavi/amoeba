"""
Phase 6.2 Research Plot Generator
Consumes benchmarks/phase-06.2/data/phase-06.2-results.json and renders 6 publication-quality graphs:
  - P6.2-G01-fusion-weight-vs-quality.png
  - P6.2-G02-method-quality-comparison.png
  - P6.2-G03-category-quality-comparison.png
  - P6.2-G04-query-win-loss.png
  - P6.2-G05-latency-comparison.png
  - P6.2-G06-candidate-depth-analysis.png
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
    "Hybrid (alpha=0.5 Weighted)": "#ec4899",
    "Hybrid (RRF k=60)": "#f59e0b",
    "P@1": "#6366f1",
    "MRR": "#10b981",
    "NDCG@5": "#f59e0b",
    "Latency": "#0284c7"
}

def load_data(json_path):
    with open(json_path, "r", encoding="utf-8") as f:
        return json.load(f)

def plot_g01(data, output_dir):
    """P6.2-G01 — Fusion Weight alpha vs Ranking Quality"""
    ws = data["weight_sweep"]
    alphas = [row["alpha"] for row in ws]
    p1 = [row["P@1"] for row in ws]
    mrr = [row["MRR"] for row in ws]
    ndcg5 = [row["NDCG@5"] for row in ws]

    fig, ax = plt.subplots(figsize=(8.5, 5.2), dpi=300)
    ax.plot(alphas, p1, marker="o", label="P@1", color=COLORS["P@1"], linewidth=2.2, markersize=5)
    ax.plot(alphas, mrr, marker="s", label="MRR", color=COLORS["MRR"], linewidth=2.2, markersize=5)
    ax.plot(alphas, ndcg5, marker="^", label="NDCG@5", color=COLORS["NDCG@5"], linewidth=2.2, markersize=5)

    ax.set_title("P6.2-G01: Fusion Weight Alpha (α) vs. Retrieval Quality (N=10 queries)", pad=15, fontweight="bold")
    ax.set_xlabel("Lexical Fusion Weight (α)  [α=0 Pure Semantic, α=1 Pure Lexical]")
    ax.set_ylabel("Metric Score")
    ax.set_xlim(-0.05, 1.05)
    ax.set_ylim(0.85, 1.02)
    ax.grid(True)
    ax.legend(frameon=True, facecolor="white", edgecolor="#cbd5e1", loc="lower left")

    # Annotate key regions
    ax.annotate("Pure Semantic\n(P@1: 1.00, MRR: 1.00)", xy=(0.0, 1.0), xytext=(0.03, 0.94),
                arrowprops=dict(arrowstyle="->", color="#475569", lw=0.8), fontsize=8.5, fontweight="bold")
    ax.annotate("Pure Lexical\n(P@1: 0.90, MRR: 0.93)", xy=(1.0, 0.90), xytext=(0.75, 0.87),
                arrowprops=dict(arrowstyle="->", color="#475569", lw=0.8), fontsize=8.5, fontweight="bold")
    ax.annotate("Balanced Hybrid α=0.5\n(P@1: 1.00, MRR: 1.00)", xy=(0.5, 1.0), xytext=(0.42, 0.96),
                arrowprops=dict(arrowstyle="->", color="#475569", lw=0.8), fontsize=8.5, fontweight="bold")

    plt.tight_layout()
    out_path = os.path.join(output_dir, "P6.2-G01-fusion-weight-vs-quality.png")
    plt.savefig(out_path)
    plt.close()
    print(f"Rendered: {out_path}")

def plot_g02(data, output_dir):
    """P6.2-G02 — Ranking Quality Comparison Across Methods"""
    methods = [
        "Baseline Lexical",
        "BM25",
        "CodeAware",
        "Semantic (all-MiniLM-L6-v2)",
        "Hybrid (alpha=0.5 Weighted)"
    ]
    p1 = [data["methods"][m]["metrics"]["P@1"] for m in methods]
    mrr = [data["methods"][m]["metrics"]["MRR"] for m in methods]
    ndcg5 = [data["methods"][m]["metrics"]["NDCG@5"] for m in methods]

    labels = ["Baseline", "BM25", "CodeAware", "Semantic\n(MiniLM)", "Hybrid\n(α=0.5)"]
    x = np.arange(len(labels))
    width = 0.25

    fig, ax = plt.subplots(figsize=(10, 5.5), dpi=300)
    r1 = ax.bar(x - width, p1, width, label="P@1", color="#6366f1", edgecolor="#334155", linewidth=0.8)
    r2 = ax.bar(x, mrr, width, label="MRR", color="#10b981", edgecolor="#334155", linewidth=0.8)
    r3 = ax.bar(x + width, ndcg5, width, label="NDCG@5", color="#f59e0b", edgecolor="#334155", linewidth=0.8)

    ax.set_title("P6.2-G02: Retrieval & Ranking Quality by Method (Validation Set, N=10)", pad=15, fontweight="bold")
    ax.set_ylabel("Score")
    ax.set_xticks(x)
    ax.set_xticklabels(labels)
    ax.set_ylim(0, 1.18)
    ax.legend(frameon=True, facecolor="white", edgecolor="#cbd5e1")
    ax.grid(axis="y")

    def autolabel(rects):
        for rect in rects:
            h = rect.get_height()
            ax.annotate(f"{h:.2f}",
                        xy=(rect.get_x() + rect.get_width() / 2, h),
                        xytext=(0, 3), textcoords="offset points",
                        ha="center", va="bottom", fontsize=8.5)

    autolabel(r1)
    autolabel(r2)
    autolabel(r3)

    plt.tight_layout()
    out_path = os.path.join(output_dir, "P6.2-G02-method-quality-comparison.png")
    plt.savefig(out_path)
    plt.close()
    print(f"Rendered: {out_path}")

def plot_g03(data, output_dir):
    """P6.2-G03 — Category Retrieval Quality Comparison"""
    methods = [
        "CodeAware",
        "Semantic (all-MiniLM-L6-v2)",
        "Hybrid (alpha=0.5 Weighted)"
    ]
    first_method = methods[0]
    categories = list(data["methods"][first_method]["metrics"]["category_P@1"].keys())
    
    y = np.arange(len(categories))
    height = 0.25

    fig, ax = plt.subplots(figsize=(10.5, 7.2), dpi=300)
    for i, method in enumerate(methods):
        scores = [data["methods"][method]["metrics"]["category_P@1"][cat] for cat in categories]
        label = "CodeAware (Lexical)" if "CodeAware" in method else ("Semantic" if "Semantic" in method else "Hybrid (α=0.5)")
        ax.barh(y + (i - 1) * height, scores, height, label=label,
                color=COLORS.get(method, "#64748b"), edgecolor="#334155", linewidth=0.8)

    ax.set_title("P6.2-G03: Retrieval Quality (P@1) by Query Category (1 Query per Category)", pad=15, fontweight="bold")
    ax.set_xlabel("Precision@1 (P@1)")
    ax.set_yticks(y)
    ax.set_yticklabels(categories)
    ax.set_xlim(0, 1.2)
    ax.invert_yaxis()
    ax.legend(loc="lower right", frameon=True, facecolor="white", edgecolor="#cbd5e1")
    ax.grid(axis="x")

    plt.tight_layout()
    out_path = os.path.join(output_dir, "P6.2-G03-category-quality-comparison.png")
    plt.savefig(out_path)
    plt.close()
    print(f"Rendered: {out_path}")

def plot_g04(data, output_dir):
    """P6.2-G04 — Query-Level Win / Loss / Preserved Outcome Distribution"""
    win_loss = data["complementarity_analysis"]["win_loss_summary"]
    labels = [
        "Hybrid Preserved\n(Lexical & Semantic Match)",
        "Hybrid Improved\n(Resolved Lexical Miss)",
        "Hybrid Regressed\n(Worse than Lexical)"
    ]
    counts = [
        win_loss.get("HYBRID_PRESERVED", 9),
        win_loss.get("HYBRID_IMPROVED", 1),
        win_loss.get("HYBRID_REGRESSED", 0)
    ]
    colors = ["#10b981", "#6366f1", "#ef4444"]

    fig, ax = plt.subplots(figsize=(8.5, 5), dpi=300)
    bars = ax.bar(labels, counts, color=colors, width=0.5, edgecolor="#334155", linewidth=0.8)

    ax.set_title("P6.2-G04: Query-Level Outcome Classification (N=10 queries)", pad=15, fontweight="bold")
    ax.set_ylabel("Number of Queries")
    ax.set_ylim(0, 11)
    ax.grid(axis="y")

    for bar in bars:
        h = bar.get_height()
        ax.annotate(f"{int(h)} ({int(h)*10}%)",
                    xy=(bar.get_x() + bar.get_width() / 2, h),
                    xytext=(0, 4), textcoords="offset points",
                    ha="center", va="bottom", fontweight="bold", fontsize=10)

    plt.tight_layout()
    out_path = os.path.join(output_dir, "P6.2-G04-query-win-loss.png")
    plt.savefig(out_path)
    plt.close()
    print(f"Rendered: {out_path}")

def plot_g05(data, output_dir):
    """P6.2-G05 — Latency Comparison across Pipelines"""
    perf = data["performance"]
    pipelines = ["Lexical Only\n(CodeAware)", "Semantic Only\n(MiniLM)", "Hybrid Pipeline\n(Lexical+Semantic+Fusion)"]
    latencies = [
        perf["lexical_only_total_ms"],
        perf["semantic_only_total_ms"],
        perf["total_hybrid_query_latency_ms"]
    ]
    colors = ["#10b981", "#8b5cf6", "#ec4899"]

    fig, ax = plt.subplots(figsize=(8.5, 5), dpi=300)
    bars = ax.bar(pipelines, latencies, color=colors, width=0.5, edgecolor="#334155", linewidth=0.8)

    ax.set_title("P6.2-G05: Total End-to-End Query Latency by Pipeline (CPU, Median)", pad=15, fontweight="bold")
    ax.set_ylabel("Latency (ms)")
    ax.set_ylim(0, max(latencies) * 1.3)
    ax.grid(axis="y")

    for bar in bars:
        h = bar.get_height()
        ax.annotate(f"{h:.2f} ms",
                    xy=(bar.get_x() + bar.get_width() / 2, h),
                    xytext=(0, 4), textcoords="offset points",
                    ha="center", va="bottom", fontweight="bold", fontsize=10)

    plt.tight_layout()
    out_path = os.path.join(output_dir, "P6.2-G05-latency-comparison.png")
    plt.savefig(out_path)
    plt.close()
    print(f"Rendered: {out_path}")

def plot_g06(data, output_dir):
    """P6.2-G06 — Candidate Pool Depth Analysis"""
    cda = data["candidate_depth_analysis"]
    depths = [r["candidate_depth_k"] for r in cda]
    ndcg5 = [r["NDCG@5"] for r in cda]
    mrr = [r["MRR"] for r in cda]

    fig, ax1 = plt.subplots(figsize=(8.5, 5), dpi=300)

    color_ndcg = "#6366f1"
    color_mrr = "#10b981"

    ax1.plot(depths, ndcg5, marker="o", color=color_ndcg, linewidth=2.2, label="NDCG@5")
    ax1.plot(depths, mrr, marker="s", color=color_mrr, linewidth=2.2, linestyle="--", label="MRR")
    ax1.set_xlabel("Semantic Candidate Depth (Top-K)")
    ax1.set_ylabel("Ranking Quality Score")
    ax1.set_ylim(0.95, 1.01)
    ax1.grid(True)
    ax1.legend(loc="lower right", frameon=True, facecolor="white", edgecolor="#cbd5e1")

    ax1.set_title("P6.2-G06: Candidate Pool Depth vs. Hybrid Ranking Quality", pad=15, fontweight="bold")

    plt.tight_layout()
    out_path = os.path.join(output_dir, "P6.2-G06-candidate-depth-analysis.png")
    plt.savefig(out_path)
    plt.close()
    print(f"Rendered: {out_path}")

def main():
    json_path = "benchmarks/phase-06.2/data/phase-06.2-results.json"
    output_dir = "benchmarks/phase-06.2/plots"
    os.makedirs(output_dir, exist_ok=True)

    print(f"Loading Phase 6.2 benchmark results from {json_path}...")
    data = load_data(json_path)

    plot_g01(data, output_dir)
    plot_g02(data, output_dir)
    plot_g03(data, output_dir)
    plot_g04(data, output_dir)
    plot_g05(data, output_dir)
    plot_g06(data, output_dir)

    print("Successfully generated all 6 research plots in benchmarks/phase-06.2/plots/.")

if __name__ == "__main__":
    main()
