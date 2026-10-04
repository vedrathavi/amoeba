import json
import os

BEFORE_PATH = "d:/amoeba/benchmarks/repoprobe_v1/results/latest/questions.json"
AFTER_PATH = "d:/amoeba/benchmarks/repoprobe_v1/results/top50_bounded_investigation/questions.json"

with open(BEFORE_PATH, "r", encoding="utf-8") as f:
    before_qs = json.load(f)

with open(AFTER_PATH, "r", encoding="utf-8") as f:
    after_qs = json.load(f)

before_map = {q["question_id"]: q for q in before_qs}
after_map = {q["question_id"]: q for q in after_qs}

print("=================================================================")
print("STEP 1 & 2: DETAILED METRICS COMPUTATION (BEFORE VS AFTER)")
print("=================================================================")

def compute_metrics(qs, name=""):
    total = len(qs)
    pos = [q for q in qs if q.get("expected_files") or q.get("expected_symbols")]
    neg = [q for q in qs if not q.get("expected_files") and not q.get("expected_symbols")]
    
    repoprobe_qs = [q for q in qs if not q["question_id"].startswith("amoeba-")]
    amoeba_qs = [q for q in qs if q["question_id"].startswith("amoeba-")]
    
    correct = sum(1 for q in qs if q.get("correctness") == "correct")
    grounded = sum(1 for q in qs if q.get("grounded", False))
    neg_correct = sum(1 for q in neg if q.get("correctness") == "correct")
    
    rp_correct = sum(1 for q in repoprobe_qs if q.get("correctness") == "correct")
    am_correct = sum(1 for q in amoeba_qs if q.get("correctness") == "correct")
    
    # Hits on positive questions (66)
    hop0_hit50 = sum(1 for q in pos if q.get("hop0_hit_at_50", False))
    final_hit10 = sum(1 for q in pos if q.get("retrieval_hit_at_10", False))
    final_hit5 = sum(1 for q in pos if q.get("retrieval_hit_at_5", False))
    final_hit1 = sum(1 for q in pos if q.get("retrieval_hit_at_1", False))
    
    mrr_hop0 = sum(1.0 / q["target_rank_in_hop0"] for q in pos if q.get("target_rank_in_hop0", 0) > 0) / len(pos)
    
    # Failures by category
    fail_counts = {}
    for q in qs:
        cat = q.get("failure_category", "none")
        fail_counts[cat] = fail_counts.get(cat, 0) + 1
        
    suff_fails = sum(1 for q in pos if q.get("sufficiency_decision") is False)
    # investigation fails
    inv_fails = fail_counts.get("investigation_failure", 0)
    ret_fails = fail_counts.get("retrieval_failure", 0)
    ans_fails = fail_counts.get("answer_failure", 0)
    
    # Latencies
    avg_total_lat = sum(q.get("total_latency_ms", 0.0) for q in qs) / total
    avg_ret_lat = sum(q.get("retrieval_latency_ms", 0.0) for q in qs) / total
    avg_suff_lat = sum(q.get("sufficiency_latency_ms", 0.0) for q in qs) / total
    avg_inv_lat = sum(q.get("investigation_latency_ms", 0.0) for q in qs) / total
    avg_seed_lat = sum(q.get("seed_selection_latency_ms", 0.0) for q in qs) / total
    avg_reas_lat = sum(q.get("reasoning_latency_ms", 0.0) for q in qs) / total

    return {
        "total": total,
        "pos_count": len(pos),
        "neg_count": len(neg),
        "correct": correct,
        "overall_acc_pct": correct / total * 100,
        "rp_total": len(repoprobe_qs),
        "rp_correct": rp_correct,
        "rp_acc_pct": rp_correct / len(repoprobe_qs) * 100,
        "am_total": len(amoeba_qs),
        "am_correct": am_correct,
        "am_acc_pct": am_correct / len(amoeba_qs) * 100,
        "neg_correct": neg_correct,
        "neg_acc_pct": neg_correct / len(neg) * 100 if neg else 0,
        "hop0_hit50": hop0_hit50,
        "hop0_hit50_pct": hop0_hit50 / len(pos) * 100,
        "final_hit10": final_hit10,
        "final_hit10_pct": final_hit10 / len(pos) * 100,
        "final_hit5": final_hit5,
        "final_hit5_pct": final_hit5 / len(pos) * 100,
        "final_hit1": final_hit1,
        "final_hit1_pct": final_hit1 / len(pos) * 100,
        "mrr_hop0": mrr_hop0,
        "fail_counts": fail_counts,
        "suff_fails": suff_fails,
        "inv_fails": inv_fails,
        "ret_fails": ret_fails,
        "ans_fails": ans_fails,
        "avg_total_lat": avg_total_lat,
        "avg_ret_lat": avg_ret_lat,
        "avg_suff_lat": avg_suff_lat,
        "avg_inv_lat": avg_inv_lat,
        "avg_seed_lat": avg_seed_lat,
        "avg_reas_lat": avg_reas_lat,
    }

b_m = compute_metrics(before_qs, "Before")
a_m = compute_metrics(after_qs, "After")

print(f"{'Metric':<30} | {'Before (Phase 9.1/9.2)':<22} | {'After (Phase 9.3A)':<22} | {'Abs Delta':<12} | {'Rel Delta':<12}")
print("-" * 108)

def format_row(name, b_val, a_val, b_pct, a_pct, is_pct=True):
    if is_pct:
        abs_d = f"{a_pct - b_pct:+.2f}%"
        rel_d = f"{(a_pct - b_pct) / b_pct * 100:+.2f}%" if b_pct != 0 else "N/A"
        b_str = f"{b_pct:.2f}% ({b_val})"
        a_str = f"{a_pct:.2f}% ({a_val})"
    else:
        abs_d = f"{a_val - b_val:+.4f}"
        rel_d = f"{(a_val - b_val) / b_val * 100:+.2f}%" if b_val != 0 else "N/A"
        b_str = f"{b_val:.4f}"
        a_str = f"{a_val:.4f}"
    print(f"{name:<30} | {b_str:<22} | {a_str:<22} | {abs_d:<12} | {rel_d:<12}")

format_row("Overall Accuracy", f"{b_m['correct']}/{b_m['total']}", f"{a_m['correct']}/{a_m['total']}", b_m['overall_acc_pct'], a_m['overall_acc_pct'])
format_row("RepoProbe Accuracy", f"{b_m['rp_correct']}/{b_m['rp_total']}", f"{a_m['rp_correct']}/{a_m['rp_total']}", b_m['rp_acc_pct'], a_m['rp_acc_pct'])
format_row("Amoeba-designed Accuracy", f"{b_m['am_correct']}/{b_m['am_total']}", f"{a_m['am_correct']}/{a_m['am_total']}", b_m['am_acc_pct'], a_m['am_acc_pct'])
format_row("Negative Refusal Accuracy", f"{b_m['neg_correct']}/{b_m['neg_count']}", f"{a_m['neg_correct']}/{a_m['neg_count']}", b_m['neg_acc_pct'], a_m['neg_acc_pct'])
format_row("Hop-0 Hit@50", f"{b_m['hop0_hit50']}/{b_m['pos_count']}", f"{a_m['hop0_hit50']}/{a_m['pos_count']}", b_m['hop0_hit50_pct'], a_m['hop0_hit50_pct'])
format_row("Final Hit@10", f"{b_m['final_hit10']}/{b_m['pos_count']}", f"{a_m['final_hit10']}/{a_m['pos_count']}", b_m['final_hit10_pct'], a_m['final_hit10_pct'])
format_row("Final Hit@5", f"{b_m['final_hit5']}/{b_m['pos_count']}", f"{a_m['final_hit5']}/{a_m['pos_count']}", b_m['final_hit5_pct'], a_m['final_hit5_pct'])
format_row("Final Hit@1", f"{b_m['final_hit1']}/{b_m['pos_count']}", f"{a_m['final_hit1']}/{a_m['pos_count']}", b_m['final_hit1_pct'], a_m['final_hit1_pct'])
format_row("Hop-0 MRR", b_m['mrr_hop0'], a_m['mrr_hop0'], 0, 0, is_pct=False)

print("\n--- Failure Counts ---")
print(f"Evidence Sufficiency Failures: Before={b_m['suff_fails']} | After={a_m['suff_fails']} | Delta={a_m['suff_fails'] - b_m['suff_fails']}")
print(f"Investigation Failures:        Before={b_m['inv_fails']} | After={a_m['inv_fails']} | Delta={a_m['inv_fails'] - b_m['inv_fails']}")
print(f"Retrieval Failures:            Before={b_m['ret_fails']} | After={a_m['ret_fails']} | Delta={a_m['ret_fails'] - b_m['ret_fails']}")
print(f"Answer Failures:               Before={b_m['ans_fails']} | After={a_m['ans_fails']} | Delta={a_m['ans_fails'] - b_m['ans_fails']}")

print("\n--- Latencies (ms) ---")
print(f"Avg Total Latency:       Before={b_m['avg_total_lat']:.2f} ms | After={a_m['avg_total_lat']:.2f} ms")
print(f"Avg Retrieval Latency:   Before={b_m['avg_ret_lat']:.2f} ms | After={a_m['avg_ret_lat']:.2f} ms")
print(f"Avg Sufficiency Latency: Before={b_m['avg_suff_lat']:.2f} ms | After={a_m['avg_suff_lat']:.2f} ms")
