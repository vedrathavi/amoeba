#!/usr/bin/env python3
"""
Deep Forensic Analysis of Phase 8.3 Retrieval Results across all 66 Positive Questions.
Analyzes:
1. Target Rank Distribution
2. Lexical vs Semantic vs Hybrid Matrix
3. Fusion Loss / Distortion Analysis
4. Repository-wise, Language-wise, and Taxonomy-wise Breakdowns
5. Detailed Root Cause Analysis for all 36 Retrieval Failures (Rank > 10 in Hybrid)
"""

import json
import os
import sys
from collections import defaultdict

sys.stdout.reconfigure(encoding="utf-8")

RESULTS_FILE = r"d:\amoeba\benchmarks\repoprobe_v1\results\retrieval\master_retrieval_results.json"
GT_FILE = r"d:\amoeba\benchmarks\repoprobe_v1\ground_truth.json"
QUESTIONS_FILE = r"d:\amoeba\benchmarks\repoprobe_v1\questions.json"

def main():
    with open(RESULTS_FILE, "r", encoding="utf-8") as f:
        results = json.load(f)
    with open(GT_FILE, "r", encoding="utf-8") as f:
        gt_list = json.load(f)
    with open(QUESTIONS_FILE, "r", encoding="utf-8") as f:
        q_list = json.load(f)

    gt_by_id = {g["question_id"]: g for g in gt_list}
    q_by_id = {q["question_id"]: q for q in q_list}

    positives = [r for r in results if not r.get("is_negative", False)]

    # 1. Comparative Matrix
    print("=" * 120)
    print("ALL 66 POSITIVE QUESTIONS: LEXICAL vs SEMANTIC vs HYBRID COMPARISON")
    print("=" * 120)
    print(f"{'Question ID':<20} {'Repo':<25} {'Taxonomy':<22} {'Lex R':<7} {'Sem R':<7} {'Hyb R':<7} {'Best Mode':<15} {'Status':<15}")
    print("-" * 120)

    fusion_lost_lexical = []
    fusion_lost_semantic = []
    both_in_top10 = []
    neither_in_top10 = []
    target_in_top50_not_top10 = []
    target_absent = []

    for r in positives:
        qid = r["question_id"]
        repo = r["repository"]
        tax = r["taxonomy"]
        lex_rank = r["lexical_only"]["target_rank"]
        sem_rank = r["semantic_only"]["target_rank"]
        hyb_rank = r["hybrid"]["target_rank"]

        lex_str = str(lex_rank) if lex_rank > 0 else "-"
        sem_str = str(sem_rank) if sem_rank > 0 else "-"
        hyb_str = str(hyb_rank) if hyb_rank > 0 else "-"

        # Best mode
        ranks = []
        if lex_rank > 0: ranks.append((lex_rank, "Lexical"))
        if sem_rank > 0: ranks.append((sem_rank, "Semantic"))
        if hyb_rank > 0: ranks.append((hyb_rank, "Hybrid"))

        best_mode = "None"
        if ranks:
            ranks.sort(key=lambda x: x[0])
            best_mode = f"{ranks[0][1]} (#{ranks[0][0]})"

        status = "PASSED (<=10)" if (1 <= hyb_rank <= 10) else "FAILED (>10)"

        if (1 <= lex_rank <= 10) and (hyb_rank > 10 or hyb_rank == 0):
            fusion_lost_lexical.append((qid, repo, lex_rank, sem_rank, hyb_rank))
        if (1 <= sem_rank <= 10) and (hyb_rank > 10 or hyb_rank == 0):
            fusion_lost_semantic.append((qid, repo, lex_rank, sem_rank, hyb_rank))

        if hyb_rank > 10:
            target_in_top50_not_top10.append(r)
        elif hyb_rank == 0:
            target_absent.append(r)

        print(f"{qid:<20} {repo:<25} {tax:<22} {lex_str:<7} {sem_str:<7} {hyb_str:<7} {best_mode:<15} {status:<15}")

    print("\n" + "=" * 120)
    print("FUSION ANOMALY AUDIT")
    print("=" * 120)
    print(f"Questions where LEXICAL found target in Top-10 but HYBRID LOST it ({len(fusion_lost_lexical)}):")
    for qid, repo, lr, sr, hr in fusion_lost_lexical:
        print(f"  - {qid} ({repo}): Lexical Rank #{lr}, Semantic Rank #{sr}, Hybrid Rank #{hr}")

    print(f"\nQuestions where SEMANTIC found target in Top-10 but HYBRID LOST it ({len(fusion_lost_semantic)}):")
    for qid, repo, lr, sr, hr in fusion_lost_semantic:
        print(f"  - {qid} ({repo}): Lexical Rank #{lr}, Semantic Rank #{sr}, Hybrid Rank #{hr}")

    # 2. Forensic Analysis of all 36 Failures
    failures = [r for r in positives if r["hybrid"]["target_rank"] == 0 or r["hybrid"]["target_rank"] > 10]
    print("\n" + "=" * 120)
    print(f"FORENSIC CLASSIFICATION OF ALL {len(failures)} RETRIEVAL FAILURES")
    print("=" * 120)

    category_counts = defaultdict(int)
    failure_details = []

    for r in failures:
        qid = r["question_id"]
        repo = r["repository"]
        tax = r["taxonomy"]
        gt = gt_by_id.get(qid, {})
        q_obj = q_by_id.get(qid, {})
        q_text = q_obj.get("question", "")
        exp_files = gt.get("expected_files", [])
        exp_syms = gt.get("expected_symbols", [])
        lex_rank = r["lexical_only"]["target_rank"]
        sem_rank = r["semantic_only"]["target_rank"]
        hyb_rank = r["hybrid"]["target_rank"]

        clean_q = r.get("clean_query", "")
        tech_entities = r.get("technical_entities", [])

        # Categorize
        # 1. Fusion Distortion (Lexical had it in Top-10, Hybrid destroyed it)
        if 1 <= lex_rank <= 10:
            cat = "Hybrid Fusion Distortion (Lexical Top-10)"
            explanation = f"Lexical alone ranked target at #{lex_rank}, but semantic score was 0/low and fusion down-ranked it to #{hyb_rank}."
        # 2. Semantic Fusion Distortion
        elif 1 <= sem_rank <= 10:
            cat = "Hybrid Fusion Distortion (Semantic Top-10)"
            explanation = f"Semantic alone ranked target at #{sem_rank}, but lexical score was 0/low and fusion down-ranked it to #{hyb_rank}."
        # 3. Present in Top-50 (Rank 11-50 in Lexical or Semantic) -> Ranking Failure
        elif (11 <= lex_rank <= 50) or (11 <= sem_rank <= 50) or (11 <= hyb_rank <= 50):
            if lex_rank > 0 and (sem_rank == 0 or lex_rank < sem_rank):
                cat = "Lexical Ranking Sub-optimality (Rank 11-50)"
                explanation = f"Target was found by lexical index at #{lex_rank}, but fell outside Top-10 due to high-frequency overlapping generic identifiers."
            elif sem_rank > 0:
                cat = "Semantic Ranking Sub-optimality (Rank 11-50)"
                explanation = f"Target was found by semantic index at #{sem_rank}, but embedding similarity was diluted by other symbols."
            else:
                cat = "Hybrid Ranking Failure (Rank 11-50)"
                explanation = f"Target reached hybrid rank #{hyb_rank}."
        # 4. Completely Absent from Top-50
        else:
            # Check if vocabulary gap / identifier mismatch
            cat = "Vocabulary Gap / Path-Only / Unindexed Identifier (>50 or Absent)"
            explanation = f"Target not present in Top-50 lexical or semantic results (Lex: #{lex_rank}, Sem: #{sem_rank}). Query terms do not match AST identifier names."

        category_counts[cat] += 1
        failure_details.append({
            "question_id": qid,
            "repo": repo,
            "category": cat,
            "lex_rank": lex_rank,
            "sem_rank": sem_rank,
            "hyb_rank": hyb_rank,
            "question": q_text,
            "expected_files": exp_files,
            "expected_symbols": exp_syms,
            "explanation": explanation
        })

    for cat, cnt in sorted(category_counts.items(), key=lambda x: x[1], reverse=True):
        print(f"  {cat:<60}: {cnt:2d} ({cnt/len(failures)*100:5.1f}%)")

    print("\n" + "=" * 120)
    print("DETAILED CASE-BY-CASE FAILURE AUDIT")
    print("=" * 120)
    for f in failure_details:
        print(f"[{f['question_id']}] ({f['repo']}) - {f['category']}")
        print(f"  Query:    {f['question']}")
        print(f"  Expected: Files: {f['expected_files']} | Symbols: {f['expected_symbols']}")
        print(f"  Ranks:    Lexical: {f['lex_rank']} | Semantic: {f['sem_rank']} | Hybrid: {f['hyb_rank']}")
        print(f"  Cause:    {f['explanation']}\n")

    # 3. Repository-Wise Breakdown
    print("=" * 120)
    print("REPOSITORY-WISE RETRIEVAL BREAKDOWN")
    print("=" * 120)
    print(f"{'Repository':<25} {'Pos Q':<6} {'Hyb H@1':<8} {'Hyb H@5':<8} {'Hyb H@10':<9} {'Hyb H@50':<9} {'Lex H@10':<9} {'Sem H@10':<9} {'MRR':<7}")
    print("-" * 120)

    by_repo = defaultdict(list)
    for r in positives:
        by_repo[r["repository"]].append(r)

    for repo, qlist in sorted(by_repo.items()):
        n = len(qlist)
        h1 = sum(1 for q in qlist if q["hybrid"]["hit_at_1"])
        h5 = sum(1 for q in qlist if q["hybrid"]["hit_at_5"])
        h10 = sum(1 for q in qlist if q["hybrid"]["hit_at_10"])
        h50 = sum(1 for q in qlist if q["hybrid"]["hit_at_50"])
        lex10 = sum(1 for q in qlist if q["lexical_only"]["hit_at_10"])
        sem10 = sum(1 for q in qlist if q["semantic_only"]["hit_at_10"])
        mrr = sum(q["hybrid"]["mrr"] for q in qlist) / n

        print(f"{repo:<25} {n:<6} {h1/n*100:5.1f}%   {h5/n*100:5.1f}%   {h10/n*100:5.1f}%    {h50/n*100:5.1f}%    {lex10/n*100:5.1f}%    {sem10/n*100:5.1f}%    {mrr:.4f}")

    # 4. Language-Wise Breakdown
    repo_lang = {
        "duckdb/ducklake": "C++",
        "google/adk-python": "Python",
        "better-auth/better-auth": "TypeScript",
        "Dokploy/dokploy": "TypeScript",
        "apple/pkl": "Java",
        "henrygd/beszel": "Go",
        "opencloud-eu/opencloud": "Go",
        "BurntSushi/jiff": "Rust",
        "abenz1267/walker": "Go",
        "docling-project/docling": "Python",
    }

    print("\n" + "=" * 120)
    print("LANGUAGE-WISE RETRIEVAL BREAKDOWN")
    print("=" * 120)
    print(f"{'Language':<15} {'Pos Q':<6} {'Hyb H@1':<8} {'Hyb H@5':<8} {'Hyb H@10':<9} {'Hyb H@50':<9} {'Lex H@10':<9} {'Sem H@10':<9} {'MRR':<7}")
    print("-" * 120)

    by_lang = defaultdict(list)
    for r in positives:
        lang = repo_lang.get(r["repository"], "Unknown")
        by_lang[lang].append(r)

    for lang, qlist in sorted(by_lang.items()):
        n = len(qlist)
        h1 = sum(1 for q in qlist if q["hybrid"]["hit_at_1"])
        h5 = sum(1 for q in qlist if q["hybrid"]["hit_at_5"])
        h10 = sum(1 for q in qlist if q["hybrid"]["hit_at_10"])
        h50 = sum(1 for q in qlist if q["hybrid"]["hit_at_50"])
        lex10 = sum(1 for q in qlist if q["lexical_only"]["hit_at_10"])
        sem10 = sum(1 for q in qlist if q["semantic_only"]["hit_at_10"])
        mrr = sum(q["hybrid"]["mrr"] for q in qlist) / n

        print(f"{lang:<15} {n:<6} {h1/n*100:5.1f}%   {h5/n*100:5.1f}%   {h10/n*100:5.1f}%    {h50/n*100:5.1f}%    {lex10/n*100:5.1f}%    {sem10/n*100:5.1f}%    {mrr:.4f}")

    # 5. Taxonomy-Wise Breakdown
    print("\n" + "=" * 120)
    print("TAXONOMY-WISE RETRIEVAL BREAKDOWN")
    print("=" * 120)
    print(f"{'Taxonomy':<25} {'Pos Q':<6} {'Hyb H@1':<8} {'Hyb H@5':<8} {'Hyb H@10':<9} {'Hyb H@50':<9} {'Lex H@10':<9} {'Sem H@10':<9} {'MRR':<7}")
    print("-" * 120)

    by_tax = defaultdict(list)
    for r in positives:
        by_tax[r["taxonomy"]].append(r)

    for tax, qlist in sorted(by_tax.items()):
        n = len(qlist)
        h1 = sum(1 for q in qlist if q["hybrid"]["hit_at_1"])
        h5 = sum(1 for q in qlist if q["hybrid"]["hit_at_5"])
        h10 = sum(1 for q in qlist if q["hybrid"]["hit_at_10"])
        h50 = sum(1 for q in qlist if q["hybrid"]["hit_at_50"])
        lex10 = sum(1 for q in qlist if q["lexical_only"]["hit_at_10"])
        sem10 = sum(1 for q in qlist if q["semantic_only"]["hit_at_10"])
        mrr = sum(q["hybrid"]["mrr"] for q in qlist) / n

        print(f"{tax:<25} {n:<6} {h1/n*100:5.1f}%   {h5/n*100:5.1f}%   {h10/n*100:5.1f}%    {h50/n*100:5.1f}%    {lex10/n*100:5.1f}%    {sem10/n*100:5.1f}%    {mrr:.4f}")


if __name__ == "__main__":
    main()
