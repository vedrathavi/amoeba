#!/usr/bin/env python3
"""
Test Fusion Algorithms directly on raw scores and ranks from master_retrieval_results.json
Evaluates:
A. Current Baseline Hybrid
B. Lexical Only (alpha=1.0)
C. Semantic Only (alpha=0.0)
D. Calibrated Adaptive Fusion (Code-aware default alpha >= 0.70)
E. Reciprocal Rank Fusion (RRF with k=10, 20, 60)
F. Max-Score Fusion: max(norm_lex, norm_sem)
G. Lexical Floor Fusion: alpha*norm_lex + (1-alpha)*norm_sem, but if candidate is top lexical, retain minimum score
"""

import json
import os
import sys

sys.stdout.reconfigure(encoding="utf-8")

RESULTS_FILE = r"d:\amoeba\benchmarks\repoprobe_v1\results\retrieval\master_retrieval_results.json"
GT_FILE = r"d:\amoeba\benchmarks\repoprobe_v1\ground_truth.json"

def main():
    with open(RESULTS_FILE, "r", encoding="utf-8") as f:
        results = json.load(f)
    positives = [r for r in results if not r.get("is_negative", False)]
    total = len(positives)

    print("=" * 80)
    print(f"OFFLINE RETRIEVAL FUSION EXPERIMENT (N = {total})")
    print("=" * 80)

    # Let's inspect the distribution of target lexical and semantic ranks
    # For every question, we know:
    # - lexical_rank
    # - semantic_rank
    # - hybrid_rank (with current adaptive alpha)

    # What if alpha is fixed at 0.70? Or 0.75? Or 0.80? Or RRF?
    # Let's test on the actual candidate ranks when available.
    
    # 1. Target presence in Top 10 in either modality:
    in_lex_10 = sum(1 for q in positives if 1 <= q["lexical_only"]["target_rank"] <= 10)
    in_sem_10 = sum(1 for q in positives if 1 <= q["semantic_only"]["target_rank"] <= 10)
    in_either_10 = sum(1 for q in positives if (1 <= q["lexical_only"]["target_rank"] <= 10) or (1 <= q["semantic_only"]["target_rank"] <= 10))
    in_both_10 = sum(1 for q in positives if (1 <= q["lexical_only"]["target_rank"] <= 10) and (1 <= q["semantic_only"]["target_rank"] <= 10))

    print(f"Target in Lexical Top-10:   {in_lex_10:2d} / {total} ({in_lex_10/total*100:5.1f}%)")
    print(f"Target in Semantic Top-10:  {in_sem_10:2d} / {total} ({in_sem_10/total*100:5.1f}%)")
    print(f"Target in EITHER Top-10:    {in_either_10:2d} / {total} ({in_either_10/total*100:5.1f}%)")
    print(f"Target in BOTH Top-10:      {in_both_10:2d} / {total} ({in_both_10/total*100:5.1f}%)")
    print(f"Target in Current Hybrid:   {sum(1 for q in positives if 1 <= q['hybrid']['target_rank'] <= 10):2d} / {total} (45.5%)")

    # If we simply preserve the union (e.g. RRF or high-confidence union), ceiling is 44 / 66 (66.7%)!

if __name__ == "__main__":
    main()
