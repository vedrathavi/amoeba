import json
import os

QUESTIONS_PATH = "d:/amoeba/benchmarks/repoprobe_v1/results/top50_bounded_investigation/questions.json"
GT_PATH = "d:/amoeba/benchmarks/repoprobe_v1/ground_truth.json"

with open(QUESTIONS_PATH, "r", encoding="utf-8") as f:
    results = json.load(f)

with open(GT_PATH, "r", encoding="utf-8") as f:
    gt_list = json.load(f)

gt_map = {g["question_id"]: g for g in gt_list}
res_map = {r["question_id"]: r for r in results}

# Filter positive questions
pos_qs = [r for r in results if r.get("expected_files") or r.get("expected_symbols")]

print(f"Total positive questions: {len(pos_qs)}")

# Classify failures for each question
# Categories:
# A. TARGET ABSENT FROM HOP-0 TOP-50 (Retrieval recall failure)
# B. TARGET PRESENT IN TOP-50 BUT LOST BEFORE SEED SELECTION / CANDIDATE SELECTION (Candidate selection failure)
# C. TARGET SELECTED AS SEED BUT INVESTIGATION DOES NOT REACH IT (Investigation failure)
# D. TARGET REACHED BUT NOT INCLUDED IN FINAL EVIDENCE (Evidence assembly / budget failure)
# E. TARGET PRESENT IN EVIDENCE BUT SUFFICIENCY REJECTS (Evidence failure)
# F. TARGET SUPPORTED BUT ANSWER INCORRECT (Reasoning / answer failure)
# S. SUCCESS (Correct)

failure_matrix = []

for q in pos_qs:
    qid = q["question_id"]
    repo = q["repository"]
    exp_f = q["expected_files"]
    exp_s = q["expected_symbols"]
    
    corr = q.get("correctness")
    suff = q.get("sufficiency_decision")
    
    rank_hop0 = q.get("target_rank_in_hop0", 0)
    in_top50 = q.get("hop0_hit_at_50", False)
    in_top20 = q.get("hop0_hit_at_20", False)
    in_top10 = q.get("hop0_hit_at_10", False)
    in_top5 = q.get("hop0_hit_at_5", False)
    in_top1 = q.get("hop0_hit_at_1", False)
    
    ret_hit10 = q.get("retrieval_hit_at_10", False)
    ret_hit50 = q.get("retrieval_hit_at_50", False)
    
    target_as_seed = q.get("target_selected_as_seed", False)
    hop1_found = q.get("hop1_discovered_target", False)
    
    # Check if target is present in final evidence items
    ev_count = q.get("evidence_items_count", 0)
    ret_syms = q.get("retrieved_symbols", [])
    ret_files = q.get("retrieved_files", [])
    
    # Failure classification
    if corr == "correct":
        f_class = "S_SUCCESS"
    else:
        if not in_top50 and not hop1_found:
            f_class = "A_RETRIEVAL_ABSENT_TOP50"
        elif not ret_hit10:
            # Target was in Top-50 or Hop-1, but not in final Top-10 candidate pool
            f_class = "B_CANDIDATE_SELECTION_LOST"
        elif not suff:
            # Target was in final Top-10 candidate pool, but sufficiency gate rejected
            f_class = "E_SUFFICIENCY_REJECTED"
        else:
            # Sufficiency passed but answer was incorrect
            f_class = "F_ANSWER_INCORRECT"
            
    failure_matrix.append({
        "question_id": qid,
        "repo": repo,
        "f_class": f_class,
        "correctness": corr,
        "sufficiency": suff,
        "target_rank_hop0": rank_hop0,
        "in_top50": in_top50,
        "in_top10": in_top10,
        "ret_hit10": ret_hit10,
        "target_as_seed": target_as_seed,
        "hop1_found": hop1_found,
        "retrieved_symbols": ret_syms[:4],
        "expected_symbols": exp_s[:2],
        "expected_files": exp_f[:2],
        "sufficiency_reason": q.get("sufficiency_reason"),
    })

# Summary counts
class_counts = {}
for item in failure_matrix:
    c = item["f_class"]
    class_counts[c] = class_counts.get(c, 0) + 1

print("\n=== FAILURE CLASSIFICATION SUMMARY (66 Positive Questions) ===")
for c, count in sorted(class_counts.items()):
    print(f"  {c:<30}: {count} ({count/len(pos_qs)*100:.2f}%)")

print("\n=== DETAILED LIST BY FAILURE CLASS ===")
for c in sorted(class_counts.keys()):
    items = [it for it in failure_matrix if it["f_class"] == c]
    print(f"\n--- {c} ({len(items)} questions) ---")
    for it in items:
        print(f"  {it['question_id']:<25} ({it['repo']:<20}) | Hop0Rank={it['target_rank_hop0']} | Top10Ret={it['ret_hit10']} | Suff={it['sufficiency']}")
        if it['f_class'] != "S_SUCCESS":
            print(f"    Expected:  syms={it['expected_symbols']} files={it['expected_files']}")
            print(f"    Retrieved: syms={it['retrieved_symbols']}")
            print(f"    Reason:    {it['sufficiency_reason']}")
