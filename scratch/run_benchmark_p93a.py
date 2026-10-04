import os
import subprocess
import json
import time

BENCHMARK_DIR = r"d:\amoeba\benchmarks\repoprobe_v1"
REPOS_DIR = os.path.join(BENCHMARK_DIR, "repos")
EXE_PATH = r"d:\amoeba\build\amoeba_repoprobe_benchmark.exe"

with open(os.path.join(BENCHMARK_DIR, "questions.json"), "r", encoding="utf-8") as f:
    questions = json.load(f)
with open(os.path.join(BENCHMARK_DIR, "ground_truth.json"), "r", encoding="utf-8") as f:
    ground_truth = json.load(f)

gt_by_id = {g["question_id"]: g for g in ground_truth}

# Group by repo
questions_by_repo = {}
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
    questions_by_repo.setdefault(q["repo_name"], []).append(q_item)

configs = ["baseline_top10", "top50_no_investigation", "top50_bounded_investigation"]

for config_name in configs:
    target_results_dir = os.path.join(BENCHMARK_DIR, "results", config_name)
    os.makedirs(target_results_dir, exist_ok=True)
    all_results = []
    print(f"\n========================================================")
    print(f"RUNNING CONFIGURATION: {config_name}")
    print(f"========================================================")

    for repo_idx, (repo_name, q_list) in enumerate(questions_by_repo.items(), 1):
        slug = repo_name.split("/")[-1]
        repo_dir = os.path.join(REPOS_DIR, slug)
        temp_tsv = os.path.join(target_results_dir, f"temp_{slug}_queries.tsv")
        temp_out = os.path.join(target_results_dir, f"temp_{slug}_results.json")

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

        if os.path.exists(temp_out):
            with open(temp_out, "r", encoding="utf-8") as rf:
                repo_results = json.load(rf)
                all_results.extend(repo_results)
            os.remove(temp_tsv)
            os.remove(temp_out)
        print(f"[{repo_idx}/10] {repo_name} ({len(q_list)} queries) finished in {t_elapsed:.2f}s")

    # Write questions.json
    with open(os.path.join(target_results_dir, "questions.json"), "w", encoding="utf-8") as qf:
        json.dump(all_results, qf, indent=2)

    # Compute summary
    total = len(all_results)
    pos = [q for q in all_results if q["expected_files"] or q["expected_symbols"]]
    neg = [q for q in all_results if not q["expected_files"] and not q["expected_symbols"]]

    correct = sum(1 for q in all_results if q["correctness"] == "correct")
    grounded = sum(1 for q in all_results if q.get("grounded", False))
    neg_correct = sum(1 for q in neg if q["correctness"] == "correct")

    hit1 = sum(1 for q in pos if q["retrieval_hit_at_1"]) / len(pos) * 100
    hit5 = sum(1 for q in pos if q["retrieval_hit_at_5"]) / len(pos) * 100
    hit10 = sum(1 for q in pos if q["retrieval_hit_at_10"]) / len(pos) * 100
    hit20 = sum(1 for q in pos if q["retrieval_hit_at_20"]) / len(pos) * 100
    hit50 = sum(1 for q in pos if q["retrieval_hit_at_50"]) / len(pos) * 100

    hop0_hit1 = sum(1 for q in pos if q.get("hop0_hit_at_1", False)) / len(pos) * 100
    hop0_hit5 = sum(1 for q in pos if q.get("hop0_hit_at_5", False)) / len(pos) * 100
    hop0_hit10 = sum(1 for q in pos if q.get("hop0_hit_at_10", False)) / len(pos) * 100
    hop0_hit20 = sum(1 for q in pos if q.get("hop0_hit_at_20", False)) / len(pos) * 100
    hop0_hit50 = sum(1 for q in pos if q.get("hop0_hit_at_50", False)) / len(pos) * 100

    mrr = sum(1.0 / q["target_rank_in_hop0"] for q in pos if q.get("target_rank_in_hop0", 0) > 0) / len(pos)

    # Split by source
    repoprobe_qs = [q for q in all_results if not q["question_id"].startswith("amoeba-")]
    amoeba_qs = [q for q in all_results if q["question_id"].startswith("amoeba-")]

    rp_correct = sum(1 for q in repoprobe_qs if q["correctness"] == "correct")
    am_correct = sum(1 for q in amoeba_qs if q["correctness"] == "correct")

    # Failure counts
    suff_fails = sum(1 for q in pos if q.get("evidence_sufficiency") is False)
    ans_fails = sum(1 for q in pos if q.get("evidence_sufficiency") is True and q["correctness"] != "correct")

    print(f"\n--- Results for {config_name} ---")
    print(f"Total Questions: {total}")
    print(f"Overall Accuracy: {correct}/{total} ({correct/total*100:.2f}%)")
    print(f"RepoProbe Accuracy: {rp_correct}/{len(repoprobe_qs)} ({rp_correct/len(repoprobe_qs)*100:.2f}%)")
    print(f"Amoeba Accuracy: {am_correct}/{len(amoeba_qs)} ({am_correct/len(amoeba_qs)*100:.2f}%)")
    print(f"Negative Refusal: {neg_correct}/{len(neg)} ({neg_correct/len(neg)*100:.2f}%)")
    print(f"Sufficiency Failures (pos): {suff_fails}")
    print(f"Answer Failures (pos): {ans_fails}")
    print(f"Hit@1: {hit1:.2f}% | Hit@5: {hit5:.2f}% | Hit@10: {hit10:.2f}% | Hit@50: {hit50:.2f}%")
    print(f"MRR: {mrr:.4f}")

    failures = [q for q in all_results if q["correctness"] != "correct"]
    with open(os.path.join(target_results_dir, "failures.json"), "w", encoding="utf-8") as ff:
        json.dump(failures, ff, indent=2)

    by_repo = {}
    for q in all_results:
        r = q["repository"]
        by_repo.setdefault(r, {"total": 0, "correct": 0, "hit10": 0, "hit50": 0})
        by_repo[r]["total"] += 1
        if q["correctness"] == "correct":
            by_repo[r]["correct"] += 1
        if q.get("retrieval_hit_at_10", False):
            by_repo[r]["hit10"] += 1
        if q.get("retrieval_hit_at_50", False):
            by_repo[r]["hit50"] += 1

    repo_breakdown = {}
    for r, d in by_repo.items():
        repo_breakdown[r] = {
            "total": d["total"],
            "correct": d["correct"],
            "accuracy_pct": d["correct"] / d["total"] * 100,
            "hit10_pct": d["hit10"] / d["total"] * 100,
            "hit50_pct": d["hit50"] / d["total"] * 100,
        }

    fail_dist = {}
    for q in all_results:
        cat = q.get("failure_category", "none")
        fail_dist[cat] = fail_dist.get(cat, 0) + 1

    summary = {
        "configuration": config_name,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "total_questions": total,
        "positive_questions": len(pos),
        "negative_questions": len(neg),
        "metrics": {
            "correctness_rate_pct": correct / total * 100,
            "groundedness_rate_pct": grounded / total * 100,
            "negative_refusal_accuracy_pct": neg_correct / len(neg) * 100 if neg else 0,
            "retrieval_hit_at_1_pct": hit1,
            "retrieval_hit_at_5_pct": hit5,
            "retrieval_hit_at_10_pct": hit10,
            "retrieval_hit_at_20_pct": hit20,
            "retrieval_hit_at_50_pct": hit50,
            "hop0_hit_at_1_pct": hop0_hit1,
            "hop0_hit_at_5_pct": hop0_hit5,
            "hop0_hit_at_10_pct": hop0_hit10,
            "hop0_hit_at_20_pct": hop0_hit20,
            "hop0_hit_at_50_pct": hop0_hit50,
            "mrr": mrr,
        },
        "failure_distribution": fail_dist,
        "breakdown_by_repository": repo_breakdown,
    }
    with open(os.path.join(target_results_dir, "summary.json"), "w", encoding="utf-8") as sf:
        json.dump(summary, sf, indent=2)
