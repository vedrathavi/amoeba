#!/usr/bin/env python3
"""
Amoeba Phase 8.2.9 — Bounded Typed Relationship Expansion Experiment Runner
Evaluates Depth 0, Depth 1, and Depth 2 across all 10 repositories (70 benchmark questions).
Generates detailed repository-wise, relationship-wise, and question-level delta analysis.
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
RESULTS_DIR = os.path.join(BENCHMARK_DIR, "results", "expansion")
EXE_PATH = r"d:\amoeba\build\amoeba_expansion_benchmark.exe"


def main():
    os.makedirs(RESULTS_DIR, exist_ok=True)
    print("=" * 80)
    print("Amoeba Phase 8.2.9 — Bounded Relationship Expansion Benchmark Runner")
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

    all_question_results = []
    repo_execution_times = {}
    start_total_time = time.time()

    # 2. Execute repository by repository
    for repo_idx, (repo_name, q_list) in enumerate(by_repo.items(), 1):
        slug = repo_name.split("/")[-1]
        repo_dir = os.path.join(REPOS_DIR, slug)
        temp_tsv = os.path.join(RESULTS_DIR, f"temp_{slug}_queries.tsv")
        temp_out = os.path.join(RESULTS_DIR, f"temp_{slug}_results.json")

        print(f"[{repo_idx}/10] Running Benchmark on: {repo_name} ({len(q_list)} queries)...")

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
        repo_execution_times[repo_name] = t_elapsed

        if res.returncode != 0:
            print(f"  ERROR executing benchmark on {slug}:")
            print(res.stderr)
            continue

        print(f"  Finished {slug} in {t_elapsed:.2f}s.")

        if os.path.exists(temp_out):
            with open(temp_out, "r", encoding="utf-8") as f_out:
                repo_res = json.load(f_out)
                all_question_results.extend(repo_res)

    total_wall_time = time.time() - start_total_time
    print(f"\nAll 10 repositories completed in {total_wall_time:.2f}s.\n")

    # Save master raw results
    master_results_file = os.path.join(RESULTS_DIR, "master_expansion_results.json")
    with open(master_results_file, "w", encoding="utf-8") as mf:
        json.dump(all_question_results, mf, indent=2)

    # 3. Comprehensive Analysis
    total_q = len(all_question_results)
    positives = [q for q in all_question_results if not q.get("is_negative", False)]
    negatives = [q for q in all_question_results if q.get("is_negative", False)]

    def compute_stats_for_depth(depth_key):
        total_correct = sum(1 for q in all_question_results if q[depth_key]["is_correct"])
        pos_correct = sum(1 for q in positives if q[depth_key]["is_correct"])
        neg_correct = sum(1 for q in negatives if q[depth_key]["is_correct"])
        suff_passed = sum(1 for q in positives if q[depth_key]["sufficiency_passed"])
        grounded_passed = sum(1 for q in positives if q[depth_key]["answer_is_grounded"])
        
        hit_1 = sum(1 for q in positives if q.get("hit_at_1", False))
        hit_5 = sum(1 for q in positives if q.get("hit_at_5", False))
        hit_10 = sum(1 for q in positives if q.get("hit_at_10", False))

        # Failure breakdown on positive questions
        ret_fails = sum(1 for q in positives if not q.get("hit_at_10", False))
        ev_fails = sum(1 for q in positives if q.get("hit_at_10", False) and not q[depth_key]["sufficiency_passed"])
        reason_fails = sum(1 for q in positives if q.get("hit_at_10", False) and q[depth_key]["sufficiency_passed"] and not q[depth_key]["answer_is_grounded"])

        avg_units = sum(q[depth_key]["context_units"] for q in all_question_results) / total_q if total_q else 0
        avg_added = sum(q[depth_key]["added_units"] for q in all_question_results) / total_q if total_q else 0
        avg_chars = sum(q[depth_key]["context_characters"] for q in all_question_results) / total_q if total_q else 0
        max_chars = max(q[depth_key]["context_characters"] for q in all_question_results) if total_q else 0

        avg_assem_lat = sum(q[depth_key]["assembly_latency_ms"] for q in all_question_results) / total_q if total_q else 0
        avg_suff_lat = sum(q[depth_key]["sufficiency_latency_ms"] for q in all_question_results) / total_q if total_q else 0
        avg_tot_lat = sum(q[depth_key]["total_latency_ms"] for q in all_question_results) / total_q if total_q else 0

        # Negative false passes
        neg_false_pass = sum(1 for q in negatives if q[depth_key]["sufficiency_passed"])

        return {
            "total_correct": total_correct,
            "overall_accuracy_pct": total_correct / total_q * 100 if total_q else 0,
            "positive_correct": pos_correct,
            "positive_accuracy_pct": pos_correct / len(positives) * 100 if positives else 0,
            "negative_correct": neg_correct,
            "negative_accuracy_pct": neg_correct / len(negatives) * 100 if negatives else 0,
            "sufficiency_pass_rate_pct": suff_passed / len(positives) * 100 if positives else 0,
            "groundedness_rate_pct": grounded_passed / len(positives) * 100 if positives else 0,
            "hit_at_1_pct": hit_1 / len(positives) * 100 if positives else 0,
            "hit_at_5_pct": hit_5 / len(positives) * 100 if positives else 0,
            "hit_at_10_pct": hit_10 / len(positives) * 100 if positives else 0,
            "retrieval_failures": ret_fails,
            "evidence_failures": ev_fails,
            "reasoning_failures": reason_fails,
            "avg_context_units": avg_units,
            "avg_added_units": avg_added,
            "avg_context_chars": avg_chars,
            "max_context_chars": max_chars,
            "avg_assembly_latency_ms": avg_assem_lat,
            "avg_sufficiency_latency_ms": avg_suff_lat,
            "avg_total_latency_ms": avg_tot_lat,
            "negative_false_passes": neg_false_pass,
        }

    stats_d0 = compute_stats_for_depth("depth_0")
    stats_d1 = compute_stats_for_depth("depth_1")
    stats_d2 = compute_stats_for_depth("depth_2")

    # 4. Repository-wise Breakdown
    repo_groups = defaultdict(list)
    for q in all_question_results:
        repo_groups[q["repository"]].append(q)

    repo_summaries = {}
    for r_name, q_list in repo_groups.items():
        pos_list = [q for q in q_list if not q.get("is_negative", False)]
        neg_list = [q for q in q_list if q.get("is_negative", False)]
        n_pos = len(pos_list)
        n_neg = len(neg_list)

        d0_c = sum(1 for q in q_list if q["depth_0"]["is_correct"])
        d1_c = sum(1 for q in q_list if q["depth_1"]["is_correct"])
        d2_c = sum(1 for q in q_list if q["depth_2"]["is_correct"])

        d0_suff = sum(1 for q in pos_list if q["depth_0"]["sufficiency_passed"])
        d1_suff = sum(1 for q in pos_list if q["depth_1"]["sufficiency_passed"])
        d2_suff = sum(1 for q in pos_list if q["depth_2"]["sufficiency_passed"])

        d0_g = sum(1 for q in pos_list if q["depth_0"]["answer_is_grounded"])
        d1_g = sum(1 for q in pos_list if q["depth_1"]["answer_is_grounded"])
        d2_g = sum(1 for q in pos_list if q["depth_2"]["answer_is_grounded"])

        hit1 = sum(1 for q in pos_list if q.get("hit_at_1", False))
        hit5 = sum(1 for q in pos_list if q.get("hit_at_5", False))
        hit10 = sum(1 for q in pos_list if q.get("hit_at_10", False))

        d0_lat = sum(q["depth_0"]["total_latency_ms"] for q in q_list) / len(q_list) if q_list else 0
        d1_lat = sum(q["depth_1"]["total_latency_ms"] for q in q_list) / len(q_list) if q_list else 0
        d2_lat = sum(q["depth_2"]["total_latency_ms"] for q in q_list) / len(q_list) if q_list else 0

        d1_units = sum(q["depth_1"]["context_units"] for q in q_list) / len(q_list) if q_list else 0
        d1_chars = sum(q["depth_1"]["context_characters"] for q in q_list) / len(q_list) if q_list else 0

        d0_ret_fails = sum(1 for q in pos_list if not q.get("hit_at_10", False))
        d0_ev_fails = sum(1 for q in pos_list if q.get("hit_at_10", False) and not q["depth_0"]["sufficiency_passed"])

        # Determine dominant problem
        if d0_ret_fails > d0_ev_fails and d0_ret_fails > (n_pos - d0_ret_fails - d0_ev_fails):
            dominant_problem = "retrieval"
        elif d0_ev_fails > d0_ret_fails:
            dominant_problem = "evidence"
        elif d0_ret_fails > 0 and d0_ev_fails > 0:
            dominant_problem = "mixed"
        else:
            dominant_problem = "retrieval"

        improved_d0_to_d1 = sum(1 for q in q_list if not q["depth_0"]["is_correct"] and q["depth_1"]["is_correct"])
        regressed_d0_to_d1 = sum(1 for q in q_list if q["depth_0"]["is_correct"] and not q["depth_1"]["is_correct"])
        unchanged_d0_to_d1 = sum(1 for q in q_list if q["depth_0"]["is_correct"] == q["depth_1"]["is_correct"])

        improved_d1_to_d2 = sum(1 for q in q_list if not q["depth_1"]["is_correct"] and q["depth_2"]["is_correct"])
        regressed_d1_to_d2 = sum(1 for q in q_list if q["depth_1"]["is_correct"] and not q["depth_2"]["is_correct"])

        repo_summaries[r_name] = {
            "total_questions": len(q_list),
            "positive_questions": n_pos,
            "negative_questions": n_neg,
            "baseline_correctness": f"{d0_c}/{len(q_list)} ({d0_c/len(q_list)*100:.1f}%)",
            "depth_1_correctness": f"{d1_c}/{len(q_list)} ({d1_c/len(q_list)*100:.1f}%)",
            "depth_2_correctness": f"{d2_c}/{len(q_list)} ({d2_c/len(q_list)*100:.1f}%)",
            "hit_at_1": f"{hit1}/{n_pos} ({hit1/n_pos*100:.1f}%)" if n_pos else "N/A",
            "hit_at_5": f"{hit5}/{n_pos} ({hit5/n_pos*100:.1f}%)" if n_pos else "N/A",
            "hit_at_10": f"{hit10}/{n_pos} ({hit10/n_pos*100:.1f}%)" if n_pos else "N/A",
            "baseline_sufficiency": f"{d0_suff}/{n_pos} ({d0_suff/n_pos*100:.1f}%)" if n_pos else "N/A",
            "depth_1_sufficiency": f"{d1_suff}/{n_pos} ({d1_suff/n_pos*100:.1f}%)" if n_pos else "N/A",
            "depth_2_sufficiency": f"{d2_suff}/{n_pos} ({d2_suff/n_pos*100:.1f}%)" if n_pos else "N/A",
            "baseline_groundedness": f"{d0_g}/{n_pos} ({d0_g/n_pos*100:.1f}%)" if n_pos else "N/A",
            "depth_1_groundedness": f"{d1_g}/{n_pos} ({d1_g/n_pos*100:.1f}%)" if n_pos else "N/A",
            "depth_2_groundedness": f"{d2_g}/{n_pos} ({d2_g/n_pos*100:.1f}%)" if n_pos else "N/A",
            "baseline_latency_ms": round(d0_lat, 2),
            "depth_1_latency_ms": round(d1_lat, 2),
            "depth_2_latency_ms": round(d2_lat, 2),
            "depth_1_avg_context_units": round(d1_units, 2),
            "depth_1_avg_context_chars": round(d1_chars, 1),
            "expansion_overhead_ms": round(d1_lat - d0_lat, 2),
            "improved_d0_to_d1": improved_d0_to_d1,
            "regressed_d0_to_d1": regressed_d0_to_d1,
            "unchanged_d0_to_d1": unchanged_d0_to_d1,
            "improved_d1_to_d2": improved_d1_to_d2,
            "regressed_d1_to_d2": regressed_d1_to_d2,
            "dominant_problem": dominant_problem,
            "build_time_s": round(repo_execution_times.get(r_name, 0), 2),
        }

    # 5. Question-Level Delta Analysis
    question_deltas_d0_d1 = []
    question_deltas_d1_d2 = []

    for q in all_question_results:
        d0 = q["depth_0"]
        d1 = q["depth_1"]
        d2 = q["depth_2"]

        if d0["is_correct"] != d1["is_correct"] or d0["sufficiency_passed"] != d1["sufficiency_passed"]:
            question_deltas_d0_d1.append({
                "question_id": q["question_id"],
                "repository": q["repository"],
                "taxonomy": q["taxonomy"],
                "is_negative": q.get("is_negative", False),
                "d0_correct": d0["is_correct"],
                "d1_correct": d1["is_correct"],
                "d0_sufficiency": d0["sufficiency_passed"],
                "d1_sufficiency": d1["sufficiency_passed"],
                "d0_reason": d0["sufficiency_reason"],
                "d1_reason": d1["sufficiency_reason"],
                "added_units": d1["added_units"],
                "context_chars": d1["context_characters"],
                "latency_d0": round(d0["total_latency_ms"], 2),
                "latency_d1": round(d1["total_latency_ms"], 2),
                "expanded_provenance": d1["expanded_provenance"],
            })

        if d1["is_correct"] != d2["is_correct"] or d1["sufficiency_passed"] != d2["sufficiency_passed"]:
            question_deltas_d1_d2.append({
                "question_id": q["question_id"],
                "repository": q["repository"],
                "taxonomy": q["taxonomy"],
                "d1_correct": d1["is_correct"],
                "d2_correct": d2["is_correct"],
                "d1_sufficiency": d1["sufficiency_passed"],
                "d2_sufficiency": d2["sufficiency_passed"],
                "added_units_d2": d2["added_units"],
                "expanded_provenance_d2": d2["expanded_provenance"],
            })

    # 6. Relationship-wise analysis
    rel_counts = defaultdict(lambda: {"considered": 0, "selected": 0, "improved": 0, "regressed": 0, "noisy": 0})
    for q in all_question_results:
        d0_c = q["depth_0"]["is_correct"]
        d1_c = q["depth_1"]["is_correct"]
        prov_list = q["depth_1"]["expanded_provenance"]
        for p in prov_list:
            # Format: "<RelType> (<Direction>) from `<Seed>` -> `<Target>`"
            rel_type = p.split(" ")[0]
            rel_counts[rel_type]["selected"] += 1
            if not d0_c and d1_c:
                rel_counts[rel_type]["improved"] += 1
            elif d0_c and not d1_c:
                rel_counts[rel_type]["regressed"] += 1
            else:
                rel_counts[rel_type]["noisy"] += 1

    # Compile master analysis payload
    analysis_payload = {
        "aggregate_stats": {
            "depth_0": stats_d0,
            "depth_1": stats_d1,
            "depth_2": stats_d2,
        },
        "repository_summaries": repo_summaries,
        "question_deltas_d0_to_d1": question_deltas_d0_d1,
        "question_deltas_d1_to_d2": question_deltas_d1_d2,
        "relationship_utility": dict(rel_counts),
    }

    analysis_file = os.path.join(RESULTS_DIR, "master_expansion_analysis.json")
    with open(analysis_file, "w", encoding="utf-8") as af:
        json.dump(analysis_payload, af, indent=2)

    # Print Formatted Report
    print("=" * 80)
    print("AGGREGATE BENCHMARK RESULTS COMPARISON (70 Questions / 10 Repos)")
    print("=" * 80)
    print(f"{'Metric':<35} | {'Depth 0 (Baseline)':<20} | {'Depth 1 (1-Hop)':<20} | {'Depth 2 (2-Hop)':<20}")
    print("-" * 105)
    print(f"{'Overall Correctness':<35} | {stats_d0['total_correct']}/{total_q} ({stats_d0['overall_accuracy_pct']:.1f}%)        | {stats_d1['total_correct']}/{total_q} ({stats_d1['overall_accuracy_pct']:.1f}%)        | {stats_d2['total_correct']}/{total_q} ({stats_d2['overall_accuracy_pct']:.1f}%)")
    print(f"{'Positive Accuracy':<35} | {stats_d0['positive_correct']}/{len(positives)} ({stats_d0['positive_accuracy_pct']:.1f}%)        | {stats_d1['positive_correct']}/{len(positives)} ({stats_d1['positive_accuracy_pct']:.1f}%)        | {stats_d2['positive_correct']}/{len(positives)} ({stats_d2['positive_accuracy_pct']:.1f}%)")
    print(f"{'Negative Refusal Accuracy':<35} | {stats_d0['negative_correct']}/{len(negatives)} ({stats_d0['negative_accuracy_pct']:.1f}%)      | {stats_d1['negative_correct']}/{len(negatives)} ({stats_d1['negative_accuracy_pct']:.1f}%)      | {stats_d2['negative_correct']}/{len(negatives)} ({stats_d2['negative_accuracy_pct']:.1f}%)")
    print(f"{'Sufficiency Pass Rate':<35} | {stats_d0['sufficiency_pass_rate_pct']:.1f}%                | {stats_d1['sufficiency_pass_rate_pct']:.1f}%                | {stats_d2['sufficiency_pass_rate_pct']:.1f}%")
    print(f"{'Groundedness Rate':<35} | {stats_d0['groundedness_rate_pct']:.1f}%                | {stats_d1['groundedness_rate_pct']:.1f}%                | {stats_d2['groundedness_rate_pct']:.1f}%")
    print(f"{'Hit@10 Rate':<35} | {stats_d0['hit_at_10_pct']:.1f}%                | {stats_d1['hit_at_10_pct']:.1f}%                | {stats_d2['hit_at_10_pct']:.1f}%")
    print(f"{'Retrieval Failures':<35} | {stats_d0['retrieval_failures']}                      | {stats_d1['retrieval_failures']}                      | {stats_d2['retrieval_failures']}")
    print(f"{'Evidence Failures':<35} | {stats_d0['evidence_failures']}                      | {stats_d1['evidence_failures']}                      | {stats_d2['evidence_failures']}")
    print(f"{'Reasoning Failures':<35} | {stats_d0['reasoning_failures']}                       | {stats_d1['reasoning_failures']}                       | {stats_d2['reasoning_failures']}")
    print(f"{'Negative False Passes':<35} | {stats_d0['negative_false_passes']}                       | {stats_d1['negative_false_passes']}                       | {stats_d2['negative_false_passes']}")
    print(f"{'Avg Total Latency (ms)':<35} | {stats_d0['avg_total_latency_ms']:.2f} ms             | {stats_d1['avg_total_latency_ms']:.2f} ms             | {stats_d2['avg_total_latency_ms']:.2f} ms")
    print(f"{'Avg Context Units':<35} | {stats_d0['avg_context_units']:.2f}                 | {stats_d1['avg_context_units']:.2f}                 | {stats_d2['avg_context_units']:.2f}")
    print(f"{'Avg Context Chars':<35} | {stats_d0['avg_context_chars']:.1f}               | {stats_d1['avg_context_chars']:.1f}               | {stats_d2['avg_context_chars']:.1f}")
    print(f"{'Max Context Chars':<35} | {stats_d0['max_context_chars']}                  | {stats_d1['max_context_chars']}                  | {stats_d2['max_context_chars']}")
    print("=" * 80 + "\n")

    print("=" * 80)
    print("REPOSITORY-WISE BREAKDOWN")
    print("=" * 80)
    for r_name, summary in repo_summaries.items():
        slug = r_name.split("/")[-1]
        print(f"Repo: {slug:<15} | Base: {summary['baseline_correctness']:<12} | D1: {summary['depth_1_correctness']:<12} | D2: {summary['depth_2_correctness']:<12} | Prob: {summary['dominant_problem']:<10} | Overhead: +{summary['expansion_overhead_ms']}ms")

    print("\n" + "=" * 80)
    print(f"QUESTION DELTAS (Depth 0 -> Depth 1): {len(question_deltas_d0_d1)} changed")
    print("=" * 80)
    for delta in question_deltas_d0_d1:
        stat = "IMPROVED" if (not delta["d0_correct"] and delta["d1_correct"]) else ("REGRESSED" if (delta["d0_correct"] and not delta["d1_correct"]) else "SUFF_CHANGE")
        print(f"[{stat}] QID: {delta['question_id']} ({delta['repository']}) | +{delta['added_units']} units | D0->D1 latency: {delta['latency_d0']}ms -> {delta['latency_d1']}ms")
        for p in delta["expanded_provenance"]:
            print(f"    └── Prov: {p}")

    print("\n" + "=" * 80)
    print(f"QUESTION DELTAS (Depth 1 -> Depth 2): {len(question_deltas_d1_d2)} changed")
    print("=" * 80)
    for delta in question_deltas_d1_d2:
        stat = "IMPROVED" if (not delta["d1_correct"] and delta["d2_correct"]) else ("REGRESSED" if (delta["d1_correct"] and not delta["d2_correct"]) else "SUFF_CHANGE")
        print(f"[{stat}] QID: {delta['question_id']} ({delta['repository']}) | D1: {delta['d1_correct']} -> D2: {delta['d2_correct']}")


if __name__ == "__main__":
    main()
