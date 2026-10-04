import json
import os
import glob

EXP_DIR = r"d:\amoeba\scratch\experiments_results"
policies = ["policy_a", "policy_b", "policy_c", "policy_d"]

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

# Ensure questions.json is combined for each policy
for p in policies:
    policy_dir = os.path.join(EXP_DIR, p)
    repo_files = [f for f in glob.glob(os.path.join(policy_dir, "*.json")) if not f.endswith("questions.json")]
    all_q = []
    for rf in repo_files:
        if os.path.getsize(rf) > 10:
            try:
                with open(rf, "r", encoding="utf-8") as f:
                    all_q.extend(json.load(f))
            except Exception as e:
                print(f"Warning on {rf}: {e}")
    with open(os.path.join(policy_dir, "questions.json"), "w", encoding="utf-8") as f:
        json.dump(all_q, f, indent=2)

print("==========================================================================================")
print("                       PHASE 9.3A EXPERIMENT RESULTS SUMMARY")
print("==========================================================================================")

for p in policies:
    json_path = os.path.join(EXP_DIR, p, "questions.json")
    with open(json_path, "r", encoding="utf-8") as f:
        data = json.load(f)
    
    total = len(data)
    if total == 0:
        print(f"\n--- Policy: {p} (No data) ---")
        continue
        
    pos_data = [q for q in data if q["question_id"] not in negative_qids]
    neg_data = [q for q in data if q["question_id"] in negative_qids]
    
    # Correctness
    correct_count = sum(1 for q in data if q.get("correctness") == "correct")
    pos_correct = sum(1 for q in pos_data if q.get("correctness") == "correct")
    neg_correct = sum(1 for q in neg_data if q.get("correctness") == "correct")
    
    # Sufficiency failures
    suff_failures = sum(1 for q in pos_data if not q.get("sufficiency_decision", False))
    
    # RepoProbe vs Amoeba
    repoprobe_items = [q for q in data if not q.get("question_id", "").startswith("amoeba-")]
    amoeba_items = [q for q in data if q.get("question_id", "").startswith("amoeba-")]
    repoprobe_correct = sum(1 for q in repoprobe_items if q.get("correctness") == "correct")
    amoeba_correct = sum(1 for q in amoeba_items if q.get("correctness") == "correct")
    
    # Hits
    hit1 = sum(1 for q in pos_data if q.get("retrieval_hit_at_1", False))
    hit5 = sum(1 for q in pos_data if q.get("retrieval_hit_at_5", False))
    hit10 = sum(1 for q in pos_data if q.get("retrieval_hit_at_10", False))
    
    # Target 12 status
    target_results = {q["question_id"]: q for q in data if q["question_id"] in target_12_qids}
    target_resolved = sum(1 for qid in target_12_qids if target_results.get(qid, {}).get("correctness") == "correct")
    
    print(f"\n--- Policy: {p} ---")
    print(f"  Overall Accuracy:     {correct_count}/{total} ({correct_count/total*100:.2f}%)")
    print(f"  Positive Accuracy:    {pos_correct}/{len(pos_data)} ({pos_correct/len(pos_data)*100:.2f}%)")
    print(f"  Negative Refusal:     {neg_correct}/{len(neg_data)} ({neg_correct/len(neg_data)*100:.2f}%)")
    print(f"  RepoProbe Accuracy:   {repoprobe_correct}/{len(repoprobe_items)} ({repoprobe_correct/len(repoprobe_items)*100:.2f}%)")
    print(f"  Amoeba Accuracy:      {amoeba_correct}/{len(amoeba_items)} ({amoeba_correct/len(amoeba_items)*100:.2f}%)")
    print(f"  Sufficiency Failures: {suff_failures}/{len(pos_data)} (Sufficiency Pass Rate: {(len(pos_data)-suff_failures)/len(pos_data)*100:.2f}%)")
    print(f"  Hit@1:  {hit1}/{len(pos_data)} ({hit1/len(pos_data)*100:.2f}%)")
    print(f"  Hit@5:  {hit5}/{len(pos_data)} ({hit5/len(pos_data)*100:.2f}%)")
    print(f"  Hit@10: {hit10}/{len(pos_data)} ({hit10/len(pos_data)*100:.2f}%)")
    print(f"  Target 12 Resolved:   {target_resolved}/12 ({target_resolved/12*100:.2f}%)")
    
    print("  Detailed 12 Target Breakdown:")
    for qid in target_12_qids:
        item = target_results.get(qid, {})
        status = item.get("correctness", "missing")
        suff = item.get("sufficiency_decision", False)
        fail_cat = item.get("failure_category", "none")
        print(f"    - {qid:20s}: status={status:7s}, suff={str(suff):5s}, fail_cat={fail_cat}")
