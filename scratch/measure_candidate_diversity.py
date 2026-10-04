import json

with open("d:/amoeba/benchmarks/repoprobe_v1/results/top50_bounded_investigation/questions.json", "r", encoding="utf-8") as f:
    qs = [q for q in json.load(f) if q.get("expected_files") or q.get("expected_symbols")]

print(f"Total positive questions: {len(qs)}")

total_ret_count = 0
total_unique_files = 0
total_unique_syms = 0

crowding_stats = []

for q in qs:
    r_files = q.get("retrieved_files", [])
    r_syms = q.get("retrieved_symbols", [])
    
    unique_f = len(set(r_files))
    unique_s = len(set(r_syms))
    
    total_ret_count += len(r_files)
    total_unique_files += unique_f
    total_unique_syms += unique_s
    
    # Measure same-file duplicates
    file_counts = {}
    for f in r_files:
        file_counts[f] = file_counts.get(f, 0) + 1
    
    max_file_count = max(file_counts.values()) if file_counts else 0
    crowding_stats.append({
        "question_id": q["question_id"],
        "repo": q["repository"],
        "total_cands": len(r_files),
        "unique_files": unique_f,
        "unique_syms": unique_s,
        "max_cands_per_file": max_file_count,
        "is_correct": q.get("correctness") == "correct",
        "failure_cat": q.get("failure_category"),
    })

avg_cands = total_ret_count / len(qs)
avg_files = total_unique_files / len(qs)
avg_syms = total_unique_syms / len(qs)

print("\n=== CANDIDATE DIVERSITY SUMMARY (Across Top-10 Assembled Pools) ===")
print(f"Avg Candidates per Question:   {avg_cands:.2f}")
print(f"Avg Unique Files per Question: {avg_files:.2f} ({(avg_files/avg_cands)*100:.1f}% unique)")
print(f"Avg Unique Syms per Question:  {avg_syms:.2f} ({(avg_syms/avg_cands)*100:.1f}% unique)")

# Crowding breakdown
severe_crowding = [c for c in crowding_stats if c["max_cands_per_file"] >= 3]
print(f"Questions with >= 3 candidates from the EXACT same file: {len(severe_crowding)} / {len(qs)} ({len(severe_crowding)/len(qs)*100:.1f}%)")

print("\n--- Examples of severe file crowding in failing questions ---")
for sc in severe_crowding:
    if not sc["is_correct"]:
        print(f"  {sc['question_id']:<22} ({sc['repo']:<20}) | Total={sc['total_cands']} UniqueFiles={sc['unique_files']} MaxInOneFile={sc['max_cands_per_file']} | FailCat={sc['failure_cat']}")
