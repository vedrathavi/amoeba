import json
import os

with open('benchmarks/repoprobe_v1/results/latest/questions.json', 'r', encoding='utf-8') as fp:
    all_q = json.load(fp)

print(f"Total questions loaded: {len(all_q)}")

neg_ids = ['amoeba-adk-python-02', 'amoeba-opencloud-02', 'amoeba-pkl-02', 'amoeba-jiff-02']
print("\n=== NEGATIVE CONTROLS ===")
for q in all_q:
    if q['question_id'] in neg_ids or (not q.get('expected_files') and not q.get('expected_symbols')):
        print(f"{q['question_id']}: correctness={q.get('correctness')}, sufficiency={q.get('sufficiency_decision')}, failure={q.get('failure_category')}, reason={q.get('sufficiency_reason')}")

forensic_15 = [
    'better-auth-0', 'dokploy-2', 'pkl-0', 'pkl-2', 'beszel-0', 'beszel-3',
    'jiff-0', 'jiff-1', 'jiff-2', 'jiff-3', 'jiff-4', 'walker-0', 'walker-2', 'walker-8', 'docling-0'
]
print("\n=== 15 FORENSIC CASES ===")
for q in all_q:
    if q['question_id'] in forensic_15:
        print(f"{q['question_id']}: hit10={q.get('retrieval_hit_at_10')}, sufficiency={q.get('sufficiency_decision')}, correctness={q.get('correctness')}, failure={q.get('failure_category')}")
