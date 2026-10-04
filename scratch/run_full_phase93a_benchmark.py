import os
import subprocess
import json
import time

BENCHMARK_DIR = r"d:\amoeba\benchmarks\repoprobe_v1"
REPOS_DIR = os.path.join(BENCHMARK_DIR, "repos")
OUTPUT_DIR = r"d:\amoeba\scratch\phase93a_results"
BIN_PATH = r"d:\amoeba\build\amoeba_repoprobe_benchmark.exe"

os.makedirs(OUTPUT_DIR, exist_ok=True)

questions = json.load(open(os.path.join(BENCHMARK_DIR, "questions.json"), encoding="utf-8"))
gt_by_id = {g["question_id"]: g for g in json.load(open(os.path.join(BENCHMARK_DIR, "ground_truth.json"), encoding="utf-8"))}
by_repo = {}
for q in questions:
    by_repo.setdefault(q["repo_name"], []).append(q)

target_12_qids = [
    "amoeba-ducklake-01",
    "adk-python-5",
    "better-auth-0",
    "better-auth-4",
    "pkl-0",
    "opencloud-0",
    "jiff-3",
    "jiff-4",
    "walker-0",
    "walker-3",
    "walker-4",
    "docling-0"
]

negative_qids = [
    "adk-python-2",
    "dokploy-2",
    "pkl-2",
    "beszel-5"
]

all_results = []

print("=================================================================")
print("   RUNNING REPOPROBE BENCHMARK (top50_bounded_investigation)")
print("=================================================================")

for repo_idx, (repo_name, q_list) in enumerate(by_repo.items(), 1):
    slug = repo_name.split("/")[-1]
    repo_dir = os.path.join(REPOS_DIR, slug)
    temp_tsv = os.path.join(OUTPUT_DIR, f"temp_{slug}_queries.tsv")
    out_json = os.path.join(OUTPUT_DIR, f"{slug}.json")
    
    with open(temp_tsv, "w", encoding="utf-8") as tf:
        for q in q_list:
            gt = gt_by_id.get(q["question_id"], {})
            files_str = "|".join(gt.get("expected_files", []))
            syms_str = "|".join(gt.get("expected_symbols", []))
            rels_str = "|".join(gt.get("expected_relationships", []))
            concepts_str = "|".join(gt.get("expected_concepts", []))
            qtext = q["question"].replace("\r", "").replace("\n", " ").strip()
            tf.write(f"{q['question_id']}\t{repo_name}\t{q.get('source', 'repoprobe')}\t{q.get('taxonomy', 'Implementation Details')}\t{q.get('difficulty', 'Medium')}\t{qtext}\t{files_str}\t{syms_str}\t{rels_str}\t{concepts_str}\n")
            
    print(f"\n[{repo_idx}/10] Processing {repo_name} ({len(q_list)} queries)...")
    t0 = time.time()
    res = subprocess.run([BIN_PATH, repo_dir, temp_tsv, out_json, "top50_bounded_investigation"], capture_output=True, text=True, encoding="utf-8", errors="replace")
    t_elapsed = time.time() - t0
    
    if os.path.exists(out_json):
        with open(out_json, "r", encoding="utf-8") as f:
            repo_q = json.load(f)
            all_results.extend(repo_q)
        print(f"  Completed in {t_elapsed:.2f}s: {len(repo_q)} questions evaluated")
    else:
        print(f"  ERROR on {repo_name}: {res.stderr}")

combined_path = os.path.join(OUTPUT_DIR, "questions.json")
with open(combined_path, "w", encoding="utf-8") as f:
    json.dump(all_results, f, indent=2)

print(f"\nSaved all {len(all_results)} questions to {combined_path}")

# Metrics evaluation
total = len(all_results)
pos_data = [q for q in all_results if q["question_id"] not in negative_qids]
neg_data = [q for q in all_results if q["question_id"] in negative_qids]

correct_count = sum(1 for q in all_results if q.get("correctness") == "correct")
pos_correct = sum(1 for q in pos_data if q.get("correctness") == "correct")
neg_correct = sum(1 for q in neg_data if q.get("correctness") == "correct")

suff_failures = sum(1 for q in pos_data if not q.get("sufficiency_decision", False))

repoprobe_items = [q for q in all_results if not q.get("question_id", "").startswith("amoeba-")]
amoeba_items = [q for q in all_results if q.get("question_id", "").startswith("amoeba-")]
repoprobe_correct = sum(1 for q in repoprobe_items if q.get("correctness") == "correct")
amoeba_correct = sum(1 for q in amoeba_items if q.get("correctness") == "correct")

hit1 = sum(1 for q in pos_data if q.get("retrieval_hit_at_1", False))
hit5 = sum(1 for q in pos_data if q.get("retrieval_hit_at_5", False))
hit10 = sum(1 for q in pos_data if q.get("retrieval_hit_at_10", False))

target_results = {q["question_id"]: q for q in all_results if q["question_id"] in target_12_qids}
target_resolved = sum(1 for qid in target_12_qids if target_results.get(qid, {}).get("correctness") == "correct")

print("\n=================================================================")
print("                    BENCHMARK METRICS RESULTS")
print("=================================================================")
print(f"Overall Accuracy:     {correct_count}/{total} ({correct_count/total*100:.2f}%)")
print(f"Positive Accuracy:    {pos_correct}/{len(pos_data)} ({pos_correct/len(pos_data)*100:.2f}%)")
print(f"Negative Refusal:     {neg_correct}/{len(neg_data)} ({neg_correct/len(neg_data)*100:.2f}%)")
print(f"RepoProbe Accuracy:   {repoprobe_correct}/{len(repoprobe_items)} ({repoprobe_correct/len(repoprobe_items)*100:.2f}%)")
print(f"Amoeba Accuracy:      {amoeba_correct}/{len(amoeba_items)} ({amoeba_correct/len(amoeba_items)*100:.2f}%)")
print(f"Sufficiency Failures: {suff_failures}/{len(pos_data)}")
print(f"Hit@1:                {hit1}/{len(pos_data)} ({hit1/len(pos_data)*100:.2f}%)")
print(f"Hit@5:                {hit5}/{len(pos_data)} ({hit5/len(pos_data)*100:.2f}%)")
print(f"Hit@10:               {hit10}/{len(pos_data)} ({hit10/len(pos_data)*100:.2f}%)")
print(f"Target 12 Resolved:   {target_resolved}/12 ({target_resolved/12*100:.2f}%)")

print("\nDetailed Target 12 Breakdown:")
for qid in target_12_qids:
    item = target_results.get(qid, {})
    status = item.get("correctness", "missing")
    suff = item.get("sufficiency_decision", False)
    fail_cat = item.get("failure_category", "none")
    print(f"  - {qid:20s}: status={status:7s}, suff={str(suff):5s}, fail_cat={fail_cat}")

