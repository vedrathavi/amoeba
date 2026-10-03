#!/usr/bin/env python3
"""
Amoeba Phase 8.4 — Hybrid Fusion Recalibration Experiment Runner

Orchestrates amoeba_test_fusion.exe across all 10 benchmark repositories,
collects per-question TSV rows, and aggregates Hit@1/5/10/20/50 and MRR
for every fusion strategy in a single comparative table.

Usage:
    python run_fusion_experiment.py [--output <results_dir>]

Output:
    results/fusion/fusion_ranks_master.tsv  — raw per-question ranks
    results/fusion/fusion_summary.txt       — aggregated metrics table
"""

import json
import os
import subprocess
import sys
import time
from collections import defaultdict

sys.stdout.reconfigure(encoding="utf-8")

BENCHMARK_DIR = r"d:\amoeba\benchmarks\repoprobe_v1"
REPOS_DIR     = os.path.join(BENCHMARK_DIR, "repos")
RESULTS_DIR   = os.path.join(BENCHMARK_DIR, "results", "fusion")
EXE_PATH      = r"d:\amoeba\build\amoeba_test_fusion.exe"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def load_questions_and_gt():
    with open(os.path.join(BENCHMARK_DIR, "questions.json"), "r", encoding="utf-8") as f:
        questions = json.load(f)
    with open(os.path.join(BENCHMARK_DIR, "ground_truth.json"), "r", encoding="utf-8") as f:
        gt_list = json.load(f)
    gt_by_id = {g["question_id"]: g for g in gt_list}
    return questions, gt_by_id


def build_repo_tsv(q_list, tsv_path):
    """Write a per-repo TSV for amoeba_test_fusion."""
    with open(tsv_path, "w", encoding="utf-8") as tf:
        for q in q_list:
            files_str = "|".join(q["expected_files"])
            syms_str  = "|".join(q["expected_symbols"])
            rels_str  = "|".join(q.get("expected_relationships", []))
            conc_str  = "|".join(q.get("expected_concepts", []))
            tf.write(
                f"{q['question_id']}\t{q['repo_name']}\t{q.get('source','repoprobe')}\t"
                f"{q.get('taxonomy','Implementation Details')}\t"
                f"{str(q.get('difficulty','Medium'))}\t"
                f"{q['question']}\t{files_str}\t{syms_str}\t{rels_str}\t{conc_str}\n"
            )


# ---------------------------------------------------------------------------
# Aggregation
# ---------------------------------------------------------------------------

def metrics_from_ranks(ranks):
    """Compute Hit@k and MRR from a list of ranks (0 = miss, -1 = negative)."""
    positive = [r for r in ranks if r != -1]
    n = len(positive)
    if n == 0:
        return dict(h1=0, h5=0, h10=0, h20=0, h50=0, mrr=0.0, n=0)
    h1  = sum(1 for r in positive if r == 1)
    h5  = sum(1 for r in positive if 1 <= r <= 5)
    h10 = sum(1 for r in positive if 1 <= r <= 10)
    h20 = sum(1 for r in positive if 1 <= r <= 20)
    h50 = sum(1 for r in positive if 1 <= r <= 50)
    mrr = sum(1.0 / r for r in positive if r > 0) / n
    return dict(h1=h1, h5=h5, h10=h10, h20=h20, h50=h50, mrr=mrr, n=n)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    os.makedirs(RESULTS_DIR, exist_ok=True)

    print("=" * 80)
    print("Amoeba Phase 8.4 — Hybrid Fusion Recalibration Experiment")
    print("=" * 80)
    print(f"Binary:  {EXE_PATH}")
    print(f"Results: {RESULTS_DIR}\n")

    if not os.path.exists(EXE_PATH):
        print(f"ERROR: Binary not found: {EXE_PATH}")
        print("Build with:  cmake --build d:\\amoeba\\build --target amoeba_test_fusion")
        sys.exit(1)

    questions, gt_by_id = load_questions_and_gt()

    # Group by repo
    by_repo = {}
    for q in questions:
        qid = q["question_id"]
        gt  = gt_by_id.get(qid, {})
        item = {
            "question_id":            qid,
            "repo_name":              q["repo_name"],
            "source":                 q.get("source", "repoprobe"),
            "taxonomy":               q.get("taxonomy", "Implementation Details"),
            "difficulty":             str(q.get("difficulty", "Medium")),
            "question":               q["question"].replace("\r", "").replace("\n", " ").strip(),
            "expected_files":         gt.get("expected_files", []),
            "expected_symbols":       gt.get("expected_symbols", []),
            "expected_relationships": gt.get("expected_relationships", []),
            "expected_concepts":      gt.get("expected_concepts", []),
        }
        by_repo.setdefault(q["repo_name"], []).append(item)

    # Collect all per-question rows from all repos
    master_rows  = []     # list of dicts: {question_id, repo_name, is_negative, strategy->rank}
    strategies   = None   # column names (parsed from first TSV header)
    total_start  = time.time()

    for idx, (repo_name, q_list) in enumerate(by_repo.items(), 1):
        slug     = repo_name.split("/")[-1]
        repo_dir = os.path.join(REPOS_DIR, slug)
        tsv_in   = os.path.join(RESULTS_DIR, f"_tmp_{slug}_queries.tsv")
        tsv_out  = os.path.join(RESULTS_DIR, f"_tmp_{slug}_ranks.tsv")

        print(f"[{idx:2d}/10] {repo_name}  ({len(q_list)} queries) ...", end="", flush=True)

        build_repo_tsv(q_list, tsv_in)
        t0  = time.time()
        res = subprocess.run(
            [EXE_PATH, repo_dir, tsv_in, tsv_out],
            capture_output=True, text=True
        )
        elapsed = time.time() - t0

        if res.returncode != 0:
            print(f"\n  ERROR on {slug}:\n{res.stderr}")
            continue

        print(f"  {elapsed:.1f}s")

        # Parse output TSV
        if not os.path.exists(tsv_out):
            print(f"  WARNING: output file missing for {slug}")
            continue

        with open(tsv_out, "r", encoding="utf-8") as f:
            lines = f.readlines()

        if not lines:
            continue

        header = lines[0].rstrip("\n").split("\t")
        # header: question_id  repo_name  is_negative  <strategy cols...>
        strat_cols = header[3:]
        if strategies is None:
            strategies = strat_cols
        elif strategies != strat_cols:
            print(f"  WARNING: strategy column mismatch for {slug} — skipping repo")
            continue

        for line in lines[1:]:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 3 + len(strat_cols):
                continue
            row = {
                "question_id": parts[0],
                "repo_name":   parts[1],
                "is_negative": parts[2] == "1",
            }
            for i, col in enumerate(strat_cols):
                row[col] = int(parts[3 + i])
            master_rows.append(row)

    total_elapsed = time.time() - total_start
    print(f"\nAll repos completed in {total_elapsed:.1f}s\n")

    if not master_rows or strategies is None:
        print("ERROR: No results collected.")
        sys.exit(1)

    # Save master TSV
    master_tsv = os.path.join(RESULTS_DIR, "fusion_ranks_master.tsv")
    with open(master_tsv, "w", encoding="utf-8") as f:
        f.write("question_id\trepo_name\tis_negative\t" + "\t".join(strategies) + "\n")
        for row in master_rows:
            vals = "\t".join(str(row[s]) for s in strategies)
            f.write(f"{row['question_id']}\t{row['repo_name']}\t{1 if row['is_negative'] else 0}\t{vals}\n")
    print(f"Master ranks saved: {master_tsv}\n")

    # ---------------------------------------------------------------------------
    # Aggregation
    # ---------------------------------------------------------------------------
    positives = [r for r in master_rows if not r["is_negative"]]
    negatives = [r for r in master_rows if r["is_negative"]]
    n_pos = len(positives)
    n_neg = len(negatives)

    print("=" * 80)
    print(f"FUSION RECALIBRATION RESULTS  ({n_pos} positive, {n_neg} negative)")
    print("=" * 80)
    print(f"{'Strategy':<18} {'H@1':>5} {'H@5':>5} {'H@10':>6} {'H@20':>6} {'H@50':>6}  {'MRR':>7}")
    print("-" * 65)

    best_h10 = -1
    summary_lines = []
    for strat in strategies:
        ranks  = [r[strat] for r in positives]
        m      = metrics_from_ranks(ranks)
        h10pct = m["h10"] / n_pos * 100 if n_pos else 0

        line = (
            f"{strat:<18} "
            f"{m['h1']:>2}/{n_pos} "
            f"{m['h5']:>2}/{n_pos} "
            f"{m['h10']:>3}/{n_pos} "
            f"{m['h20']:>3}/{n_pos} "
            f"{m['h50']:>3}/{n_pos}  "
            f"{m['mrr']:.4f}"
        )
        marker = "  <-- BEST H@10" if h10pct > best_h10 else ""
        if h10pct > best_h10:
            best_h10 = h10pct

        print(line + marker)
        summary_lines.append(line)

    # Negative safety check
    print("\n" + "=" * 80)
    print("NEGATIVE SAFETY CHECK  (all ranks should be -1)")
    print("=" * 80)
    all_safe = True
    for strat in strategies:
        neg_ranks = [r[strat] for r in negatives]
        violations = [r for r in neg_ranks if r != -1]
        status = "OK" if not violations else f"VIOLATION ({len(violations)} non-sentinel)"
        if violations:
            all_safe = False
        print(f"  {strat:<18}  {status}")

    print()
    if all_safe:
        print("All negative questions returned -1 sentinel. Safety: PASS.")
    else:
        print("WARNING: Some negative questions returned non-sentinel ranks!")

    # Save summary
    summary_file = os.path.join(RESULTS_DIR, "fusion_summary.txt")
    with open(summary_file, "w", encoding="utf-8") as sf:
        sf.write(f"Phase 8.4 Fusion Recalibration — {n_pos} positive, {n_neg} negative\n\n")
        sf.write(f"{'Strategy':<18} {'H@1':>5} {'H@5':>5} {'H@10':>6} {'H@20':>6} {'H@50':>6}  {'MRR':>7}\n")
        sf.write("-" * 65 + "\n")
        for line in summary_lines:
            sf.write(line + "\n")
        sf.write(f"\nAll-safe: {all_safe}\n")
    print(f"\nSummary saved: {summary_file}")


if __name__ == "__main__":
    main()
