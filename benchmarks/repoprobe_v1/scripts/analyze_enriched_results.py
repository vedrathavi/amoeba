import json
import os

AUDIT_FILE = r"d:\amoeba\benchmarks\repoprobe_v1\results\semantic_audit\semantic_audit_full.json"

with open(AUDIT_FILE, "r", encoding="utf-8") as f:
    results = json.load(f)

print(f"Total results: {len(results)}")

positive_q = [r for r in results if r["source"] != "negative_refusal" and r["difficulty"] != "5"]
print(f"Positive questions count: {len(positive_q)}")

def calc_recall(k_name):
    hits = sum(1 for r in positive_q if r.get(k_name, False))
    pct = (hits / len(positive_q)) * 100 if positive_q else 0.0
    return hits, pct

for k, name in [(1, "sem_hit_1"), (5, "sem_hit_5"), (10, "sem_hit_10"), (20, "sem_hit_20"), (50, "sem_hit_50"), (100, "sem_hit_100")]:
    hits, pct = calc_recall(name)
    print(f"Semantic Top-{k:3d}: {hits:2d}/{len(positive_q)} ({pct:.1f}%)")

lex_hits = sum(1 for r in positive_q if r.get("lex_hit_10", False))
print(f"Lexical Top-10:   {lex_hits:2d}/{len(positive_q)} ({(lex_hits/len(positive_q))*100:.1f}%)")

hyb_hits = sum(1 for r in positive_q if r.get("hyb_hit_10", False))
print(f"Hybrid Top-10:    {hyb_hits:2d}/{len(positive_q)} ({(hyb_hits/len(positive_q))*100:.1f}%)")
