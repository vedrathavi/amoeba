import json

with open('benchmarks/repoprobe_v1/results/latest/questions.json', 'r', encoding='utf-8') as fp:
    all_q = json.load(fp)

for q in all_q:
    if q['question_id'] in ['amoeba-adk-python-02', 'amoeba-pkl-02']:
        print(f"ID: {q['question_id']}")
        print(f"Files: {q.get('retrieved_files')}")
        print(f"Symbols: {q.get('retrieved_symbols')}")
        print(f"Reason: {q.get('sufficiency_reason')}")
        print("-" * 50)
