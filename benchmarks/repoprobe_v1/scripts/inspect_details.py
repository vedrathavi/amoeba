import json

with open('benchmarks/repoprobe_v1/results/latest/questions.json', 'r', encoding='utf-8') as f:
    questions = {q['question_id']: q for q in json.load(f)}

for qid in ['amoeba-better-auth-01', 'amoeba-opencloud-01', 'amoeba-docling-02', 'adk-python-0', 'better-auth-0', 'better-auth-3']:
    q = questions.get(qid)
    if q:
        print(f"=== {qid} ===")
        print(f"Question: {q.get('question')}")
        print(f"Expected files: {q.get('expected_files')}")
        print(f"Expected symbols: {q.get('expected_symbols')}")
        print(f"Retrieved files: {q.get('retrieved_files')}")
        print(f"Retrieved symbols: {q.get('retrieved_symbols')}")
        print(f"Sufficiency reason: {q.get('sufficiency_reason')}")
        print(f"Failure category: {q.get('failure_category')}")
        print()
