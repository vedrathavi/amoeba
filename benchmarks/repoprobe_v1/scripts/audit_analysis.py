import json
import os

with open(r"d:\amoeba\benchmarks\repoprobe_v1\results\latest\questions.json", "r", encoding="utf-8") as f:
    questions = json.load(f)

grounded_count = sum(1 for q in questions if q.get("grounded"))
correct_count = sum(1 for q in questions if q.get("correctness") == "correct")

print(f"Total Questions: {len(questions)}")
print(f"Grounded:        {grounded_count}")
print(f"Correct:         {correct_count}")
print()

print("--- Questions Grounded but marked Incorrect ---")
for q in questions:
    if q.get("grounded") and q.get("correctness") != "correct":
        print(f"ID: {q['question_id']}")
        print(f"  Repo:            {q['repository']}")
        print(f"  Hit@10:          {q.get('retrieval_hit_at_10')}")
        print(f"  Sufficiency:     {q.get('sufficiency_decision')}")
        print(f"  Failure Cat:     {q.get('failure_category')}")
        print(f"  Expected Files:  {q.get('expected_files')}")
        print(f"  Retrieved Files: {q.get('retrieved_files')[:3]}")
        print(f"  Final Status:    {q.get('final_answer_status')}")
        print(f"  Explanation:     {q.get('diagnostic_explanation')}")
        print()

print("--- Questions Correct but marked Not Grounded ---")
for q in questions:
    if q.get("correctness") == "correct" and not q.get("grounded"):
        print(f"ID: {q['question_id']}")
        print(f"  Repo:            {q['repository']}")
        print(f"  Failure Cat:     {q.get('failure_category')}")
        print(f"  Final Status:    {q.get('final_answer_status')}")
        print(f"  Explanation:     {q.get('diagnostic_explanation')}")
        print()
