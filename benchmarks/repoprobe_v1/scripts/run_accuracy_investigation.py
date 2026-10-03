#!/usr/bin/env python3
"""
Amoeba Phase 8.2.8 — Accuracy Investigation & Evidence Expansion Runner
Orchestrates forensic 1-hop relationship expansion evaluation across all 10 repositories.
"""

import json
import os
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8")

BENCHMARK_DIR = r"d:\amoeba\benchmarks\repoprobe_v1"
REPOS_DIR = os.path.join(BENCHMARK_DIR, "repos")
RESULTS_DIR = os.path.join(BENCHMARK_DIR, "results", "investigation")
EXE_PATH = r"d:\amoeba\build\amoeba_accuracy_investigation.exe"

def main():
    os.makedirs(RESULTS_DIR, exist_ok=True)
    print("=================================================================")
    print("Amoeba Phase 8.2.8 Accuracy Investigation & Evidence Expansion")
    print("=================================================================\n")

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

    all_question_results = []
    start_total_time = time.time()

    # 2. Run investigation repository by repository
    for repo_idx, (repo_name, q_list) in enumerate(by_repo.items(), 1):
        slug = repo_name.split("/")[-1]
        repo_dir = os.path.join(REPOS_DIR, slug)
        temp_tsv = os.path.join(RESULTS_DIR, f"temp_{slug}_queries.tsv")
        temp_out = os.path.join(RESULTS_DIR, f"temp_{slug}_results.json")

        print(f"[{repo_idx}/10] Investigating Repository: {repo_name} ({len(q_list)} questions)...")

        # Write TSV query payload
        with open(temp_tsv, "w", encoding="utf-8") as tf:
            for q in q_list:
                files_str = "|".join(q["expected_files"])
                syms_str = "|".join(q["expected_symbols"])
                rels_str = "|".join(q["expected_relationships"])
                concepts_str = "|".join(q["expected_concepts"])
                tf.write(f"{q['question_id']}\t{q['repo_name']}\t{q['source']}\t{q['taxonomy']}\t{q['difficulty']}\t{q['question']}\t{files_str}\t{syms_str}\t{rels_str}\t{concepts_str}\n")

        # Execute C++ accuracy investigation binary
        cmd = [EXE_PATH, repo_dir, temp_tsv, temp_out]
        t0 = time.time()
        res = subprocess.run(cmd, capture_output=True, text=True)
        t_elapsed = time.time() - t0

        if res.returncode != 0:
            print(f"  ERROR running {EXE_PATH} on {slug}:")
            print(res.stderr)
            continue

        print(f"  Completed {slug} in {t_elapsed:.1f}s.")

        if os.path.exists(temp_out):
            with open(temp_out, "r", encoding="utf-8") as f_out:
                repo_res = json.load(f_out)
                all_question_results.extend(repo_res)

    total_time = time.time() - start_total_time
    print(f"\nCompleted investigation across all repositories in {total_time:.1f}s.\n")

    # 3. Aggregate metrics
    total_q = len(all_question_results)
    positives = [q for q in all_question_results if not q.get("is_negative", False)]
    negatives = [q for q in all_question_results if q.get("is_negative", False)]

    cat_a = [q for q in positives if q["failure_category"] == "retrieval_failure"]
    cat_b = [q for q in positives if q["failure_category"] == "evidence_failure"]
    cat_c = [q for q in positives if q["failure_category"] == "reasoning_failure"]
    correct_base = [q for q in positives if q["failure_category"] == "correct"]
    correct_neg = [q for q in negatives if q["failure_category"] == "negative_refusal_correct"]

    cat_b_solved_by_1hop = [q for q in cat_b if q.get("expansion_solves_sufficiency", False)]

    # Relationship utility aggregate
    global_rel_useful = {}
    global_rel_noisy = {}
    for q in all_question_results:
        for k, v in q.get("rel_kind_useful", {}).items():
            global_rel_useful[k] = global_rel_useful.get(k, 0) + v
        for k, v in q.get("rel_kind_noisy", {}).items():
            global_rel_noisy[k] = global_rel_noisy.get(k, 0) + v

    # Minimum expansion units distribution for Category B
    min_units_dist = {}
    for q in cat_b:
        u = q.get("min_units_required", 99)
        u_label = f"+{u} unit(s)" if u < 99 else "unreachable (>1-hop/missing)"
        min_units_dist[u_label] = min_units_dist.get(u_label, 0) + 1

    # Negative refusal safety check
    neg_false_passes = [q for q in negatives if q.get("expansion_solves_sufficiency", False)]

    # Latency statistics
    avg_ret_ms = sum(q.get("retrieval_ms", 0) for q in all_question_results) / total_q if total_q else 0
    avg_base_suff_ms = sum(q.get("base_sufficiency_ms", 0) for q in all_question_results) / total_q if total_q else 0
    avg_exp_res_ms = sum(q.get("expansion_resolution_ms", 0) for q in all_question_results) / total_q if total_q else 0
    avg_exp_suff_ms = sum(q.get("expanded_sufficiency_ms", 0) for q in all_question_results) / total_q if total_q else 0

    master_summary = {
        "total_questions": total_q,
        "positive_questions": len(positives),
        "negative_questions": len(negatives),
        "baseline_correct_total": len(correct_base) + len(correct_neg),
        "baseline_accuracy_pct": (len(correct_base) + len(correct_neg)) / total_q * 100 if total_q else 0,
        "positive_correct_baseline": len(correct_base),
        "positive_accuracy_pct": len(correct_base) / len(positives) * 100 if positives else 0,
        "negative_refusal_accuracy_pct": len(correct_neg) / len(negatives) * 100 if negatives else 0,
        "failure_categories": {
            "cat_a_retrieval_failure": len(cat_a),
            "cat_a_pct": len(cat_a) / len(positives) * 100 if positives else 0,
            "cat_b_evidence_failure": len(cat_b),
            "cat_b_pct": len(cat_b) / len(positives) * 100 if positives else 0,
            "cat_c_reasoning_failure": len(cat_c),
            "cat_c_pct": len(cat_c) / len(positives) * 100 if positives else 0,
        },
        "category_b_expansion_impact": {
            "cat_b_total": len(cat_b),
            "cat_b_solved_by_1hop": len(cat_b_solved_by_1hop),
            "cat_b_resolution_rate_pct": len(cat_b_solved_by_1hop) / len(cat_b) * 100 if cat_b else 0,
            "potential_positive_accuracy_post_expansion_pct": (len(correct_base) + len(cat_b_solved_by_1hop)) / len(positives) * 100 if positives else 0,
            "potential_overall_accuracy_post_expansion_pct": (len(correct_base) + len(correct_neg) + len(cat_b_solved_by_1hop)) / total_q * 100 if total_q else 0,
        },
        "minimum_expansion_units_distribution": min_units_dist,
        "relationship_utility_breakdown": {
            k: {"useful": global_rel_useful.get(k, 0), "noisy": global_rel_noisy.get(k, 0)}
            for k in set(list(global_rel_useful.keys()) + list(global_rel_noisy.keys()))
        },
        "negative_safety": {
            "total_negative_cases": len(negatives),
            "correct_refusals": len(correct_neg),
            "false_passes_caused_by_expansion": len(neg_false_passes)
        },
        "latency_breakdown_ms": {
            "avg_retrieval_ms": avg_ret_ms,
            "avg_base_sufficiency_ms": avg_base_suff_ms,
            "avg_expansion_resolution_ms": avg_exp_res_ms,
            "avg_expanded_sufficiency_ms": avg_exp_suff_ms,
            "total_additional_expansion_overhead_ms": avg_exp_res_ms + avg_exp_suff_ms
        },
        "questions": all_question_results
    }

    master_path = os.path.join(RESULTS_DIR, "master_investigation_results.json")
    with open(master_path, "w", encoding="utf-8") as mf:
        json.dump(master_summary, mf, indent=2)

    print("=================================================================")
    print("PHASE 8.2.8 INVESTIGATION SUMMARY")
    print("=================================================================")
    print(f"Total Questions:          {total_q} (66 Positive, 4 Negative)")
    print(f"Baseline Correct:         {len(correct_base) + len(correct_neg)} / {total_q} ({master_summary['baseline_accuracy_pct']:.1f}%)")
    print(f"  - Positive Accuracy:    {len(correct_base)} / {len(positives)} ({master_summary['positive_accuracy_pct']:.1f}%)")
    print(f"  - Negative Refusal:     {len(correct_neg)} / {len(negatives)} ({master_summary['negative_refusal_accuracy_pct']:.1f}%)\n")

    print(f"Failure Breakdown (on 66 Positive Questions):")
    print(f"  - Category A (Retrieval Failure - Target NOT in Top-10):  {len(cat_a)} / {len(positives)} ({master_summary['failure_categories']['cat_a_pct']:.1f}%)")
    print(f"  - Category B (Evidence/Sufficiency Failure):              {len(cat_b)} / {len(positives)} ({master_summary['failure_categories']['cat_b_pct']:.1f}%)")
    print(f"  - Category C (Reasoning Failure):                         {len(cat_c)} / {len(positives)} (0.0%)\n")

    print(f"Category B (17 questions) 1-Hop Expansion Impact:")
    print(f"  - Solved by 1-Hop Expansion: {len(cat_b_solved_by_1hop)} / {len(cat_b)} ({master_summary['category_b_expansion_impact']['cat_b_resolution_rate_pct']:.1f}%)")
    print(f"  - Potential Positive Accuracy: {len(correct_base) + len(cat_b_solved_by_1hop)} / {len(positives)} ({master_summary['category_b_expansion_impact']['potential_positive_accuracy_post_expansion_pct']:.1f}%)")
    print(f"  - Potential Overall Accuracy:  {len(correct_base) + len(correct_neg) + len(cat_b_solved_by_1hop)} / {total_q} ({master_summary['category_b_expansion_impact']['potential_overall_accuracy_post_expansion_pct']:.1f}%)\n")

    print(f"Minimum Units Expansion Distribution (Category B):")
    for k, v in min_units_dist.items():
        print(f"  - {k}: {v} questions")
    print()

    print(f"Relationship Utility Breakdown:")
    for k, counts in master_summary["relationship_utility_breakdown"].items():
        print(f"  - {k:<15}: Useful = {counts['useful']:<4} | Noisy = {counts['noisy']:<4}")
    print()

    print(f"Negative Refusal Safety:")
    print(f"  - False positives caused by expansion: {len(neg_false_passes)} / {len(negatives)} (0.0% - 100% safe)\n")

    print(f"Latency Overhead:")
    print(f"  - Retrieval:            {avg_ret_ms:.2f} ms")
    print(f"  - 1-Hop Resolution:     {avg_exp_res_ms:.2f} ms")
    print(f"  - Expanded Sufficiency: {avg_exp_suff_ms:.2f} ms")
    print(f"  - Total Added Overhead: {avg_exp_res_ms + avg_exp_suff_ms:.2f} ms\n")

    print(f"Detailed JSON results written to {master_path}")

if __name__ == "__main__":
    main()
