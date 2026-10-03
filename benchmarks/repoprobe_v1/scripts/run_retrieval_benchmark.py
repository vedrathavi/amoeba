#!/usr/bin/env python3
"""
Amoeba Phase 8.3 — Fast Retrieval-Only Benchmark Runner
Executes primary retrieval across all 10 benchmark repositories (70 questions)
WITHOUT LLM reasoning, evidence assembly, or Ollama overhead.
Measures: Hit@1, Hit@5, Hit@10, Hit@20, Hit@50, MRR, Target Rank Distribution,
and detailed Latency across Hybrid, Lexical-Only, and Semantic-Only modes.
"""

import json
import os
import subprocess
import sys
import time
from collections import defaultdict

sys.stdout.reconfigure(encoding="utf-8")

BENCHMARK_DIR = r"d:\amoeba\benchmarks\repoprobe_v1"
REPOS_DIR = os.path.join(BENCHMARK_DIR, "repos")
RESULTS_DIR = os.path.join(BENCHMARK_DIR, "results", "retrieval")
EXE_PATH = r"d:\amoeba\build\amoeba_retrieval_benchmark.exe"


def main():
    os.makedirs(RESULTS_DIR, exist_ok=True)
    print("=" * 80)
    print("Amoeba Phase 8.3 — Fast Retrieval-Only Benchmark Runner")
    print("=" * 80)
    print(f"Binary: {EXE_PATH}")
    print(f"Results Directory: {RESULTS_DIR}\n")

    # 1. Load questions & ground truth
    with open(os.path.join(BENCHMARK_DIR, "questions.json"), "r", encoding="utf-8") as f:
        questions = json.load(f)
    with open(os.path.join(BENCHMARK_DIR, "ground_truth.json"), "r", encoding="utf-8") as f:
        ground_truth = json.load(f)

    gt_by_id = {g["question_id"]: g for g in ground_truth}

    by_repo = {}
    for q in questions:
        qid = q["question_id"]
        gt = gt_by_id.get(qid, {})
        q_item = {
            "question_id": qid,
            "repo_name": q["repo_name"],
            "source": q.get("source", "repoprobe"),
            "taxonomy": q.get("taxonomy", "Implementation Details"),
            "difficulty": str(q.get("difficulty", "Medium")),
            "question": q["question"].replace("\r", "").replace("\n", " ").strip(),
            "expected_files": gt.get("expected_files", []),
            "expected_symbols": gt.get("expected_symbols", []),
            "expected_relationships": gt.get("expected_relationships", []),
            "expected_concepts": gt.get("expected_concepts", []),
        }
        by_repo.setdefault(q["repo_name"], []).append(q_item)

    all_results = []
    start_total_time = time.time()

    # 2. Execute repository by repository
    for repo_idx, (repo_name, q_list) in enumerate(by_repo.items(), 1):
        slug = repo_name.split("/")[-1]
        repo_dir = os.path.join(REPOS_DIR, slug)
        temp_tsv = os.path.join(RESULTS_DIR, f"temp_{slug}_queries.tsv")
        temp_out = os.path.join(RESULTS_DIR, f"temp_{slug}_results.json")

        print(f"[{repo_idx}/10] Running Retrieval Benchmark on: {repo_name} ({len(q_list)} queries)...")

        with open(temp_tsv, "w", encoding="utf-8") as tf:
            for q in q_list:
                files_str = "|".join(q["expected_files"])
                syms_str = "|".join(q["expected_symbols"])
                rels_str = "|".join(q["expected_relationships"])
                concepts_str = "|".join(q["expected_concepts"])
                tf.write(
                    f"{q['question_id']}\t{q['repo_name']}\t{q['source']}\t{q['taxonomy']}\t"
                    f"{q['difficulty']}\t{q['question']}\t{files_str}\t{syms_str}\t{rels_str}\t{concepts_str}\n"
                )

        cmd = [EXE_PATH, repo_dir, temp_tsv, temp_out]
        t0 = time.time()
        res = subprocess.run(cmd, capture_output=True, text=True)
        t_elapsed = time.time() - t0

        if res.returncode != 0:
            print(f"  ERROR executing benchmark on {slug}:")
            print(res.stderr)
            continue

        print(f"  Finished {slug} in {t_elapsed:.2f}s.")

        if os.path.exists(temp_out):
            with open(temp_out, "r", encoding="utf-8") as f_out:
                repo_res = json.load(f_out)
                all_results.extend(repo_res)

    total_wall_time = time.time() - start_total_time
    print(f"\nAll 10 repositories completed in {total_wall_time:.2f}s.\n")

    # Save master results
    master_results_file = os.path.join(RESULTS_DIR, "master_retrieval_results.json")
    with open(master_results_file, "w", encoding="utf-8") as mf:
        json.dump(all_results, mf, indent=2)

    # 3. Analyze Results
    positives = [q for q in all_results if not q.get("is_negative", False)]
    total_pos = len(positives)
    print("=" * 80)
    print(f"RETRIEVAL BENCHMARK SUMMARY (N = {len(all_results)} Total, {total_pos} Positive)")
    print("=" * 80)

    for mode in ["hybrid", "lexical_only", "semantic_only"]:
        hit1 = sum(1 for q in positives if q[mode]["hit_at_1"])
        hit5 = sum(1 for q in positives if q[mode]["hit_at_5"])
        hit10 = sum(1 for q in positives if q[mode]["hit_at_10"])
        hit20 = sum(1 for q in positives if q[mode]["hit_at_20"])
        hit50 = sum(1 for q in positives if q[mode]["hit_at_50"])
        mrr_sum = sum(q[mode]["mrr"] for q in positives)
        mrr = mrr_sum / total_pos if total_pos > 0 else 0

        print(f"\nMode: {mode.upper()}")
        print(f"  Hit@1:   {hit1:2d} / {total_pos} ({hit1/total_pos*100:5.1f}%)")
        print(f"  Hit@5:   {hit5:2d} / {total_pos} ({hit5/total_pos*100:5.1f}%)")
        print(f"  Hit@10:  {hit10:2d} / {total_pos} ({hit10/total_pos*100:5.1f}%)")
        print(f"  Hit@20:  {hit20:2d} / {total_pos} ({hit20/total_pos*100:5.1f}%)")
        print(f"  Hit@50:  {hit50:2d} / {total_pos} ({hit50/total_pos*100:5.1f}%)")
        print(f"  MRR:     {mrr:.4f}")

    # Rank buckets for Hybrid
    rank_buckets = {"1": 0, "2-5": 0, "6-10": 0, "11-20": 0, "21-50": 0, ">50 / absent": 0}
    for q in positives:
        r = q["hybrid"]["target_rank"]
        if r == 1:
            rank_buckets["1"] += 1
        elif 2 <= r <= 5:
            rank_buckets["2-5"] += 1
        elif 6 <= r <= 10:
            rank_buckets["6-10"] += 1
        elif 11 <= r <= 20:
            rank_buckets["11-20"] += 1
        elif 21 <= r <= 50:
            rank_buckets["21-50"] += 1
        else:
            rank_buckets[">50 / absent"] += 1

    print("\n" + "=" * 80)
    print("HYBRID TARGET RANK DISTRIBUTION")
    print("=" * 80)
    for bucket, count in rank_buckets.items():
        pct = (count / total_pos) * 100
        print(f"  {bucket:15s}: {count:2d} ({pct:5.1f}%)")

    # Latencies
    avg_qu = sum(q["query_understanding_ms"] for q in all_results) / len(all_results)
    avg_lex = sum(q["lexical_ms"] for q in all_results) / len(all_results)
    avg_sem = sum(q["semantic_ms"] for q in all_results) / len(all_results)
    avg_fuse = sum(q["fusion_ms"] for q in all_results) / len(all_results)
    avg_tot = sum(q["total_retrieval_ms"] for q in all_results) / len(all_results)

    print("\n" + "=" * 80)
    print("RETRIEVAL LATENCY BREAKDOWN (ms)")
    print("=" * 80)
    print(f"  Query Understanding: {avg_qu:6.3f} ms")
    print(f"  Lexical Retrieval:   {avg_lex:6.3f} ms")
    print(f"  Semantic Retrieval:  {avg_sem:6.3f} ms")
    print(f"  Hybrid Fusion:       {avg_fuse:6.3f} ms")
    print(f"  Total Retrieval:     {avg_tot:6.3f} ms")


if __name__ == "__main__":
    main()
