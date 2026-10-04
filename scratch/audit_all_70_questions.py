import json

BEFORE_PATH = "d:/amoeba/benchmarks/repoprobe_v1/results/latest/questions.json"
AFTER_PATH = "d:/amoeba/benchmarks/repoprobe_v1/results/top50_bounded_investigation/questions.json"

with open(BEFORE_PATH, "r", encoding="utf-8") as f:
    before_qs = json.load(f)

with open(AFTER_PATH, "r", encoding="utf-8") as f:
    after_qs = json.load(f)

before_map = {q["question_id"]: q for q in before_qs}
after_map = {q["question_id"]: q for q in after_qs}

print("=================================================================")
print("ALL QUESTIONS COMPARISON (BEFORE VS AFTER)")
print("=================================================================")

changed_correctness = []
changed_sufficiency = []
changed_rank = []

for qid, b_q in before_map.items():
    a_q = after_map[qid]
    
    b_corr = b_q.get("correctness")
    a_corr = a_q.get("correctness")
    
    b_suff = b_q.get("sufficiency_decision")
    a_suff = a_q.get("sufficiency_decision")
    
    b_rank = b_q.get("target_rank_in_hop0")
    a_rank = a_q.get("target_rank_in_hop0")
    
    if b_corr != a_corr:
        changed_correctness.append((qid, b_q["repository"], b_corr, a_corr, b_suff, a_suff, b_rank, a_rank))
    if b_suff != a_suff:
        changed_sufficiency.append((qid, b_q["repository"], b_suff, a_suff, b_q.get("sufficiency_reason"), a_q.get("sufficiency_reason")))
    if b_rank != a_rank:
        changed_rank.append((qid, b_q["repository"], b_rank, a_rank))

print(f"Total Questions with Changed Correctness: {len(changed_correctness)}")
for qid, repo, bc, ac, bs, asuf, br, ar in changed_correctness:
    print(f"  {qid:<25} ({repo:<20}): Correctness {bc} -> {ac} | Suff {bs} -> {asuf} | Rank {br} -> {ar}")

print(f"\nTotal Questions with Changed Sufficiency: {len(changed_sufficiency)}")
for qid, repo, bs, asuf, br, ar in changed_sufficiency:
    print(f"  {qid:<25} ({repo:<20}): Suff {bs} -> {asuf}\n    Before Reason: {br}\n    After Reason:  {ar}")

print(f"\nTotal Questions with Changed Hop-0 Rank: {len(changed_rank)}")
for qid, repo, br, ar in changed_rank:
    print(f"  {qid:<25} ({repo:<20}): Rank {br} -> {ar}")
