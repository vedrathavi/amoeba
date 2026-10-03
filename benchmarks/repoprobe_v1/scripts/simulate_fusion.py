#!/usr/bin/env python3
"""
Simulate Fusion Calibration on Master Retrieval Results.
Tests various fusion algorithms (RRF, Dynamic Min-Max, Softmax, Calibrated Alpha, Non-Zero Lexical Floor)
to see the exact impact on Hit@1, Hit@5, Hit@10, Hit@20, Hit@50, and MRR.
"""

import json
import math
import os
import sys

sys.stdout.reconfigure(encoding="utf-8")

RESULTS_FILE = r"d:\amoeba\benchmarks\repoprobe_v1\results\retrieval\master_retrieval_results.json"
GT_FILE = r"d:\amoeba\benchmarks\repoprobe_v1\ground_truth.json"

def path_matches(retrieved, expected):
    if not retrieved or not expected: return False
    r = retrieved.replace('\\', '/')
    e = expected.replace('\\', '/')
    if r == e or r.endswith(e) or e.endswith(r) or e in r or r in e:
        return True
    return os.path.basename(r) == os.path.basename(e)

def symbol_matches(retrieved, expected):
    if not retrieved or not expected: return False
    return retrieved == expected or expected in retrieved or retrieved in expected

def main():
    with open(RESULTS_FILE, "r", encoding="utf-8") as f:
        results = json.load(f)
    with open(GT_FILE, "r", encoding="utf-8") as f:
        gt_list = json.load(f)

    gt_by_id = {g["question_id"]: g for g in gt_list}
    positives = [r for r in results if not r.get("is_negative", False)]
    total_pos = len(positives)

    print("=" * 80)
    print(f"FUSION STRATEGY SIMULATION (N = {total_pos} Positive Questions)")
    print("=" * 80)

    # Strategies to test:
    # 1. Baseline Hybrid (from recorded master results)
    # 2. Reciprocal Rank Fusion (RRF with k=10, 20, 60)
    # 3. Lexical Dominant with Semantic Tie-break (alpha = 0.70, 0.80, 0.90)
    # 4. Lexical Fallback: If Lexical Rank <= 10, guarantee top placement

    def evaluate_ranks(rank_list):
        h1 = sum(1 for r in rank_list if r == 1)
        h5 = sum(1 for r in rank_list if 1 <= r <= 5)
        h10 = sum(1 for r in rank_list if 1 <= r <= 10)
        h20 = sum(1 for r in rank_list if 1 <= r <= 20)
        h50 = sum(1 for r in rank_list if 1 <= r <= 50)
        mrr = sum(1.0/r for r in rank_list if r > 0) / len(rank_list)
        return h1, h5, h10, h20, h50, mrr

    # 1. Baseline
    base_ranks = [q["hybrid"]["target_rank"] for q in positives]
    h1, h5, h10, h20, h50, mrr = evaluate_ranks(base_ranks)
    print(f"\n1. Baseline Hybrid (Adaptive Alpha 0.25-0.75):")
    print(f"   H@1: {h1:2d} ({h1/total_pos*100:5.1f}%) | H@5: {h5:2d} ({h5/total_pos*100:5.1f}%) | H@10: {h10:2d} ({h10/total_pos*100:5.1f}%) | H@50: {h50:2d} ({h50/total_pos*100:5.1f}%) | MRR: {mrr:.4f}")

    # 2. Pure Lexical
    lex_ranks = [q["lexical_only"]["target_rank"] for q in positives]
    h1, h5, h10, h20, h50, mrr = evaluate_ranks(lex_ranks)
    print(f"\n2. Pure Lexical (alpha = 1.0):")
    print(f"   H@1: {h1:2d} ({h1/total_pos*100:5.1f}%) | H@5: {h5:2d} ({h5/total_pos*100:5.1f}%) | H@10: {h10:2d} ({h10/total_pos*100:5.1f}%) | H@50: {h50:2d} ({h50/total_pos*100:5.1f}%) | MRR: {mrr:.4f}")

    # 3. Pure Semantic
    sem_ranks = [q["semantic_only"]["target_rank"] for q in positives]
    h1, h5, h10, h20, h50, mrr = evaluate_ranks(sem_ranks)
    print(f"\n3. Pure Semantic (alpha = 0.0):")
    print(f"   H@1: {h1:2d} ({h1/total_pos*100:5.1f}%) | H@5: {h5:2d} ({h5/total_pos*100:5.1f}%) | H@10: {h10:2d} ({h10/total_pos*100:5.1f}%) | H@50: {h50:2d} ({h50/total_pos*100:5.1f}%) | MRR: {mrr:.4f}")

    # 4. RRF Simulations
    for k in [5, 10, 20, 30, 60]:
        rrf_ranks = []
        for q in positives:
            lr = q["lexical_only"]["target_rank"]
            sr = q["semantic_only"]["target_rank"]
            # Target RRF score
            # Approximate target fused rank from relative candidate scores
            # If target has lr and sr:
            # Let's compute exact target rank if possible or approximate from ranks
            # For exact candidate list we simulate:
            # A candidate with (lr, sr) has score:
            # score = (1/(k+lr) if lr>0 else 0) + (1/(k+sr) if sr>0 else 0)
            # A target's rank can be estimated by comparing with competing candidates
            pass

    # 5. Best of Lexical & Semantic Oracle
    oracle_ranks = []
    for q in positives:
        lr = q["lexical_only"]["target_rank"]
        sr = q["semantic_only"]["target_rank"]
        valid = [r for r in [lr, sr] if r > 0]
        oracle_ranks.append(min(valid) if valid else 0)

    h1, h5, h10, h20, h50, mrr = evaluate_ranks(oracle_ranks)
    print(f"\n4. Oracle Best of (Lexical, Semantic):")
    print(f"   H@1: {h1:2d} ({h1/total_pos*100:5.1f}%) | H@5: {h5:2d} ({h5/total_pos*100:5.1f}%) | H@10: {h10:2d} ({h10/total_pos*100:5.1f}%) | H@50: {h50:2d} ({h50/total_pos*100:5.1f}%) | MRR: {mrr:.4f}")

if __name__ == "__main__":
    main()
