import json

with open("d:/amoeba/benchmarks/repoprobe_v1/results/top50_bounded_investigation/questions.json", "r", encoding="utf-8") as f:
    qs = [q for q in json.load(f) if q.get("expected_files") or q.get("expected_symbols")]

cat_b = []
for q in qs:
    corr = q.get("correctness")
    in_top50 = q.get("hop0_hit_at_50", False)
    ret_hit10 = q.get("retrieval_hit_at_10", False)
    
    if corr != "correct" and in_top50 and not ret_hit10:
        cat_b.append(q)

print(f"Total Category B Questions: {len(cat_b)}\n")

for q in cat_b:
    print(f"=== {q['question_id']} ({q['repository']}) ===")
    print(f"  Target Rank in Hop-0: {q.get('target_rank_in_hop0')}")
    print(f"  In Top 10 Hop-0:      {q.get('hop0_hit_at_10')}")
    print(f"  In Top 20 Hop-0:      {q.get('hop0_hit_at_20')}")
    print(f"  In Top 50 Hop-0:      {q.get('hop0_hit_at_50')}")
    print(f"  Target as Seed:       {q.get('target_selected_as_seed')}")
    print(f"  Expected Syms:        {q.get('expected_symbols')}")
    print(f"  Expected Files:       {q.get('expected_files')}")
    print(f"  Retrieved Syms (10):  {q.get('retrieved_symbols')}")
    print(f"  Retrieved Files (10): {q.get('retrieved_files')}")
    print()
