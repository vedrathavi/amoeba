import json
import sys

sys.stdout.reconfigure(encoding="utf-8")

with open(r"d:\amoeba\benchmarks\repoprobe_v1\results\latest\failures.json", "r", encoding="utf-8") as f:
    failures = json.load(f)
with open(r"d:\amoeba\benchmarks\repoprobe_v1\questions.json", "r", encoding="utf-8") as f:
    q_map = {q["question_id"]: q for q in json.load(f)}
with open(r"d:\amoeba\benchmarks\repoprobe_v1\ground_truth.json", "r", encoding="utf-8") as f:
    gt_map = {g["question_id"]: g for g in json.load(f)}

# Select 20 representative failures across repositories and taxonomies
target_qids = [
    "ducklake-0",
    "ducklake-1",
    "ducklake-3",
    "adk-python-0",
    "adk-python-1",
    "adk-python-2",
    "better-auth-0",
    "better-auth-4",
    "dokploy-0",
    "dokploy-1",
    "dokploy-5",
    "pkl-0",
    "pkl-2",
    "beszel-0",
    "beszel-2",
    "opencloud-0",
    "opencloud-2",
    "jiff-1",
    "walker-0",
    "docling-0",
    "amoeba-ducklake-02",
    "amoeba-dokploy-01",
    "amoeba-better-auth-02"
]

selected = [f for f in failures if f["question_id"] in target_qids]
print(f"Selected {len(selected)} failures for deep audit.\n")

for item in selected:
    qid = item["question_id"]
    q = q_map.get(qid, {})
    gt = gt_map.get(qid, {})
    print(f"=================================================================")
    print(f"Question ID: {qid} ({item['repository']}, Source: {item['source']}, Tax: {item['taxonomy']})")
    print(f"Question:    {q.get('question', '')[:140].replace(chr(10), ' ')}...")
    print(f"Expected:    Files: {item.get('expected_files')} | Symbols: {item.get('expected_symbols')}")
    print(f"Retrieved:   Files: {item.get('retrieved_files')[:3]}")
    print(f"             Symbols: {item.get('retrieved_symbols')[:3]}")
    print(f"Hit@10:      {item.get('retrieval_hit_at_10')}")
    print(f"Sufficiency: {item.get('sufficiency_decision')} (Reason: {item.get('sufficiency_reason')})")
    print(f"Pipeline FailCat: {item.get('failure_category')}")
