import json

BEFORE_PATH = "d:/amoeba/benchmarks/repoprobe_v1/results/latest/questions.json"
AFTER_PATH = "d:/amoeba/benchmarks/repoprobe_v1/results/top50_bounded_investigation/questions.json"

with open(BEFORE_PATH, "r", encoding="utf-8") as f:
    before_map = {q["question_id"]: q for q in json.load(f)}

with open(AFTER_PATH, "r", encoding="utf-8") as f:
    after_map = {q["question_id"]: q for q in json.load(f)}

questions_to_inspect = [
    "adk-python-2",
    "better-auth-2",
    "amoeba-dokploy-01",
    "adk-python-3",
    "opencloud-2",
    "opencloud-11"
]

for qid in questions_to_inspect:
    b = before_map[qid]
    a = after_map[qid]
    print("=================================================================")
    print(f"QUESTION: {qid} ({b['repository']})")
    print("=================================================================")
    print(f"Expected Files:   {b.get('expected_files')}")
    print(f"Expected Symbols: {b.get('expected_symbols')}")
    print(f"Before Correctness: {b.get('correctness')} | Sufficiency: {b.get('sufficiency_decision')}")
    print(f"Before Reason:      {b.get('sufficiency_reason')}")
    print(f"Before Retrieved:   {b.get('retrieved_symbols')[:5]}")
    print(f"Before Answer:      {b.get('final_answer')}")
    print()
    print(f"After Correctness:  {a.get('correctness')} | Sufficiency: {a.get('sufficiency_decision')}")
    print(f"After Reason:       {a.get('sufficiency_reason')}")
    print(f"After Retrieved:    {a.get('retrieved_symbols')[:5]}")
    print(f"After Answer:       {a.get('final_answer')}")
    print(f"Diagnostic:         {a.get('diagnostic_explanation')}")
    print()
