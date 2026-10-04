import subprocess
import os
import json
import time

BENCHMARK_DIR = r"d:\amoeba\benchmarks\repoprobe_v1"
REPOS_DIR = os.path.join(BENCHMARK_DIR, "repos")
OUTPUT_DIR = r"d:\amoeba\scratch\experiments_results"
BIN_PATH = r"d:\amoeba\build\amoeba_candidate_selection_experiments.exe"

questions = json.load(open(os.path.join(BENCHMARK_DIR, "questions.json"), encoding="utf-8"))
gt_by_id = {g["question_id"]: g for g in json.load(open(os.path.join(BENCHMARK_DIR, "ground_truth.json"), encoding="utf-8"))}
by_repo = {}
for q in questions:
    by_repo.setdefault(q["repo_name"], []).append(q)

for repo_name in ["BurntSushi/jiff", "abenz1267/walker", "docling-project/docling"]:
    slug = repo_name.split("/")[-1]
    repo_dir = os.path.join(REPOS_DIR, slug)
    temp_tsv = os.path.join(OUTPUT_DIR, f"temp_{slug}_queries.tsv")
    with open(temp_tsv, "w", encoding="utf-8") as tf:
        for q in by_repo[repo_name]:
            gt = gt_by_id.get(q["question_id"], {})
            files_str = "|".join(gt.get("expected_files", []))
            syms_str = "|".join(gt.get("expected_symbols", []))
            rels_str = "|".join(gt.get("expected_relationships", []))
            concepts_str = "|".join(gt.get("expected_concepts", []))
            qtext = q["question"].replace("\r", "").replace("\n", " ").strip()
            tf.write(f"{q['question_id']}\t{repo_name}\t{q.get('source', 'repoprobe')}\t{q.get('taxonomy', 'Implementation Details')}\t{q.get('difficulty', 'Medium')}\t{qtext}\t{files_str}\t{syms_str}\t{rels_str}\t{concepts_str}\n")
    print(f"Wrote TSV for {slug} with {len(by_repo[repo_name])} queries")
    t0 = time.time()
    res = subprocess.run([BIN_PATH, repo_dir, temp_tsv, OUTPUT_DIR], capture_output=True, text=True)
    t_elapsed = time.time() - t0
    print(f"{slug} completed in {t_elapsed:.2f}s:")
    print("STDOUT:", res.stdout.strip())
    if res.stderr:
        print("STDERR:", res.stderr.strip())

# Combine all 70 questions for all policies
policies = ["policy_a", "policy_b", "policy_c", "policy_d"]
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

print("REMAINING 3 REPOS COMPLETE!")
