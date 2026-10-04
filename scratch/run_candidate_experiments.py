import os
import subprocess
import json
import glob
import time

BENCHMARK_DIR = r"d:\amoeba\benchmarks\repoprobe_v1"
REPOS_DIR = os.path.join(BENCHMARK_DIR, "repos")
OUTPUT_DIR = r"d:\amoeba\scratch\experiments_results"
BIN_PATH = r"d:\amoeba\build\amoeba_candidate_selection_experiments.exe"

os.makedirs(OUTPUT_DIR, exist_ok=True)

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

policies = ["policy_a", "policy_b", "policy_c", "policy_d"]

print("==========================================")
print("   RUNNING CANDIDATE SELECTION EXPERIMENTS")
print("==========================================")

for repo_idx, (repo_name, q_list) in enumerate(by_repo.items(), 1):
    slug = repo_name.split("/")[-1]
    repo_dir = os.path.join(REPOS_DIR, slug)
    temp_tsv = os.path.join(OUTPUT_DIR, f"temp_{slug}_queries.tsv")
    
    # Write TSV query payload
    with open(temp_tsv, "w", encoding="utf-8") as tf:
        for q in q_list:
            files_str = "|".join(q["expected_files"])
            syms_str = "|".join(q["expected_symbols"])
            rels_str = "|".join(q["expected_relationships"])
            concepts_str = "|".join(q["expected_concepts"])
            row = f"{q['question_id']}\t{q['repo_name']}\t{q['source']}\t{q['taxonomy']}\t{q['difficulty']}\t{q['question']}\t{files_str}\t{syms_str}\t{rels_str}\t{concepts_str}\n"
            tf.write(row)
            
    print(f"\n[{repo_idx}/10] Processing {repo_name} ({len(q_list)} queries)...")
    cmd = [BIN_PATH, repo_dir, temp_tsv, OUTPUT_DIR]
    t0 = time.time()
    res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    t_elapsed = time.time() - t0
    if res.returncode != 0:
        print(f"ERROR on {repo_name}: {res.stderr}")
    else:
        print(f"  Completed in {t_elapsed:.2f}s: {res.stdout.strip()}")

# Combine results per policy
for policy in policies:
    policy_dir = os.path.join(OUTPUT_DIR, policy)
    all_questions = []
    for repo_name in by_repo.keys():
        slug = repo_name.split("/")[-1]
        json_file = os.path.join(policy_dir, f"{slug}.json")
        if os.path.exists(json_file):
            with open(json_file, "r", encoding="utf-8") as f:
                all_questions.extend(json.load(f))
    combined_path = os.path.join(policy_dir, "questions.json")
    with open(combined_path, "w", encoding="utf-8") as f:
        json.dump(all_questions, f, indent=2)
    print(f"Policy {policy}: Combined {len(all_questions)} questions into {combined_path}")

print("\nALL CANDIDATE SELECTION EXPERIMENTS COMPLETE!")


