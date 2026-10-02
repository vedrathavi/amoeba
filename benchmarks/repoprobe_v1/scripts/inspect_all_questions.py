import json

with open(r"d:\amoeba\benchmarks\repoprobe_v1\questions.json", "r", encoding="utf-8") as f:
    questions = json.load(f)
with open(r"d:\amoeba\benchmarks\repoprobe_v1\ground_truth.json", "r", encoding="utf-8") as f:
    gt_map = {g["question_id"]: g for g in json.load(f)}

# Let's inspect each question
for q in questions:
    qid = q["question_id"]
    gt = gt_map.get(qid, {})
    qtext = q["question"][:120].replace("\n", " ")
    exp_files = gt.get("expected_files", [])
    exp_syms = gt.get("expected_symbols", [])
    print(f"[{qid}] ({q['repo_name']}, {q.get('taxonomy')})")
    print(f"  Q: {qtext}")
    print(f"  Exp Files: {exp_files}")
    print(f"  Exp Syms:  {exp_syms}")
    print()
