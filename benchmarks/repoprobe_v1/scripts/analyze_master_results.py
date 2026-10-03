import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

def main():
    path = Path("benchmarks/repoprobe_v1/results/investigation/master_investigation_results.json")
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)

    with open("benchmarks/repoprobe_v1/questions.json", "r", encoding="utf-8") as f:
        q_map = {q["question_id"]: q for q in json.load(f)}

    with open("benchmarks/repoprobe_v1/ground_truth.json", "r", encoding="utf-8") as f:
        gt_map = {g["question_id"]: g for g in json.load(f)}

    print("=== SUMMARY METRICS ===")
    print(f"Total Questions: {data['total_questions']}")
    print(f"Baseline Correct: {data['baseline_correct_total']} / {data['total_questions']} ({data['baseline_accuracy_pct']:.1f}%)")
    print(f"Positive Baseline Correct: {data['positive_correct_baseline']} / {data['positive_questions']} ({data['positive_accuracy_pct']:.1f}%)")
    print(f"Negative Refusal: {data['negative_safety']['correct_refusals']} / {data['negative_questions']} ({data['negative_refusal_accuracy_pct']:.1f}%)")
    
    print("\n=== FAILURE BREAKDOWN ===")
    print(f"Category A (Retrieval Failure): {data['failure_categories']['cat_a_retrieval_failure']} ({data['failure_categories']['cat_a_pct']:.1f}%)")
    print(f"Category B (Evidence Failure): {data['failure_categories']['cat_b_evidence_failure']} ({data['failure_categories']['cat_b_pct']:.1f}%)")
    print(f"Category C (Reasoning Failure): {data['failure_categories']['cat_c_reasoning_failure']} ({data['failure_categories']['cat_c_pct']:.1f}%)")
    
    print("\n=== EXPANSION IMPACT ON CATEGORY B ===")
    impact = data['category_b_expansion_impact']
    print(f"Category B Total: {impact['cat_b_total']}")
    print(f"Solved by 1-Hop: {impact['cat_b_solved_by_1hop']} ({impact['cat_b_resolution_rate_pct']:.1f}%)")
    print(f"Potential Positive Accuracy Post-Expansion: {impact['potential_positive_accuracy_post_expansion_pct']:.1f}% (17 / 66)")
    print(f"Potential Overall Accuracy Post-Expansion: {impact['potential_overall_accuracy_post_expansion_pct']:.1f}% (21 / 70)")

    print("\n=== MINIMUM UNITS DISTRIBUTION ===")
    for k, v in data['minimum_expansion_units_distribution'].items():
        print(f"  {k}: {v}")

    print("\n=== RELATIONSHIP UTILITY ===")
    for k, v in data['relationship_utility_breakdown'].items():
        total = v['useful'] + v['noisy']
        ratio = (v['useful'] / total * 100) if total > 0 else 0
        print(f"  {k:<15}: Useful={v['useful']:<4} | Noisy={v['noisy']:<5} | Precision={ratio:.1f}%")

    print("\n=== LATENCY BREAKDOWN ===")
    for k, v in data['latency_breakdown_ms'].items():
        print(f"  {k}: {v:.2f} ms")

    # Detailed Category B analysis
    cat_b_all = [q for q in data['questions'] if q['failure_category'] == 'evidence_failure']
    print(f"\n=======================================================")
    print(f"CATEGORY B QUESTIONS FORENSIC BREAKDOWN ({len(cat_b_all)})")
    print(f"=======================================================")
    
    for i, q in enumerate(cat_b_all, 1):
        qid = q["question_id"]
        q_raw = q_map[qid]
        gt = gt_map[qid]
        solved = q["expansion_solves_sufficiency"]
        status_str = "RESOLVED (+ " + str(q['min_units_required']) + " unit)" if solved else "UNRESOLVED (>1-hop or multi-hop required)"
        print(f"\n{i}. [{qid}] ({q['repository']}) — {status_str}")
        print(f"   Question: {q_raw['question']}")
        print(f"   Expected Files: {gt.get('expected_files', [])}")
        print(f"   Expected Symbols: {gt.get('expected_symbols', [])}")
        print(f"   Expected Relationships: {gt.get('expected_relationships', [])}")
        print(f"   Expected Concepts: {gt.get('expected_concepts', [])}")
        print(f"   Base Sufficiency Reason: {q['base_sufficiency_reason']}")
        print(f"   1-Hop Neighbors: Total={q['total_1hop_neighbors']}, Useful={q['useful_neighbors']}, Noisy={q['noisy_neighbors']}")
        if q.get('useful_descriptions'):
            print(f"   Useful Links: {q['useful_descriptions']}")
        if solved:
            print(f"   Added Chars: {q['added_source_chars']} | Added Elements: {q.get('added_elements', 0)}")

    print(f"\n=======================================================")
    print(f"NEGATIVE REFUSAL SAFETY QUESTIONS (4)")
    print(f"=======================================================")
    for q in data['questions']:
        if q['is_negative']:
            qid = q["question_id"]
            q_raw = q_map[qid]
            print(f"\n[{qid}] ({q['repository']})")
            print(f"   Question: {q_raw['question']}")
            print(f"   Hit@10: {q['hit_at_10']}, Base Sufficiency: {q['base_sufficiency']}")
            print(f"   Sufficiency Reason: {q['base_sufficiency_reason']}")
            print(f"   1-Hop Neighbors: Total={q['total_1hop_neighbors']}, Useful={q['useful_neighbors']}, Noisy={q['noisy_neighbors']}")
            print(f"   Expansion Solves: {q['expansion_solves_sufficiency']}")

if __name__ == "__main__":
    main()
