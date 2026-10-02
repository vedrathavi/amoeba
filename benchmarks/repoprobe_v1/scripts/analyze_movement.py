import json

with open('benchmarks/repoprobe_v1/results/latest/questions.json', 'r', encoding='utf-8') as f:
    questions = json.load(f)

print(f"Total questions: {len(questions)}")
correct = [q for q in questions if q.get('correctness') == 'correct']
print(f"Correct questions ({len(correct)}):")
for q in correct:
    print(f"  [{q['question_id']}] ({q['source']}, {q.get('repository')}) hit1={q.get('retrieval_hit_at_1')} hit5={q.get('retrieval_hit_at_5')} hit10={q.get('retrieval_hit_at_10')}")

print("\nBreakdown by failure category:")
for cat in ['none', 'retrieval_failure', 'sufficiency_failure', 'grounding_failure', 'reasoning_failure']:
    qs = [q for q in questions if q.get('failure_category') == cat]
    print(f"  {cat}: {len(qs)}")
    for q in qs:
        if q.get('source') == 'repoprobe':
            print(f"    * RepoProbe [{q['question_id']}] ({q.get('repository')}) hit10={q.get('retrieval_hit_at_10')} suff_reason={q.get('sufficiency_reason')}")
        else:
            if cat != 'none':
                print(f"    - Amoeba [{q['question_id']}] ({q.get('repository')}) hit10={q.get('retrieval_hit_at_10')} suff_reason={q.get('sufficiency_reason')}")
