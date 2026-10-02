import json

AUDIT_FILE = r"d:\amoeba\benchmarks\repoprobe_v1\results\semantic_audit\semantic_audit_full.json"
with open(AUDIT_FILE, "r", encoding="utf-8") as f:
    results = json.load(f)

cases = [
    ("ducklake-0", "DuckLake custom function/macro"),
    ("ducklake-1", "DuckLake Spark/JDBC parquet behavior"),
    ("adk-python-2", "ADK JWT/authentication"),
    ("adk-python-3", "ADK Runner lifecycle"),
    ("ducklake-3", "DuckLake PostgreSQL schema"),
]

for qid, label in cases:
    match = [r for r in results if r["question_id"] == qid]
    if match:
        m = match[0]
        print(f"==================================================")
        print(f"CASE: {label} ({qid})")
        print(f"QUESTION: {m['question_text'][:200]}...")
        print(f"EXPECTED SYMBOL: {m.get('expected_unit_text', 'N/A')}")
        print(f"SEMANTIC RANK: {m['sem_target_rank']} (Score: {m['sem_target_score']:.4f})")
        print(f"TOP-1 RETRIEVED TEXT:\n{m['top1_sem_text']}")
        print(f"==================================================\n")
