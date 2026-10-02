import json

with open('benchmarks/repoprobe_v1/results/latest/questions.json', 'r', encoding='utf-8') as f:
    questions = json.load(f)

by_repo = {}
for q in questions:
    repo = q.get('repository')
    by_repo.setdefault(repo, []).append(q)

print("Latency per repository:")
for repo, qs in by_repo.items():
    avg_retr = sum(q.get('retrieval_latency_ms', 0.0) for q in qs) / len(qs)
    avg_total = sum(q.get('total_latency_ms', 0.0) for q in qs) / len(qs)
    print(f"  {repo}: {len(qs)} questions | Avg Retrieval: {avg_retr:.1f} ms | Avg Total: {avg_total:.1f} ms")
