import json
import sys

sys.stdout.reconfigure(encoding="utf-8")

with open(r"d:\amoeba\benchmarks\repoprobe_v1\questions.json", "r", encoding="utf-8") as f:
    questions = json.load(f)
with open(r"d:\amoeba\benchmarks\repoprobe_v1\ground_truth.json", "r", encoding="utf-8") as f:
    gt_map = {g["question_id"]: g for g in json.load(f)}
with open(r"d:\amoeba\benchmarks\repoprobe_v1\results\latest\questions.json", "r", encoding="utf-8") as f:
    eval_map = {e["question_id"]: e for e in json.load(f)}

# Criteria for classification:
# 1. Code-retrievable: The answer/evidence is contained in code/AST symbols (functions, types, constants, structs, logic).
# 2. Documentation/configuration-retrievable: The answer/evidence is in markdown docs, docker-compose, YAML/TOML/JSON configs, deployment scripts.
# 3. Repository-wide reasoning: Requires cross-module reasoning or holistic understanding of architecture across multiple distinct subsystems.
# 4. Runtime/external behavior: Involves external third-party tools (Spark JDBC, BunkerWeb, Cloudflare, Oracle SQL migrations, Traefik container networking, browser cookie cross-origin behavior) or runtime deployment debugging.
# 5. Ambiguous / questionable: Question has malformed expected targets, contradictory reference answers, or is ambiguous.

def classify_question(q, gt):
    qid = q["question_id"]
    qtext = q["question"].lower()
    ref = q.get("reference_answer", "").lower()
    exp_files = gt.get("expected_files", [])
    
    # Check for negative refusal probes
    if not exp_files and not gt.get("expected_symbols"):
        return "Code-retrievable", "Negative probe evaluating principled refusal on absent code features"

    # Check for external runtime behavior
    if any(k in qtext for k in ["bunkerweb", "spark", "jdbc", "traefik.me", "surma/simplehttp2server", "oracle", "cross-origin", "cookieprefix", "http2", "http3"]) or \
       any(k in ref for k in ["bunkerweb", "traefik.dynamic", "middlewares.yml", "spark's dataframe"]):
        if "bunkerweb" in qtext or "simplehttp2server" in qtext:
            return "Runtime/external behavior", "Asks about external runtime reverse proxies/certificates or 3rd party engine behavior"
        if "spark via jdbc" in qtext:
            return "Runtime/external behavior", "Asks about Spark JDBC batching behavior and Parquet file fragmentation"

    # Check for docs/config retrievable
    if any(f.endswith(".md") or f.endswith(".yml") or f.endswith(".yaml") or f.endswith(".json") or f.endswith(".toml") or "README" in f for f in exp_files) or \
       "install" in qtext or "cli flags" in qtext or "configuration parameter" in qtext and not any(f.endswith((".cpp", ".go", ".py", ".rs", ".java", ".ts")) for f in exp_files):
        return "Documentation/configuration-retrievable", "Target information resides in non-code documentation or config files"

    # Check for repository-wide reasoning
    if q.get("taxonomy") == "Project Architecture" and len(exp_files) > 2:
        return "Repository-wide reasoning", "Requires synthesizing multi-service or repository-wide architectural patterns"
    
    # Check for ambiguity
    if "Configure a wildcard domain" in str(exp_files):
        return "Ambiguous / questionable", "Expected file contains malformed prompt snippet instead of clean filepath"

    # Default to code-retrievable
    return "Code-retrievable", "Target symbols, types, or functions exist directly in source code AST"

classifications = {}
cat_counts = {}

for q in questions:
    qid = q["question_id"]
    gt = gt_map.get(qid, {})
    cat, rationale = classify_question(q, gt)
    classifications[qid] = {"category": cat, "rationale": rationale}
    cat_counts[cat] = cat_counts.get(cat, 0) + 1

print("=================================================================")
print("BENCHMARK GROUND TRUTH AUDIT (70 QUESTIONS)")
print("=================================================================")
for cat, cnt in sorted(cat_counts.items(), key=lambda x: x[1], reverse=True):
    print(f"  - {cat:42}: {cnt:2} ({100.0 * cnt / len(questions):.1f}%)")
print("-----------------------------------------------------------------")

print("\nDetailed Question Classifications:")
for q in questions:
    qid = q["question_id"]
    gt = gt_map.get(qid, {})
    ev = eval_map.get(qid, {})
    c = classifications[qid]
    print(f"[{qid}] ({q['repo_name']}, {q.get('taxonomy')}) -> {c['category']}")
    print(f"   Correctness: {ev.get('correctness')} | Hit@10: {ev.get('retrieval_hit_at_10')} | FailCat: {ev.get('failure_category')}")
    print(f"   Rationale:   {c['rationale']}")
