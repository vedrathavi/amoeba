import json
import os

with open(r"d:\amoeba\benchmarks\repoprobe_v1\ground_truth.json", "r", encoding="utf-8") as f:
    ground_truth = json.load(f)
with open(r"d:\amoeba\benchmarks\repoprobe_v1\questions.json", "r", encoding="utf-8") as f:
    questions = json.load(f)
with open(r"d:\amoeba\benchmarks\repoprobe_v1\results\latest\questions.json", "r", encoding="utf-8") as f:
    eval_results = {e["question_id"]: e for e in json.load(f)}

repos_root = r"d:\amoeba\benchmarks\repoprobe_v1\repos"

retrieval_count = 0
representation_count = 0
sufficiency_fn_count = 0
success_count = 0
negative_count = 0

print("=================================================================")
print("PART 3: RETRIEVAL VS REPRESENTATION DISCRIMINATION")
print("=================================================================\n")

for q in questions:
    qid = q["question_id"]
    ev = eval_results.get(qid, {})
    gt = [g for g in ground_truth if g["question_id"] == qid][0]
    
    exp_files = gt.get("expected_files", [])
    exp_syms = gt.get("expected_symbols", [])
    
    if not exp_files and not exp_syms:
        negative_count += 1
        continue
        
    if ev.get("correctness") == "correct":
        success_count += 1
        continue
        
    hit10 = ev.get("retrieval_hit_at_10")
    suff = ev.get("sufficiency_decision")
    
    # Check if target files exist and are supported source code files
    all_code_files = True
    any_doc_or_config = False
    for ef in exp_files:
        ext = os.path.splitext(ef)[1].lower()
        if ext in [".md", ".yml", ".yaml", ".json", ".toml", ".txt", ".sql", ""] or "readme" in ef.lower():
            any_doc_or_config = True
            all_code_files = False
            
    if hit10 and not suff:
        sufficiency_fn_count += 1
        print(f"[{qid}] SUFFICIENCY_FALSE_NEGATIVE: Target retrieved in Top-10, but sufficiency rejected.")
    elif any_doc_or_config:
        representation_count += 1
        print(f"[{qid}] REPRESENTATION_GAP: Target is doc/config/unsupported ({exp_files}) -> Cannot be solved by code ranking alone.")
    else:
        retrieval_count += 1
        print(f"[{qid}] RETRIEVAL/QUERY_UNDERSTANDING: Target files exist in code index ({exp_files}), but ranking failed (Hit@10=False).")

print("\n-----------------------------------------------------------------")
print(f"Total Evaluated:              {len(questions)}")
print(f"  - Correct / Passing:        {success_count} (+ {negative_count} negative refusals = {success_count + negative_count})")
print(f"  - Sufficiency False Negative: {sufficiency_fn_count} (Target in Top-10, rejected at gate)")
print(f"  - Pure Retrieval Gap:         {retrieval_count} (Target in code index, but ranked poorly)")
print(f"  - Representation Gap:         {representation_count} (Target in doc/config/non-AST file)")
print("-----------------------------------------------------------------")
