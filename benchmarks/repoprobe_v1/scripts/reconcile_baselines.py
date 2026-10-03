#!/usr/bin/env python3
"""
Reconciliation script between:
A. Previous validated benchmark (results/latest/questions.json - 17/70 = 24.3%)
B. Phase 8.2.9 Depth-0 result (results/expansion/master_expansion_results.json - 22/70 = 31.4%)
"""

import json
import sys

sys.stdout.reconfigure(encoding="utf-8")

def main():
    latest_q = json.load(open(r"d:\amoeba\benchmarks\repoprobe_v1\results\latest\questions.json", encoding="utf-8"))
    exp_res = json.load(open(r"d:\amoeba\benchmarks\repoprobe_v1\results\expansion\master_expansion_results.json", encoding="utf-8"))
    q_map = {x["question_id"]: x for x in json.load(open(r"d:\amoeba\benchmarks\repoprobe_v1\questions.json", encoding="utf-8"))}
    gt_map = {x["question_id"]: x for x in json.load(open(r"d:\amoeba\benchmarks\repoprobe_v1\ground_truth.json", encoding="utf-8"))}

    latest_by_id = {q["question_id"]: q for q in latest_q}
    exp_by_id = {q["question_id"]: q for q in exp_res}

    print(f"Total Old Questions: {len(latest_by_id)}")
    print(f"Total New Questions: {len(exp_by_id)}")

    diffs = []
    for qid, old in latest_by_id.items():
        new = exp_by_id.get(qid)
        if not new:
            continue

        old_correct = (old["correctness"] == "correct")
        new_correct = new["depth_0"]["is_correct"]

        old_suff = old.get("sufficiency_decision", False)
        new_suff = new["depth_0"]["sufficiency_passed"]

        old_hit10 = old.get("retrieval_hit_at_10", False)
        new_hit10 = new["hit_at_10"]

        if old_correct != new_correct or old_suff != new_suff or old_hit10 != new_hit10:
            diffs.append({
                "question_id": qid,
                "repo": old["repository"],
                "taxonomy": old.get("taxonomy"),
                "is_negative": old.get("is_negative", False),
                "old_correct": old_correct,
                "new_correct": new_correct,
                "old_hit10": old_hit10,
                "new_hit10": new_hit10,
                "old_suff": old_suff,
                "new_suff": new_suff,
                "old_fail_cat": old.get("failure_category"),
                "old_suff_reason": old.get("sufficiency_reason"),
                "new_suff_reason": new["depth_0"].get("sufficiency_reason"),
                "old_retrieved_files": old.get("retrieved_files", [])[:3],
                "old_retrieved_symbols": old.get("retrieved_symbols", [])[:3],
                "question_text": q_map.get(qid, {}).get("question", "")[:120],
                "expected_files": gt_map.get(qid, {}).get("expected_files", []),
                "expected_symbols": gt_map.get(qid, {}).get("expected_symbols", []),
            })

    print(f"\n=======================================================")
    print(f"DIFFERING QUESTIONS COUNT: {len(diffs)}")
    print(f"=======================================================\n")

    for i, d in enumerate(diffs, 1):
        print(f"{i}. [{d['question_id']}] ({d['repo']}) — Taxonomy: {d['taxonomy']}")
        print(f"   Question: {d['question_text']}")
        print(f"   Expected: Files={d['expected_files']} | Syms={d['expected_symbols']}")
        print(f"   Correctness: Old={d['old_correct']} -> New={d['new_correct']}")
        print(f"   Hit@10:      Old={d['old_hit10']} -> New={d['new_hit10']}")
        print(f"   Sufficiency: Old={d['old_suff']} -> New={d['new_suff']}")
        print(f"   Old Failure: {d['old_fail_cat']} | Reason: {d['old_suff_reason']}")
        print(f"   New Reason:  {d['new_suff_reason']}")
        print()

if __name__ == "__main__":
    main()
