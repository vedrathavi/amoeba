import json
import os
from pathlib import Path

def analyze():
    base_dir = Path("benchmarks/repoprobe_v1")
    with open(base_dir / "results/latest/questions.json", encoding="utf-8") as f:
        results = json.load(f)

    with open(base_dir / "ground_truth.json", encoding="utf-8") as f:
        ground_truth = {gt["question_id"]: gt for gt in json.load(f)}

    with open(base_dir / "questions.json", encoding="utf-8") as f:
        questions_raw = {q["question_id"]: q for q in json.load(f)}

    total = len(results)
    positives = []
    negatives = []

    for r in results:
        qid = r["question_id"]
        gt = ground_truth[qid]
        q_raw = questions_raw[qid]
        r["full_question_text"] = q_raw["question"]
        r["reference_answer"] = gt.get("reference_answer", "")
        r["expected_relationships"] = gt.get("expected_relationships", [])
        r["expected_concepts"] = gt.get("expected_concepts", [])
        
        is_neg = not r["expected_files"] and not r["expected_symbols"]
        if is_neg:
            negatives.append(r)
        else:
            positives.append(r)

    print(f"Total: {total} (Positive: {len(positives)}, Negative: {len(negatives)})")

    # Positive classification:
    # A. RETRIEVAL FAILURE (Hit@10 == False)
    # B. EVIDENCE FAILURE (Hit@10 == True, but failed due to sufficiency/missing evidence or relationship context)
    # C. REASONING FAILURE (Hit@10 == True, Sufficiency == True, but reasoning failed)
    # D. NON-CODE / EXTERNAL / REPO-WIDE

    cat_a = []
    cat_b = []
    cat_c = []
    cat_d = []
    correct_pos = []

    for r in positives:
        qid = r["question_id"]
        hit10 = r["retrieval_hit_at_10"]
        suff = r["sufficiency_decision"]
        correct = r["correctness"] == "correct"

        if correct:
            correct_pos.append(r)
            continue

        if not hit10:
            cat_a.append(r)
        else:
            # Hit@10 is True, but not correct.
            # Let's inspect whether this is Evidence Failure (B), Reasoning Failure (C), or Non-code (D)
            # Check sufficiency and expected concepts/relationships
            if not suff:
                cat_b.append(r)
            else:
                # If sufficiency passed, why was it incorrect?
                # In current benchmark runner: if Hit@10 is True, Sufficiency is True, groundedness is True, it marked correctness="correct".
                # If correctness is incorrect despite Hit@10 and Sufficiency, let's see why:
                cat_c.append(r)

    print("\n--- POSITIVE QUESTIONS BREAKDOWN ---")
    print(f"Correct: {len(correct_pos)} ({len(correct_pos)/len(positives)*100:.1f}%)")
    print(f"Category A (Retrieval Failure - Target NOT in Top-10): {len(cat_a)} ({len(cat_a)/len(positives)*100:.1f}%)")
    print(f"Category B (Evidence Failure - Target in Top-10, Sufficiency Failed): {len(cat_b)} ({len(cat_b)/len(positives)*100:.1f}%)")
    print(f"Category C (Reasoning Failure - Target in Top-10 & Suff Passed, but reasoning failed): {len(cat_c)} ({len(cat_c)/len(positives)*100:.1f}%)")
    print(f"Category D (Non-Code / External / Repo-wide): {len(cat_d)}")

    print(f"\n=======================================================")
    print(f"ALL {len(cat_b)} CATEGORY B (EVIDENCE FAILURE) QUESTIONS")
    print(f"=======================================================")
    for i, r in enumerate(cat_b, 1):
        print(f"\n{i}. [{r['question_id']}] Repo: {r['repository']} | Source: {r['source']}")
        print(f"   Taxonomy: {r['taxonomy']} | Diff: {r['difficulty']}")
        print(f"   Question: {r['full_question_text'][:140]}")
        print(f"   Expected Files: {r['expected_files']}")
        print(f"   Expected Symbols: {r['expected_symbols']}")
        print(f"   Expected Relationships: {r['expected_relationships']}")
        print(f"   Expected Concepts: {r['expected_concepts']}")
        print(f"   Retrieved Files:")
        for rf in r['retrieved_files'][:5]:
            print(f"     - {rf}")
        print(f"   Retrieved Symbols: {r['retrieved_symbols'][:5]}")
        print(f"   Sufficiency: Decision={r['sufficiency_decision']} | Reason={r['sufficiency_reason']}")

    print(f"\n=======================================================")
    print(f"ALL {len(cat_a)} CATEGORY A (RETRIEVAL FAILURE) QUESTIONS")
    print(f"=======================================================")
    for i, r in enumerate(cat_a, 1):
        print(f"{i}. [{r['question_id']}] ({r['repository']}): ExpSyms={r['expected_symbols']} ExpFiles={r['expected_files']}")

    print("\n--- NEGATIVE QUESTIONS ---")
    for r in negatives:
        print(f"[{r['question_id']}] Correct: {r['correctness']}, Sufficiency: {r['sufficiency_decision']}, Status: {r['final_answer_status']}")

if __name__ == "__main__":
    analyze()
