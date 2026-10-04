import json

with open("d:/amoeba/benchmarks/repoprobe_v1/results/top50_bounded_investigation/questions.json", "r", encoding="utf-8") as f:
    qs = json.load(f)

by_id = {q["question_id"]: q for q in qs}

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

print("=== TARGET 12 CASES AUDIT (PHASE 9.3A) ===")
resolved_count = 0
for tid in target_12:
    q = by_id.get(tid)
    if not q:
        print(f"{tid}: NOT FOUND")
        continue
    suff = q.get('sufficiency_decision')
    corr = q.get('correctness')
    fail_cat = q.get('failure_category')
    print(f"ID: {tid}")
    print(f"  Repo:               {q.get('repository')}")
    print(f"  Sufficiency:        {suff}")
    print(f"  Sufficiency Reason: {q.get('sufficiency_reason')}")
    print(f"  Correctness:        {corr}")
    print(f"  Failure Category:   {fail_cat}")
    print(f"  Target Rank:        {q.get('target_rank_in_hop0')}")
    print(f"  Hit@10:             {q.get('retrieval_hit_at_10')}")
    print(f"  Hit@50:             {q.get('retrieval_hit_at_50')}")
    if suff is True:
        resolved_count += 1
    print()

print(f"Sufficiency Gates Passed on Target 12: {resolved_count}/12")

print("\n=== NEGATIVE QUESTIONS AUDIT ===")
negs = [q for q in qs if not q["expected_files"] and not q["expected_symbols"]]
for n in negs:
    print(f"ID: {n['question_id']} | Repo: {n['repository']} | Correctness: {n['correctness']} | Suff: {n.get('sufficiency_decision')} | Reason: {n.get('sufficiency_reason')}")
