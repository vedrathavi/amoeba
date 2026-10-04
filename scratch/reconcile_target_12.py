import json

BEFORE_PATH = "d:/amoeba/benchmarks/repoprobe_v1/results/latest/questions.json"
AFTER_PATH = "d:/amoeba/benchmarks/repoprobe_v1/results/top50_bounded_investigation/questions.json"

with open(BEFORE_PATH, "r", encoding="utf-8") as f:
    b_map = {q["question_id"]: q for q in json.load(f)}

with open(AFTER_PATH, "r", encoding="utf-8") as f:
    a_map = {q["question_id"]: q for q in json.load(f)}

target_12 = [
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

print("=================================================================")
print("STEP 3: 12 TARGET CASES QUESTION-LEVEL RECONCILIATION")
print("=================================================================")

for tid in target_12:
    b = b_map[tid]
    a = a_map[tid]
    
    b_suff = b.get("sufficiency_decision")
    a_suff = a.get("sufficiency_decision")
    b_corr = b.get("correctness")
    a_corr = a.get("correctness")
    
    status_change = f"{b_corr} (suff={b_suff}) -> {a_corr} (suff={a_suff})"
    print(f"Question ID: {tid:<20} | Repo: {b['repository']}")
    print(f"  Status Change:  {status_change}")
    print(f"  Before Reason:  {b.get('sufficiency_reason')}")
    print(f"  After Reason:   {a.get('sufficiency_reason')}")
    print(f"  Hop-0 Rank:     {b.get('target_rank_in_hop0')} -> {a.get('target_rank_in_hop0')}")
    print(f"  Retrieved Syms: {a.get('retrieved_symbols')[:4]}")
    print(f"  Expected Syms:  {b.get('expected_symbols')}")
    print()
