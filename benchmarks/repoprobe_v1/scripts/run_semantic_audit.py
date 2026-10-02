#!/usr/bin/env python3
"""
Amoeba Semantic Retrieval Audit Runner
Executes amoeba_semantic_audit.exe across all 10 repositories and 70 questions.
Produces comprehensive metrics for:
- Lexical recall Top-10
- Semantic recall at Top-1, 5, 10, 20, 50, 100
- Hybrid recall Top-10
- Score margin distributions
- Semantic document text comparisons for failures
- Taxonomy, source, and difficulty breakdowns
- Latency decomposition per repo
"""

import datetime
import json
import os
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8")

BENCHMARK_DIR = r"d:\amoeba\benchmarks\repoprobe_v1"
REPOS_DIR = os.path.join(BENCHMARK_DIR, "repos")
RESULTS_DIR = os.path.join(BENCHMARK_DIR, "results", "semantic_audit")
EXE_PATH = r"d:\amoeba\build\amoeba_semantic_audit.exe"

def main():
    os.makedirs(RESULTS_DIR, exist_ok=True)
    print("=================================================================")
    print("Amoeba Semantic Retrieval Forensic Audit")
    print("=================================================================\n")

    with open(os.path.join(BENCHMARK_DIR, "questions.json"), "r", encoding="utf-8") as f:
        questions = json.load(f)
    with open(os.path.join(BENCHMARK_DIR, "ground_truth.json"), "r", encoding="utf-8") as f:
        ground_truth = json.load(f)

    gt_by_id = {g["question_id"]: g for g in ground_truth}

    by_repo = {}
    for q in questions:
        qid = q["question_id"]
        gt = gt_by_id.get(qid, {})
        q_item = {
            "question_id": qid,
            "repo_name": q["repo_name"],
            "source": q.get("source", "repoprobe"),
            "taxonomy": q.get("taxonomy", "Implementation Details"),
            "difficulty": str(q.get("difficulty", "Medium")),
            "question": q["question"].replace("\r", "").replace("\n", " ").strip(),
            "expected_files": gt.get("expected_files", []),
            "expected_symbols": gt.get("expected_symbols", []),
            "expected_relationships": gt.get("expected_relationships", []),
            "expected_concepts": gt.get("expected_concepts", []),
        }
        by_repo.setdefault(q["repo_name"], []).append(q_item)

    all_results = []
    repo_latencies = {}

    start_total_time = time.time()

    for repo_idx, (repo_name, q_list) in enumerate(by_repo.items(), 1):
        slug = repo_name.split("/")[-1]
        repo_dir = os.path.join(REPOS_DIR, slug)
        temp_tsv = os.path.join(RESULTS_DIR, f"temp_{slug}_queries.tsv")
        temp_out = os.path.join(RESULTS_DIR, f"temp_{slug}_audit.json")

        print(f"[{repo_idx}/10] Auditing Repository: {repo_name} ({len(q_list)} questions)...")

        with open(temp_tsv, "w", encoding="utf-8") as tf:
            for q in q_list:
                files_str = "|".join(q["expected_files"])
                syms_str = "|".join(q["expected_symbols"])
                rels_str = "|".join(q["expected_relationships"])
                concepts_str = "|".join(q["expected_concepts"])
                row = f"{q['question_id']}\t{q['repo_name']}\t{q['source']}\t{q['taxonomy']}\t{q['difficulty']}\t{q['question']}\t{files_str}\t{syms_str}\t{rels_str}\t{concepts_str}\n"
                tf.write(row)

        cmd = [EXE_PATH, repo_dir, temp_tsv, temp_out]
        t0 = time.time()
        res = subprocess.run(cmd, cwd=r"d:\amoeba", capture_output=True, text=True, encoding="utf-8", errors="replace")
        t_elapsed = time.time() - t0

        if res.returncode != 0:
            print(f"  ERROR auditing {repo_name}: {res.stderr}")
            continue

        print(f"  Completed {len(q_list)} questions in {t_elapsed:.2f} s.")

        if os.path.exists(temp_out):
            with open(temp_out, "r", encoding="utf-8") as rf:
                repo_results = json.load(rf)
                all_results.extend(repo_results)
                
                # Compute repo latency breakdown
                avg_embed_ms = sum(r["embed_ms"] for r in repo_results) / len(repo_results) if repo_results else 0.0
                avg_sem_ms = sum(r["total_sem_ms"] for r in repo_results) / len(repo_results) if repo_results else 0.0
                repo_latencies[repo_name] = {
                    "question_count": len(repo_results),
                    "avg_embed_ms": avg_embed_ms,
                    "avg_total_sem_ms": avg_sem_ms,
                    "avg_scan_sort_ms": avg_sem_ms - avg_embed_ms
                }

            try:
                os.remove(temp_tsv)
                os.remove(temp_out)
            except Exception:
                pass

    total_duration = time.time() - start_total_time
    print(f"\nAudit completed in {total_duration:.2f} seconds.\n")

    # Save full audit results
    audit_file = os.path.join(RESULTS_DIR, "semantic_audit_full.json")
    with open(audit_file, "w", encoding="utf-8") as f:
        json.dump(all_results, f, indent=2)

    # Positive questions only (66 total)
    pos_results = [r for r in all_results if r["question_id"] not in ["walker-1", "docling-4", "dokploy-4", "beszel-4"]]
    num_pos = len(pos_results)

    # A. Semantic Recall at depths
    sem_hit_1 = sum(1 for r in pos_results if r["sem_hit_1"])
    sem_hit_5 = sum(1 for r in pos_results if r["sem_hit_5"])
    sem_hit_10 = sum(1 for r in pos_results if r["sem_hit_10"])
    sem_hit_20 = sum(1 for r in pos_results if r["sem_hit_20"])
    sem_hit_50 = sum(1 for r in pos_results if r["sem_hit_50"])
    sem_hit_100 = sum(1 for r in pos_results if r["sem_hit_100"])

    lex_hit_10 = sum(1 for r in pos_results if r["lex_hit_10"])
    hyb_hit_10 = sum(1 for r in pos_results if r["hyb_hit_10"])

    print("=================================================================")
    print("A. SEMANTIC RECALL BY CANDIDATE DEPTH (K)")
    print("=================================================================")
    print(f"| K   | Semantic Hit Count | Recall Rate (%) |")
    print(f"|-----|--------------------|----------------:|")
    print(f"| 1   | {sem_hit_1:2d} / {num_pos}          | {100.0*sem_hit_1/num_pos:5.1f}%          |")
    print(f"| 5   | {sem_hit_5:2d} / {num_pos}          | {100.0*sem_hit_5/num_pos:5.1f}%          |")
    print(f"| 10  | {sem_hit_10:2d} / {num_pos}          | {100.0*sem_hit_10/num_pos:5.1f}%          |")
    print(f"| 20  | {sem_hit_20:2d} / {num_pos}          | {100.0*sem_hit_20/num_pos:5.1f}%          |")
    print(f"| 50  | {sem_hit_50:2d} / {num_pos}          | {100.0*sem_hit_50/num_pos:5.1f}%          |")
    print(f"| 100 | {sem_hit_100:2d} / {num_pos}          | {100.0*sem_hit_100/num_pos:5.1f}%          |")

    # B. Retrieval Separation Matrix
    lex_only_succ = sum(1 for r in pos_results if r["lex_hit_10"] and not r["sem_hit_10"])
    sem_only_succ = sum(1 for r in pos_results if r["sem_hit_10"] and not r["lex_hit_10"])
    both_succ = sum(1 for r in pos_results if r["lex_hit_10"] and r["sem_hit_10"])
    both_fail = sum(1 for r in pos_results if not r["lex_hit_10"] and not r["sem_hit_10"])
    sem_succ_hyb_loss = sum(1 for r in pos_results if r["sem_hit_10"] and not r["hyb_hit_10"])

    print("\n=================================================================")
    print("B. RETRIEVAL SEPARATION MATRIX (Top-10)")
    print("=================================================================")
    print(f"| Case                                    | Count | Percentage |")
    print(f"|-----------------------------------------|------:|-----------:|")
    print(f"| Lexical succeeds / Semantic fails       | {lex_only_succ:5d} | {100.0*lex_only_succ/num_pos:9.1f}% |")
    print(f"| Semantic succeeds / Lexical fails       | {sem_only_succ:5d} | {100.0*sem_only_succ/num_pos:9.1f}% |")
    print(f"| Both succeed                            | {both_succ:5d} | {100.0*both_succ/num_pos:9.1f}% |")
    print(f"| Both fail                               | {both_fail:5d} | {100.0*both_fail/num_pos:9.1f}% |")
    print(f"| Semantic succeeds / Hybrid loses target | {sem_succ_hyb_loss:5d} | {100.0*sem_succ_hyb_loss/num_pos:9.1f}% |")

    # C. Taxonomy Breakdown
    by_tax = {}
    for r in pos_results:
        tax = r["taxonomy"]
        by_tax.setdefault(tax, []).append(r)

    print("\n=================================================================")
    print("C. SEMANTIC RECALL BY TAXONOMY")
    print("=================================================================")
    for tax, r_list in by_tax.items():
        n = len(r_list)
        s1 = sum(1 for r in r_list if r["sem_hit_1"])
        s5 = sum(1 for r in r_list if r["sem_hit_5"])
        s10 = sum(1 for r in r_list if r["sem_hit_10"])
        l10 = sum(1 for r in r_list if r["lex_hit_10"])
        h10 = sum(1 for r in r_list if r["hyb_hit_10"])
        print(f"[{tax}] (N={n})")
        print(f"  Semantic Hit@1:  {s1}/{n} ({100.0*s1/n:.1f}%) | Hit@5: {s5}/{n} ({100.0*s5/n:.1f}%) | Hit@10: {s10}/{n} ({100.0*s10/n:.1f}%)")
        print(f"  Lexical Hit@10:  {l10}/{n} ({100.0*l10/n:.1f}%) | Hybrid Hit@10: {h10}/{n} ({100.0*h10/n:.1f}%)\n")

    # Breakdown by Source
    by_source = {}
    for r in pos_results:
        src = r["source"]
        by_source.setdefault(src, []).append(r)

    print("=================================================================")
    print("D. SEMANTIC RECALL BY SOURCE")
    print("=================================================================")
    for src, r_list in by_source.items():
        n = len(r_list)
        s10 = sum(1 for r in r_list if r["sem_hit_10"])
        l10 = sum(1 for r in r_list if r["lex_hit_10"])
        h10 = sum(1 for r in r_list if r["hyb_hit_10"])
        print(f"[{src}] (N={n}) -> Semantic Hit@10: {s10}/{n} ({100.0*s10/n:.1f}%) | Lexical Hit@10: {l10}/{n} ({100.0*l10/n:.1f}%) | Hybrid Hit@10: {h10}/{n} ({100.0*h10/n:.1f}%)")

    # Latency Breakdown
    print("\n=================================================================")
    print("E. LATENCY DECOMPOSITION PER REPOSITORY")
    print("=================================================================")
    print(f"| Repository               | Query Embed (ms) | Cosine Scan + Sort (ms) | Total Sem Latency (ms) |")
    print(f"|--------------------------|-----------------:|------------------------:|-----------------------:|")
    for rname, lat in repo_latencies.items():
        print(f"| {rname:24s} | {lat['avg_embed_ms']:16.2f} | {lat['avg_scan_sort_ms']:23.2f} | {lat['avg_total_sem_ms']:22.2f} |")

    # Failures with document inspection
    print("\n=================================================================")
    print("F. REPRESENTATIVE SEMANTIC FAILURES")
    print("=================================================================")
    failed_repoprobe = [r for r in pos_results if r["source"] == "repoprobe" and not r["sem_hit_10"]]
    for idx, f_item in enumerate(failed_repoprobe[:6], 1):
        print(f"--- FAILURE #{idx}: {f_item['question_id']} ({f_item['repo_name']}) ---")
        print(f"QUESTION:        {f_item['question_text']}")
        print(f"SEMANTIC RANK:   {f_item['sem_target_rank']} (Score: {f_item['sem_target_score']:.4f}, Top1: {f_item['top1_sem_score']:.4f}, Margin: {f_item['score_margin']:.4f})")
        print(f"LEXICAL RANK:    {f_item['lex_target_rank']}")
        print(f"\nEXPECTED UNIT TEXT:\n{f_item['expected_unit_text']}")
        print(f"\nTOP-1 RETRIEVED SEMANTIC TEXT:\n{f_item['top1_sem_text']}")
        print("-----------------------------------------------------------------\n")

if __name__ == "__main__":
    main()
