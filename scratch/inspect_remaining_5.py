import json

with open("d:/amoeba/benchmarks/repoprobe_v1/results/top50_bounded_investigation/questions.json", "r", encoding="utf-8") as f:
    qs = json.load(f)

by_id = {q["question_id"]: q for q in qs}

remaining_5 = ["adk-python-5", "better-auth-0", "better-auth-4", "jiff-4", "walker-3"]

for qid in remaining_5:
    q = by_id[qid]
    print(f"=== {qid} ({q['repository']}) ===")
    print(f"Reason: {q['sufficiency_reason']}")
    print(f"Diagnostic: {q['diagnostic_explanation']}")
    print(f"Retrieved: {q['retrieved_symbols'][:5]}")
    print()
