import argparse
import datetime
import json
import os
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8")

BENCHMARK_DIR = r"d:\amoeba\benchmarks\repoprobe_v1"
REPOS_DIR = os.path.join(BENCHMARK_DIR, "repos")
RESULTS_DIR = os.path.join(BENCHMARK_DIR, "results", "latest")
EXE_PATH = r"d:\amoeba\build\amoeba_repoprobe_benchmark.exe"

def get_git_commit(cwd=r"d:\amoeba"):
    res = subprocess.run("git rev-parse HEAD", cwd=cwd, shell=True, capture_output=True, text=True)
    return res.stdout.strip() if res.returncode == 0 else "unknown"

def run_single_config(config_name, output_subdir=None):
    if output_subdir is None:
        target_results_dir = RESULTS_DIR
    else:
        target_results_dir = os.path.join(BENCHMARK_DIR, "results", output_subdir)
    os.makedirs(target_results_dir, exist_ok=True)

    print(f"\n=================================================================")
    print(f"Running Configuration: {config_name}")
    print(f"Target Results Directory: {target_results_dir}")
    print(f"=================================================================\n")

    # 1. Load questions & ground truth
    with open(os.path.join(BENCHMARK_DIR, "questions.json"), "r", encoding="utf-8") as f:
        questions = json.load(f)
    with open(os.path.join(BENCHMARK_DIR, "ground_truth.json"), "r", encoding="utf-8") as f:
        ground_truth = json.load(f)

    gt_by_id = {g["question_id"]: g for g in ground_truth}

    # Group by repo
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

    # 2. Run benchmark repository by repository
    for repo_idx, (repo_name, q_list) in enumerate(by_repo.items(), 1):
        slug = repo_name.split("/")[-1]
        repo_dir = os.path.join(REPOS_DIR, slug)
        temp_tsv = os.path.join(target_results_dir, f"temp_{slug}_queries.tsv")
        temp_out = os.path.join(target_results_dir, f"temp_{slug}_results.json")

        print(f"[{repo_idx}/10] Evaluating {repo_name} ({len(q_list)} queries)...")

        # Write TSV query payload
        with open(temp_tsv, "w", encoding="utf-8") as tf:
            for q in q_list:
                files_str = "|".join(q["expected_files"])
                syms_str = "|".join(q["expected_symbols"])
                rels_str = "|".join(q["expected_relationships"])
                concepts_str = "|".join(q["expected_concepts"])
                row = f"{q['question_id']}\t{q['repo_name']}\t{q['source']}\t{q['taxonomy']}\t{q['difficulty']}\t{q['question']}\t{files_str}\t{syms_str}\t{rels_str}\t{concepts_str}\n"
                tf.write(row)

        cmd = [EXE_PATH, repo_dir, temp_tsv, temp_out, config_name]
        t0 = time.time()
        res = subprocess.run(cmd, cwd=r"d:\amoeba", capture_output=True, text=True, encoding="utf-8", errors="replace")
        t_elapsed = time.time() - t0

        if res.returncode != 0:
            print(f"  ERROR evaluating {repo_name}: {res.stderr}")
            continue

        print(f"  Completed {len(q_list)} questions in {t_elapsed:.2f} s.")

        if os.path.exists(temp_out):
            with open(temp_out, "r", encoding="utf-8") as rf:
                repo_results = json.load(rf)
                all_results.extend(repo_results)

            try:
                os.remove(temp_tsv)
                os.remove(temp_out)
            except Exception:
                pass

    total_duration = time.time() - start_total_time
    print(f"\nAll evaluations for {config_name} completed in {total_duration:.2f} seconds.\n")

    # 3. Aggregate Metrics & Diagnostic Breakdown
    total_q = len(all_results)
    pos_q = sum(1 for r in all_results if r["expected_files"] or r["expected_symbols"])
    neg_q = total_q - pos_q

    hit_at_1 = sum(1 for r in all_results if r.get("retrieval_hit_at_1"))
    hit_at_5 = sum(1 for r in all_results if r.get("retrieval_hit_at_5"))
    hit_at_10 = sum(1 for r in all_results if r.get("retrieval_hit_at_10"))
    hit_at_20 = sum(1 for r in all_results if r.get("retrieval_hit_at_20"))
    hit_at_50 = sum(1 for r in all_results if r.get("retrieval_hit_at_50"))

    hop0_hit_at_1 = sum(1 for r in all_results if r.get("hop0_hit_at_1"))
    hop0_hit_at_5 = sum(1 for r in all_results if r.get("hop0_hit_at_5"))
    hop0_hit_at_10 = sum(1 for r in all_results if r.get("hop0_hit_at_10"))
    hop0_hit_at_20 = sum(1 for r in all_results if r.get("hop0_hit_at_20"))
    hop0_hit_at_50 = sum(1 for r in all_results if r.get("hop0_hit_at_50"))

    # Compute MRR for positive questions
    rr_sum = 0.0
    for r in all_results:
        if r["expected_files"] or r["expected_symbols"]:
            tr = r.get("target_rank_in_hop0", 0)
            if tr > 0:
                rr_sum += (1.0 / tr)
    mrr = (rr_sum / pos_q) if pos_q else 0.0

    grounded_count = sum(1 for r in all_results if r.get("grounded"))
    correct_count = sum(1 for r in all_results if r.get("correctness") == "correct")
    neg_correct = sum(1 for r in all_results if (not r["expected_files"] and not r["expected_symbols"] and r.get("correctness") == "correct"))

    hit_1_pct = (100.0 * hit_at_1 / pos_q) if pos_q else 0.0
    hit_5_pct = (100.0 * hit_at_5 / pos_q) if pos_q else 0.0
    hit_10_pct = (100.0 * hit_at_10 / pos_q) if pos_q else 0.0
    hit_20_pct = (100.0 * hit_at_20 / pos_q) if pos_q else 0.0
    hit_50_pct = (100.0 * hit_at_50 / pos_q) if pos_q else 0.0

    hop0_1_pct = (100.0 * hop0_hit_at_1 / pos_q) if pos_q else 0.0
    hop0_5_pct = (100.0 * hop0_hit_at_5 / pos_q) if pos_q else 0.0
    hop0_10_pct = (100.0 * hop0_hit_at_10 / pos_q) if pos_q else 0.0
    hop0_20_pct = (100.0 * hop0_hit_at_20 / pos_q) if pos_q else 0.0
    hop0_50_pct = (100.0 * hop0_hit_at_50 / pos_q) if pos_q else 0.0

    grounded_pct = (100.0 * grounded_count / total_q) if total_q else 0.0
    correct_pct = (100.0 * correct_count / total_q) if total_q else 0.0
    neg_acc_pct = (100.0 * neg_correct / neg_q) if neg_q else 100.0

    avg_retr_ms = sum(r.get("retrieval_latency_ms", 0.0) for r in all_results) / total_q if total_q else 0.0
    avg_seed_ms = sum(r.get("seed_selection_latency_ms", 0.0) for r in all_results) / total_q if total_q else 0.0
    avg_invest_ms = sum(r.get("investigation_latency_ms", 0.0) for r in all_results) / total_q if total_q else 0.0
    avg_suff_ms = sum(r.get("sufficiency_latency_ms", 0.0) for r in all_results) / total_q if total_q else 0.0
    avg_reason_ms = sum(r.get("reasoning_latency_ms", 0.0) for r in all_results) / total_q if total_q else 0.0
    avg_total_ms = sum(r.get("total_latency_ms", 0.0) for r in all_results) / total_q if total_q else 0.0

    failure_counts = {}
    for r in all_results:
        cat = r.get("failure_category", "none")
        failure_counts[cat] = failure_counts.get(cat, 0) + 1

    source_stats = {}
    for r in all_results:
        src = r.get("source", "repoprobe")
        source_stats.setdefault(src, {"total": 0, "correct": 0, "hit10": 0, "pos": 0})
        source_stats[src]["total"] += 1
        if r.get("correctness") == "correct":
            source_stats[src]["correct"] += 1
        if r["expected_files"] or r["expected_symbols"]:
            source_stats[src]["pos"] += 1
            if r.get("retrieval_hit_at_10"):
                source_stats[src]["hit10"] += 1

    repo_stats = {}
    for r in all_results:
        rp = r.get("repository", "")
        repo_stats.setdefault(rp, {"total": 0, "correct": 0, "hit10": 0, "hit50": 0, "pos": 0})
        repo_stats[rp]["total"] += 1
        if r.get("correctness") == "correct":
            repo_stats[rp]["correct"] += 1
        if r["expected_files"] or r["expected_symbols"]:
            repo_stats[rp]["pos"] += 1
            if r.get("retrieval_hit_at_10"):
                repo_stats[rp]["hit10"] += 1
            if r.get("retrieval_hit_at_50"):
                repo_stats[rp]["hit50"] += 1

    # 4. Save results
    questions_out_path = os.path.join(target_results_dir, "questions.json")
    with open(questions_out_path, "w", encoding="utf-8") as f:
        json.dump(all_results, f, indent=2, ensure_ascii=False)

    failures_list = [r for r in all_results if r.get("failure_category") != "none"]
    failures_out_path = os.path.join(target_results_dir, "failures.json")
    with open(failures_out_path, "w", encoding="utf-8") as f:
        json.dump(failures_list, f, indent=2, ensure_ascii=False)

    summary_data = {
        "configuration": config_name,
        "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "git_commit": get_git_commit(),
        "total_questions": total_q,
        "positive_questions": pos_q,
        "negative_questions": neg_q,
        "metrics": {
            "correctness_rate_pct": correct_pct,
            "groundedness_rate_pct": grounded_pct,
            "negative_refusal_accuracy_pct": neg_acc_pct,
            "retrieval_hit_at_1_pct": hit_1_pct,
            "retrieval_hit_at_5_pct": hit_5_pct,
            "retrieval_hit_at_10_pct": hit_10_pct,
            "retrieval_hit_at_20_pct": hit_20_pct,
            "retrieval_hit_at_50_pct": hit_50_pct,
            "hop0_hit_at_1_pct": hop0_1_pct,
            "hop0_hit_at_5_pct": hop0_5_pct,
            "hop0_hit_at_10_pct": hop0_10_pct,
            "hop0_hit_at_20_pct": hop0_20_pct,
            "hop0_hit_at_50_pct": hop0_50_pct,
            "mrr": mrr,
            "avg_retrieval_latency_ms": avg_retr_ms,
            "avg_seed_selection_latency_ms": avg_seed_ms,
            "avg_investigation_latency_ms": avg_invest_ms,
            "avg_sufficiency_latency_ms": avg_suff_ms,
            "avg_reasoning_latency_ms": avg_reason_ms,
            "avg_total_latency_ms": avg_total_ms,
        },
        "failure_distribution": failure_counts,
        "breakdown_by_source": {
            src: {
                "total": s["total"],
                "correct": s["correct"],
                "accuracy_pct": (100.0 * s["correct"] / s["total"]) if s["total"] else 0.0,
                "hit10_pct": (100.0 * s["hit10"] / s["pos"]) if s["pos"] else 0.0
            }
            for src, s in source_stats.items()
        },
        "breakdown_by_repository": {
            rp: {
                "total": s["total"],
                "correct": s["correct"],
                "accuracy_pct": (100.0 * s["correct"] / s["total"]) if s["total"] else 0.0,
                "hit10_pct": (100.0 * s["hit10"] / s["pos"]) if s["pos"] else 0.0,
                "hit50_pct": (100.0 * s["hit50"] / s["pos"]) if s["pos"] else 0.0
            }
            for rp, s in repo_stats.items()
        }
    }

    summary_out_path = os.path.join(target_results_dir, "summary.json")
    with open(summary_out_path, "w", encoding="utf-8") as f:
        json.dump(summary_data, f, indent=2, ensure_ascii=False)

    # 5. Print Console Summary
    print("=================================================================")
    print(f"BENCHMARK SUMMARY — CONFIG: {config_name}")
    print("=================================================================")
    print(f"Total Evaluated Questions:  {total_q}")
    print(f"Overall Correctness Rate:   {correct_pct:.1f}% ({correct_count}/{total_q})")
    print(f"Groundedness Rate:          {grounded_pct:.1f}% ({grounded_count}/{total_q})")
    print(f"Negative Refusal Accuracy:  {neg_acc_pct:.1f}% ({neg_correct}/{neg_q})")
    print(f"Retrieval Hit@1:            {hit_1_pct:.1f}% ({hit_at_1}/{pos_q})")
    print(f"Retrieval Hit@5:            {hit_5_pct:.1f}% ({hit_at_5}/{pos_q})")
    print(f"Retrieval Hit@10:           {hit_10_pct:.1f}% ({hit_at_10}/{pos_q})")
    print(f"Retrieval Hit@20:           {hit_20_pct:.1f}% ({hit_at_20}/{pos_q})")
    print(f"Retrieval Hit@50:           {hit_50_pct:.1f}% ({hit_at_50}/{pos_q})")
    print(f"Hop-0 MRR:                  {mrr:.4f}")
    print(f"Average Total Latency:      {avg_total_ms:.1f} ms (Retr: {avg_retr_ms:.1f} ms, SeedSel: {avg_seed_ms:.2f} ms, Invest: {avg_invest_ms:.2f} ms, Suff: {avg_suff_ms:.1f} ms)")
    print("-----------------------------------------------------------------")
    print("Failure Categories Distribution:")
    for cat, cnt in sorted(failure_counts.items(), key=lambda x: x[1], reverse=True):
        print(f"  - {cat:22}: {cnt:2} ({100.0 * cnt / total_q:.1f}%)")
    print("=================================================================")

    return summary_data

def main():
    parser = argparse.ArgumentParser(description="Amoeba RepoProbe v1 Benchmark Harness")
    parser.add_argument("--config", default="top50_bounded_investigation",
                        choices=["baseline_top10", "top50_no_investigation", "top50_bounded_investigation", "all"],
                        help="Benchmark configuration mode to execute")
    args = parser.parse_args()

    if args.config == "all":
        configs = ["baseline_top10", "top50_no_investigation", "top50_bounded_investigation"]
        summaries = {}
        for cfg in configs:
            summaries[cfg] = run_single_config(cfg, output_subdir=cfg)
        # Also copy latest to default
        run_single_config("top50_bounded_investigation", output_subdir="latest")
    else:
        run_single_config(args.config, output_subdir="latest")

if __name__ == "__main__":
    main()
