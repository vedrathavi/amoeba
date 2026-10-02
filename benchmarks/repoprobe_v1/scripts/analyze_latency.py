import json

with open(r"d:\amoeba\benchmarks\repoprobe_v1\results\latest\questions.json", "r", encoding="utf-8") as f:
    questions = json.load(f)

repo_latencies = {}
for q in questions:
    rp = q["repository"]
    repo_latencies.setdefault(rp, []).append(q["latency_ms"])

print("=================================================================")
print("LATENCY BREAKDOWN PER REPOSITORY (AVG QUERY TIME)")
print("=================================================================")
for rp, lats in sorted(repo_latencies.items(), key=lambda x: sum(x[1])/len(x[1]), reverse=True):
    avg_l = sum(lats) / len(lats)
    min_l = min(lats)
    max_l = max(lats)
    print(f"{rp:28}: Avg {avg_l:8.1f} ms  (Min: {min_l:7.1f} ms, Max: {max_l:8.1f} ms) [{len(lats)} queries]")
