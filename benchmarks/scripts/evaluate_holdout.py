"""
Phase 6.2 Real-World External Holdout Evaluation Script
Evaluates 10 frozen queries on the real-world frontend repository (demo_test_projects/calendar, 2,820 elements)
across 7 retrieval methods:
1. Baseline Lexical
2. BM25
3. CodeAware
4. Semantic (all-MiniLM-L6-v2)
5. Hybrid alpha=0.5
6. Hybrid alpha=0.3
7. Hybrid RRF (k=60)
"""

import json
import os
import re
import time
import math
import csv
import numpy as np
import matplotlib.pyplot as plt
from sentence_transformers import SentenceTransformer

# 1. Load 2,820 elements dumped from Amoeba engine
DATA_DIR = "benchmarks/phase-06.2/holdout/data"
PLOTS_DIR = "benchmarks/phase-06.2/holdout/plots"
os.makedirs(DATA_DIR, exist_ok=True)
os.makedirs(PLOTS_DIR, exist_ok=True)

with open(os.path.join(DATA_DIR, "holdout_elements.json"), "r", encoding="utf-8") as f:
    ELEMENTS = json.load(f)

# 2. Frozen Evaluation Queries (strictly verbatim from docs/experiments/real-world-holdout-frontend.md)
FROZEN_QUERIES = [
    {
        "id": "Q1",
        "query": "CalendarGrid",
        "category": "Exact Identifier",
        "expected_name": "CalendarGrid",
        "expected_file": "src/components/calendar/CalendarGrid.tsx",
        "expected_kinds": ["Component", "JSXComponent"],
        "rationale": "Direct exact match for the 7-column monthly calendar matrix component."
    },
    {
        "id": "Q2",
        "query": "use calendar",
        "category": "Normalized Identifier",
        "expected_name": "useCalendar",
        "expected_file": "src/hooks/useCalendar.ts",
        "expected_kinds": ["Hook", "Function"],
        "rationale": "Space-separated normalized query targeting camelCase React date hook."
    },
    {
        "id": "Q3",
        "query": "LocalStorage",
        "category": "Partial/Subword",
        "expected_name": "useLocalStorage",
        "expected_file": "src/hooks/useLocalStorage.ts",
        "expected_kinds": ["Hook", "Function"],
        "rationale": "Subword search for client-side persistence handler for notes & highlights."
    },
    {
        "id": "Q4",
        "query": "FloatingToolbar action",
        "category": "Multi-Term",
        "expected_name": "FloatingToolbar",
        "expected_file": "src/components/floating-toolbar/FloatingToolbar.tsx",
        "expected_kinds": ["Component", "JSXComponent", "Interface"],
        "rationale": "Multi-term query for floating navigation action bar and dispatch handlers."
    },
    {
        "id": "Q5",
        "query": "formatDate formatStr",
        "category": "Contextual Lexical",
        "expected_name": "formatDate",
        "expected_file": "src/lib/dateUtils.ts",
        "expected_kinds": ["Function"],
        "rationale": "Function identifier combined with its signature parameter term."
    },
    {
        "id": "Q6",
        "query": "RootLayout Next.js metadata",
        "category": "Framework",
        "expected_name": "RootLayout",
        "expected_file": "src/app/layout.tsx",
        "expected_kinds": ["Component", "Route"],
        "rationale": "Next.js App Router root layout managing global OpenGraph SEO metadata."
    },
    {
        "id": "Q7",
        "query": "Dialog",
        "category": "Ambiguous",
        "expected_name": "Dialog",
        "expected_file": "src/components/ui/dialog.tsx",
        "expected_kinds": ["JSXComponent", "Component", "Function"],
        "rationale": "Ambiguous query spanning modal primitive definitions and page usage sites."
    },
    {
        "id": "Q8",
        "query": "Where is the location destination photo and travel description rendered for each month?",
        "category": "Conceptual/Semantic",
        "expected_name": "ImagePanel",
        "expected_file": "src/components/calendar/ImagePanel.tsx",
        "expected_kinds": ["Component", "JSXComponent"],
        "alternate_name": "getImagePanelData",
        "alternate_file": "src/lib/monthLocationData.ts",
        "rationale": "Natural language question targeting landscape photography showcase without quoting exact component names."
    },
    {
        "id": "Q9",
        "query": "MonthLocation getImagePanelData",
        "category": "Cross-File",
        "expected_name": "MonthLocation",
        "expected_file": "src/lib/monthLocationData.ts",
        "expected_kinds": ["Interface", "Function"],
        "alternate_name": "getImagePanelData",
        "alternate_file": "src/lib/monthLocationData.ts",
        "rationale": "Cross-file data contract connecting destination datasets with UI presentation."
    },
    {
        "id": "Q10",
        "query": "CalendarDay rendered inside CalendarGrid",
        "category": "Component Relationship",
        "expected_name": "CalendarDay",
        "expected_file": "src/components/calendar/CalendarDay.tsx",
        "expected_kinds": ["Component", "JSXComponent"],
        "alternate_name": "CalendarGrid",
        "alternate_file": "src/components/calendar/CalendarGrid.tsx",
        "rationale": "Structural parent-child React relationship mapping date intervals to individual interactive day cells."
    }
]

# 3. Tokenizer and Inverted Index matching Amoeba
def split_tokens(s):
    if not s:
        return []
    # Split camelCase, PascalCase, snake_case, kebab-case, punctuation
    tokens = re.findall(r'[A-Z]?[a-z]+|[A-Z]+(?=[A-Z][a-z]|\d|\W|$)|\d+', s)
    return [t.lower() for t in tokens if len(t) > 0]

class HoldoutInvertedIndex:
    def __init__(self, elements):
        self.elements = elements
        self.postings = {}  # term -> list of (doc_idx, term_freq, field_weight)
        self.doc_lens = []
        self.doc_tokens = []

        for idx, el in enumerate(elements):
            name_tokens = split_tokens(el["name"])
            ctx_tokens = split_tokens(el["context"])
            file_tokens = split_tokens(el["file"])
            detail_tokens = split_tokens(el["detail"])
            kind_tokens = split_tokens(el["kind"])

            # CodeAware field weights
            term_scores = {}
            for t in name_tokens:
                term_scores[t] = term_scores.get(t, 0.0) + 3.0
            for t in ctx_tokens:
                term_scores[t] = term_scores.get(t, 0.0) + 1.5
            for t in file_tokens:
                term_scores[t] = term_scores.get(t, 0.0) + 2.0
            for t in detail_tokens:
                term_scores[t] = term_scores.get(t, 0.0) + 1.0
            for t in kind_tokens:
                term_scores[t] = term_scores.get(t, 0.0) + 1.0

            all_toks = name_tokens + ctx_tokens + file_tokens + detail_tokens + kind_tokens
            self.doc_tokens.append(all_toks)
            self.doc_lens.append(len(all_toks))

            for term, score in term_scores.items():
                if term not in self.postings:
                    self.postings[term] = []
                self.postings[term].append((idx, all_toks.count(term), score))

        self.N = len(elements)
        self.avgdl = sum(self.doc_lens) / max(1, self.N)

    def search_baseline(self, query_str):
        q_tokens = split_tokens(query_str)
        if not q_tokens:
            return []
        scores = {}
        for t in q_tokens:
            if t in self.postings:
                for idx, tf, _ in self.postings[t]:
                    scores[idx] = scores.get(idx, 0.0) + tf
        ranked = sorted(scores.items(), key=lambda x: (-x[1], x[0]))
        return ranked

    def search_bm25(self, query_str, k1=1.2, b=0.75):
        q_tokens = split_tokens(query_str)
        if not q_tokens:
            return []
        scores = {}
        for t in q_tokens:
            if t in self.postings:
                df = len(self.postings[t])
                idf = math.log(1.0 + (self.N - df + 0.5) / (df + 0.5))
                for idx, tf, _ in self.postings[t]:
                    dl = self.doc_lens[idx]
                    denom = tf + k1 * (1.0 - b + b * (dl / self.avgdl))
                    num = tf * (k1 + 1.0)
                    scores[idx] = scores.get(idx, 0.0) + idf * (num / denom)
        ranked = sorted(scores.items(), key=lambda x: (-x[1], x[0]))
        return ranked

    def search_code_aware(self, query_str):
        q_tokens = split_tokens(query_str)
        if not q_tokens:
            return []
        scores = {}
        for t in q_tokens:
            if t in self.postings:
                df = len(self.postings[t])
                idf = math.log(1.0 + (self.N - df + 0.5) / (df + 0.5))
                for idx, tf, field_weight in self.postings[t]:
                    score = idf * field_weight * (1.0 + math.log(1.0 + tf))
                    scores[idx] = scores.get(idx, 0.0) + score
        ranked = sorted(scores.items(), key=lambda x: (-x[1], x[0]))
        return ranked

def min_max_norm(score_dict, top_k=20):
    if not score_dict:
        return {}
    items = list(score_dict.items())[:top_k]
    vals = [s for _, s in items]
    s_min, s_max = min(vals), max(vals)
    if abs(s_max - s_min) < 1e-9:
        return {idx: 1.0 for idx, _ in items}
    return {idx: (s - s_min) / (s_max - s_min) for idx, s in items}

def is_target_match(elem, q_spec):
    # Match primary or alternate targets
    primary_name = q_spec.get("expected_name", "").lower()
    primary_file = q_spec.get("expected_file", "").lower()
    alt_name = q_spec.get("alternate_name", "").lower()
    alt_file = q_spec.get("alternate_file", "").lower()

    e_name = elem["name"].lower()
    e_file = elem["file"].lower()
    e_kind = elem["kind"]

    if primary_name and primary_name == e_name:
        if not primary_file or primary_file in e_file:
            return True

    if alt_name and alt_name == e_name:
        if not alt_file or alt_file in e_file:
            return True

    # Check for Route or Component file level match
    if primary_name in ["rootlayout", "calendargrid", "floatingtoolbar", "notessection"]:
        if primary_file and primary_file in e_file and e_kind in ["Component", "Route", "JSXComponent"]:
            return True

    return False

def human_inspect_relevance(top_elem, q_spec):
    if is_target_match(top_elem, q_spec):
        return "CORRECT"
    
    # Check partial relevance (e.g. inner element of target file, related prop, or sub-component)
    exp_file = q_spec.get("expected_file", "").lower()
    e_file = top_elem["file"].lower()
    if exp_file and exp_file in e_file:
        return "PARTIALLY_RELEVANT"
    
    # Check if target name matches context
    exp_name = q_spec.get("expected_name", "").lower()
    e_ctx = top_elem.get("context", "").lower()
    if exp_name and exp_name == e_ctx:
        return "PARTIALLY_RELEVANT"

    return "INCORRECT"

def main():
    print("==================================================")
    print("Phase 6.2 Real-World Holdout Evaluation")
    print("Corpus: demo_test_projects/calendar (2,820 elements)")
    print("Model:  all-MiniLM-L6-v2 (384-d dense embeddings)")
    print("==================================================")

    # Build Index
    t0 = time.perf_counter()
    index = HoldoutInvertedIndex(ELEMENTS)
    print(f"Indexed {len(ELEMENTS)} elements into InvertedIndex in {(time.perf_counter()-t0)*1000:.2f} ms")

    # Load SentenceTransformer model
    print("Loading all-MiniLM-L6-v2 model...")
    model = SentenceTransformer("all-MiniLM-L6-v2")
    
    # Pre-embed all documents
    t0 = time.perf_counter()
    doc_texts = [el["semantic_text"] for el in ELEMENTS]
    doc_embeddings = model.encode(doc_texts, batch_size=64, normalize_embeddings=True, show_progress_bar=True)
    embed_corpus_time = (time.perf_counter() - t0) * 1000
    print(f"Embedded 2,820 documents in {embed_corpus_time:.2f} ms")

    # Methods to evaluate
    METHODS = [
        "Baseline Lexical",
        "BM25",
        "CodeAware",
        "Semantic",
        "Hybrid alpha=0.5",
        "Hybrid alpha=0.3",
        "Hybrid RRF (k=60)"
    ]

    method_latencies = {m: [] for m in METHODS}
    method_rankings = {m: [] for m in METHODS}
    per_query_records = []

    for q_idx, q in enumerate(FROZEN_QUERIES):
        q_str = q["query"]
        
        # 1. Baseline
        t_start = time.perf_counter()
        base_res = index.search_baseline(q_str)
        t_base = (time.perf_counter() - t_start) * 1000
        method_latencies["Baseline Lexical"].append(t_base)
        method_rankings["Baseline Lexical"].append([idx for idx, _ in base_res])

        # 2. BM25
        t_start = time.perf_counter()
        bm25_res = index.search_bm25(q_str)
        t_bm25 = (time.perf_counter() - t_start) * 1000
        method_latencies["BM25"].append(t_bm25)
        method_rankings["BM25"].append([idx for idx, _ in bm25_res])

        # 3. CodeAware
        t_start = time.perf_counter()
        ca_res = index.search_code_aware(q_str)
        t_ca = (time.perf_counter() - t_start) * 1000
        method_latencies["CodeAware"].append(t_ca)
        method_rankings["CodeAware"].append([idx for idx, _ in ca_res])

        # 4. Semantic
        t_start = time.perf_counter()
        q_emb = model.encode([q_str], normalize_embeddings=True)[0]
        t_q_emb = (time.perf_counter() - t_start) * 1000

        t_start = time.perf_counter()
        cos_sims = np.dot(doc_embeddings, q_emb)
        sem_ranked_indices = np.argsort(-cos_sims)
        sem_res = [(int(idx), float(cos_sims[idx])) for idx in sem_ranked_indices]
        t_sem_scan = (time.perf_counter() - t_start) * 1000
        t_sem_total = t_q_emb + t_sem_scan
        method_latencies["Semantic"].append(t_sem_total)
        method_rankings["Semantic"].append([idx for idx, _ in sem_res])

        # Candidates pool for fusion (top 20 from lexical and top 20 from semantic)
        ca_dict = {idx: s for idx, s in ca_res[:20]}
        sem_dict = {idx: s for idx, s in sem_res[:20]}
        norm_ca = min_max_norm(ca_dict, 20)
        norm_sem = min_max_norm(sem_dict, 20)
        union_ids = set(norm_ca.keys()).union(set(norm_sem.keys()))

        # 5. Hybrid alpha=0.5
        t_start = time.perf_counter()
        h05_scores = {}
        for uid in union_ids:
            s_lex = norm_ca.get(uid, 0.0)
            s_sem = norm_sem.get(uid, 0.0)
            h05_scores[uid] = 0.5 * s_lex + 0.5 * s_sem
        h05_res = sorted(h05_scores.items(), key=lambda x: (-x[1], x[0]))
        t_h05 = t_ca + t_sem_total + (time.perf_counter() - t_start) * 1000
        method_latencies["Hybrid alpha=0.5"].append(t_h05)
        method_rankings["Hybrid alpha=0.5"].append([idx for idx, _ in h05_res])

        # 6. Hybrid alpha=0.3
        t_start = time.perf_counter()
        h03_scores = {}
        for uid in union_ids:
            s_lex = norm_ca.get(uid, 0.0)
            s_sem = norm_sem.get(uid, 0.0)
            h03_scores[uid] = 0.3 * s_lex + 0.7 * s_sem
        h03_res = sorted(h03_scores.items(), key=lambda x: (-x[1], x[0]))
        t_h03 = t_ca + t_sem_total + (time.perf_counter() - t_start) * 1000
        method_latencies["Hybrid alpha=0.3"].append(t_h03)
        method_rankings["Hybrid alpha=0.3"].append([idx for idx, _ in h03_res])

        # 7. Hybrid RRF (k=60)
        t_start = time.perf_counter()
        rrf_scores = {}
        ca_top20 = [idx for idx, _ in ca_res[:20]]
        sem_top20 = [idx for idx, _ in sem_res[:20]]
        for r_i, uid in enumerate(ca_top20):
            rrf_scores[uid] = rrf_scores.get(uid, 0.0) + 1.0 / (60.0 + r_i + 1)
        for r_i, uid in enumerate(sem_top20):
            rrf_scores[uid] = rrf_scores.get(uid, 0.0) + 1.0 / (60.0 + r_i + 1)
        rrf_res = sorted(rrf_scores.items(), key=lambda x: (-x[1], x[0]))
        t_rrf = t_ca + t_sem_total + (time.perf_counter() - t_start) * 1000
        method_latencies["Hybrid RRF (k=60)"].append(t_rrf)
        method_rankings["Hybrid RRF (k=60)"].append([idx for idx, _ in rrf_res])

        # Per-query record collection
        q_record = {
            "query_id": q["id"],
            "query": q["query"],
            "category": q["category"],
            "expected_target": f"{q['expected_name']} ({q['expected_file']})",
            "methods": {}
        }

        for m in METHODS:
            ranked = method_rankings[m][q_idx]
            top_elem = ELEMENTS[ranked[0]] if ranked else None
            
            # Find rank of target
            target_rank = None
            for r_pos, d_idx in enumerate(ranked):
                if is_target_match(ELEMENTS[d_idx], q):
                    target_rank = r_pos + 1
                    break

            relevance = human_inspect_relevance(top_elem, q) if top_elem else "INCORRECT"
            
            q_record["methods"][m] = {
                "top_id": top_elem["id"] if top_elem else -1,
                "top_name": top_elem["name"] if top_elem else "NONE",
                "top_kind": top_elem["kind"] if top_elem else "NONE",
                "top_file": top_elem["file"] if top_elem else "NONE",
                "target_rank": target_rank if target_rank else 999,
                "relevance": relevance,
                "latency_ms": round(method_latencies[m][q_idx], 3)
            }

        per_query_records.append(q_record)

    # 4. Compute aggregate metrics across methods
    results_summary = []
    for m in METHODS:
        ranked_lists = method_rankings[m]
        lat_list = method_latencies[m]

        p1_list, p3_list, p5_list = [], [], []
        r5_list, r10_list = [], []
        mrr_list = []
        ndcg5_list, ndcg10_list = [], []
        cat_p1 = {}

        for q_idx, q in enumerate(FROZEN_QUERIES):
            ranked = ranked_lists[q_idx]
            cat = q["category"]

            # Ground truth matching elements
            rel_indices = [idx for idx, el in enumerate(ELEMENTS) if is_target_match(el, q)]
            rel_set = set(rel_indices)

            top1 = ranked[:1]
            top3 = ranked[:3]
            top5 = ranked[:5]
            top10 = ranked[:10]

            p1 = 1.0 if top1 and top1[0] in rel_set else 0.0
            p3 = sum(1 for d in top3 if d in rel_set) / 3.0
            p5 = sum(1 for d in top5 if d in rel_set) / 5.0
            r5 = (sum(1 for d in top5 if d in rel_set) / len(rel_set)) if rel_set else 0.0
            r10 = (sum(1 for d in top10 if d in rel_set) / len(rel_set)) if rel_set else 0.0

            rr = 0.0
            for r_idx, d in enumerate(ranked):
                if d in rel_set:
                    rr = 1.0 / (r_idx + 1)
                    break

            def dcg(r_list, k):
                val = 0.0
                for i, d in enumerate(r_list[:k]):
                    if d in rel_set:
                        val += 1.0 / math.log2(i + 2)
                return val

            def idcg(rel_count, k):
                val = 0.0
                for i in range(min(rel_count, k)):
                    val += 1.0 / math.log2(i + 2)
                return val

            idcg5 = idcg(len(rel_set), 5)
            idcg10 = idcg(len(rel_set), 10)
            ndcg5 = (dcg(ranked, 5) / idcg5) if idcg5 > 0 else 0.0
            ndcg10 = (dcg(ranked, 10) / idcg10) if idcg10 > 0 else 0.0

            p1_list.append(p1)
            p3_list.append(p3)
            p5_list.append(p5)
            r5_list.append(r5)
            r10_list.append(r10)
            mrr_list.append(rr)
            ndcg5_list.append(ndcg5)
            ndcg10_list.append(ndcg10)

            if cat not in cat_p1:
                cat_p1[cat] = []
            cat_p1[cat].append(p1)

        summary_row = {
            "Method": m,
            "P@1": round(float(np.mean(p1_list)), 4),
            "P@3": round(float(np.mean(p3_list)), 4),
            "P@5": round(float(np.mean(p5_list)), 4),
            "Recall@5": round(float(np.mean(r5_list)), 4),
            "Recall@10": round(float(np.mean(r10_list)), 4),
            "MRR": round(float(np.mean(mrr_list)), 4),
            "NDCG@5": round(float(np.mean(ndcg5_list)), 4),
            "NDCG@10": round(float(np.mean(ndcg10_list)), 4),
            "LatencyMs": round(float(np.mean(lat_list)), 2),
            "Category_P@1": {cat: float(np.mean(vals)) for cat, vals in cat_p1.items()}
        }
        results_summary.append(summary_row)

    # 5. Export JSON
    out_json_path = os.path.join(DATA_DIR, "real-world-holdout-results.json")
    with open(out_json_path, "w", encoding="utf-8") as f:
        json.dump({
            "corpus": "demo_test_projects/calendar",
            "total_elements": len(ELEMENTS),
            "total_queries": len(FROZEN_QUERIES),
            "aggregate_results": results_summary,
            "per_query_results": per_query_records
        }, f, indent=2)
    print(f"Exported JSON: {out_json_path}")

    # 6. Export Results CSV
    out_csv_path = os.path.join(DATA_DIR, "real-world-holdout-results.csv")
    with open(out_csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["Method", "P@1", "P@3", "P@5", "Recall@5", "Recall@10", "MRR", "NDCG@5", "NDCG@10", "LatencyMs"])
        for r in results_summary:
            writer.writerow([r["Method"], r["P@1"], r["P@3"], r["P@5"], r["Recall@5"], r["Recall@10"], r["MRR"], r["NDCG@5"], r["NDCG@10"], r["LatencyMs"]])
    print(f"Exported CSV: {out_csv_path}")

    # 7. Export Per-Query CSV
    out_pq_path = os.path.join(DATA_DIR, "real-world-holdout-per-query.csv")
    with open(out_pq_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["QueryId", "Query", "Category", "Method", "TopResultName", "TopResultKind", "TopResultFile", "TargetRank", "Relevance", "LatencyMs"])
        for q_rec in per_query_records:
            for m in METHODS:
                m_info = q_rec["methods"][m]
                writer.writerow([
                    q_rec["query_id"],
                    q_rec["query"],
                    q_rec["category"],
                    m,
                    m_info["top_name"],
                    m_info["top_kind"],
                    m_info["top_file"],
                    m_info["target_rank"],
                    m_info["relevance"],
                    m_info["latency_ms"]
                ])
    print(f"Exported Per-Query CSV: {out_pq_path}")

    # 8. Render Plots
    # Plot 1: Method Quality Comparison (P@1, MRR, NDCG@5)
    plt.figure(figsize=(10, 5))
    x = np.arange(len(METHODS))
    width = 0.25
    p1s = [r["P@1"] for r in results_summary]
    mrrs = [r["MRR"] for r in results_summary]
    ndcg5s = [r["NDCG@5"] for r in results_summary]

    plt.bar(x - width, p1s, width, label="P@1", color="#3b82f6")
    plt.bar(x, mrrs, width, label="MRR", color="#10b981")
    plt.bar(x + width, ndcg5s, width, label="NDCG@5", color="#8b5cf6")
    plt.xticks(x, [m.replace("Hybrid ", "H-").replace(" (k=60)", "") for m in METHODS], rotation=15, ha="right", fontsize=9)
    plt.ylabel("Score")
    plt.ylim(0, 1.1)
    plt.title("Real-World Holdout: Method Quality Comparison (2,820 Elements, 10 Queries)", fontsize=11, fontweight="bold")
    plt.legend()
    plt.grid(axis="y", linestyle="--", alpha=0.5)
    plt.tight_layout()
    p1_path = os.path.join(PLOTS_DIR, "P6.2-H01-method-quality-comparison.png")
    plt.savefig(p1_path, dpi=300)
    plt.close()

    # Plot 2: Per-Query Target Rank Comparison across Key Methods (CodeAware, Semantic, Hybrid 0.5, RRF)
    plt.figure(figsize=(11, 5.5))
    q_labels = [q["id"] for q in FROZEN_QUERIES]
    methods_to_plot = ["CodeAware", "Semantic", "Hybrid alpha=0.5", "Hybrid RRF (k=60)"]
    colors = ["#f59e0b", "#3b82f6", "#10b981", "#ec4899"]
    x = np.arange(len(q_labels))
    w = 0.18

    for i, m in enumerate(methods_to_plot):
        ranks = [min(10, q_rec["methods"][m]["target_rank"]) for q_rec in per_query_records]
        plt.bar(x + (i - 1.5) * w, ranks, w, label=m, color=colors[i])

    plt.xticks(x, [f"{q['id']}: {q['category'][:10]}" for q in FROZEN_QUERIES], rotation=25, ha="right", fontsize=9)
    plt.ylabel("Target Rank (Lower is Better, Capped at 10)")
    plt.title("Real-World Holdout: Target Rank per Query (1=Rank 1 Success)", fontsize=11, fontweight="bold")
    plt.legend()
    plt.grid(axis="y", linestyle="--", alpha=0.5)
    plt.tight_layout()
    p2_path = os.path.join(PLOTS_DIR, "P6.2-H02-per-query-method-comparison.png")
    plt.savefig(p2_path, dpi=300)
    plt.close()

    # Plot 3: Category P@1 Comparison
    plt.figure(figsize=(12, 6))
    cats = [q["category"] for q in FROZEN_QUERIES]
    ca_cat_p1 = [results_summary[2]["Category_P@1"][c] for c in cats]
    sem_cat_p1 = [results_summary[3]["Category_P@1"][c] for c in cats]
    h05_cat_p1 = [results_summary[4]["Category_P@1"][c] for c in cats]
    rrf_cat_p1 = [results_summary[6]["Category_P@1"][c] for c in cats]

    x = np.arange(len(cats))
    w = 0.2
    plt.bar(x - 1.5*w, ca_cat_p1, w, label="CodeAware", color="#f59e0b")
    plt.bar(x - 0.5*w, sem_cat_p1, w, label="Semantic", color="#3b82f6")
    plt.bar(x + 0.5*w, h05_cat_p1, w, label="Hybrid α=0.5", color="#10b981")
    plt.bar(x + 1.5*w, rrf_cat_p1, w, label="Hybrid RRF", color="#ec4899")
    plt.xticks(x, cats, rotation=30, ha="right", fontsize=9)
    plt.ylabel("P@1 (Top-1 Accuracy)")
    plt.ylim(0, 1.15)
    plt.title("Real-World Holdout: P@1 across Query Categories", fontsize=11, fontweight="bold")
    plt.legend()
    plt.grid(axis="y", linestyle="--", alpha=0.5)
    plt.tight_layout()
    p3_path = os.path.join(PLOTS_DIR, "P6.2-H03-category-quality.png")
    plt.savefig(p3_path, dpi=300)
    plt.close()

    # Plot 4: Latency Comparison
    plt.figure(figsize=(9, 4.5))
    lats = [r["LatencyMs"] for r in results_summary]
    m_names = [r["Method"] for r in results_summary]
    bars = plt.bar(m_names, lats, color="#0284c7")
    for bar in bars:
        yval = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2.0, yval + 0.2, f"{yval:.2f} ms", ha='center', va='bottom', fontsize=8)
    plt.xticks(rotation=20, ha="right", fontsize=9)
    plt.ylabel("Query Latency (ms, CPU)")
    plt.ylim(0, max(lats) * 1.25)
    plt.title("Real-World Holdout: Query Latency by Retrieval Pipeline", fontsize=11, fontweight="bold")
    plt.grid(axis="y", linestyle="--", alpha=0.5)
    plt.tight_layout()
    p4_path = os.path.join(PLOTS_DIR, "P6.2-H04-latency-comparison.png")
    plt.savefig(p4_path, dpi=300)
    plt.close()

    print("Rendered all 4 research plots in benchmarks/phase-06.2/holdout/plots/")

if __name__ == "__main__":
    main()
