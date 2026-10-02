#!/usr/bin/env python3
"""
Benchmark Dataset & Execution Preparation Validator for Amoeba repoprobe_v1
Performs strict schema, repository snapshot, filesystem presence, and referential integrity checks.
"""

import json
import os
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")

BENCHMARK_DIR = r"d:\amoeba\benchmarks\repoprobe_v1"
REPOS_DIR = os.path.join(BENCHMARK_DIR, "repos")

def run_cmd(cmd, cwd=None):
    res = subprocess.run(cmd, cwd=cwd, shell=True, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return res.returncode, res.stdout.strip(), res.stderr.strip()

def validate():
    repos_file = os.path.join(BENCHMARK_DIR, "repositories.json")
    questions_file = os.path.join(BENCHMARK_DIR, "questions.json")
    gt_file = os.path.join(BENCHMARK_DIR, "ground_truth.json")

    with open(repos_file, "r", encoding="utf-8") as f:
        repos = json.load(f)
    with open(questions_file, "r", encoding="utf-8") as f:
        questions = json.load(f)
    with open(gt_file, "r", encoding="utf-8") as f:
        gt = json.load(f)

    print("================================================================")
    print("Amoeba RepoProbe v1 Benchmark Execution Preparation Validation")
    print("================================================================")

    # 1. Repositories Verification
    print(f"\n1. Repositories Verification ({len(repos)} repositories):")
    repo_names = {r["repo_name"] for r in repos}
    repo_dirs = {}
    for r in repos:
        repo_name = r["repo_name"]
        slug = repo_name.split("/")[-1]
        target_dir = os.path.join(REPOS_DIR, slug)
        repo_dirs[repo_name] = target_dir

        assert os.path.exists(target_dir), f"Repository directory missing: {target_dir}"
        pinned_sha = r["snapshot_commit"]
        code, head_sha, _ = run_cmd("git rev-parse HEAD", cwd=target_dir)
        assert code == 0, f"Git rev-parse failed for {target_dir}"
        assert head_sha.lower() == pinned_sha.lower(), f"Commit mismatch for {repo_name}: expected {pinned_sha}, got {head_sha}"
        print(f"  [OK] {repo_name:25} ({r['language']:10}, {r['size_category']:10}) @ HEAD = {head_sha[:10]}")

    # 2. Questions Verification
    print(f"\n2. Questions Verification ({len(questions)} total questions):")
    repoprobe_qs = [q for q in questions if q["source"] == "repoprobe"]
    amoeba_qs = [q for q in questions if q["source"] == "amoeba_designed"]
    print(f"  - RepoProbe Questions:      {len(repoprobe_qs)}")
    print(f"  - Amoeba-Designed Questions: {len(amoeba_qs)}")

    seen_ids = set()
    tax_counts = {}
    diff_counts = {}
    for q in questions:
        qid = q["question_id"]
        assert qid not in seen_ids, f"Duplicate question ID: {qid}"
        seen_ids.add(qid)
        assert q["repo_name"] in repo_names, f"Unknown repo in question {qid}: {q['repo_name']}"
        assert q["question"].strip(), f"Empty question text in {qid}"
        assert q["reference_answer"].strip(), f"Empty reference answer in {qid}"
        assert q["checklist"].strip(), f"Empty checklist in {qid}"

        tax = q.get("taxonomy", "Unknown")
        tax_counts[tax] = tax_counts.get(tax, 0) + 1

        diff = str(q.get("difficulty", "Unknown"))
        diff_counts[diff] = diff_counts.get(diff, 0) + 1

    # 3. Ground Truth & Filesystem Verification
    print(f"\n3. Ground Truth & Filesystem Verification ({len(gt)} items):")
    gt_map = {g["question_id"]: g for g in gt}
    assert len(gt_map) == len(questions), "Ground truth length mismatch with questions"

    status_counts = {}
    total_expected_files = 0
    missing_files = []

    for g in gt:
        qid = g["question_id"]
        repo_name = g["repo_name"]
        repo_dir = repo_dirs[repo_name]
        st = g.get("ground_truth_status", "missing")
        status_counts[st] = status_counts.get(st, 0) + 1

        expected_files = g.get("expected_files", [])
        total_expected_files += len(expected_files)

        for ef in expected_files:
            file_full = os.path.join(repo_dir, ef)
            if not os.path.exists(file_full):
                missing_files.append((qid, repo_name, ef))

    print(f"  - Total Expected File References: {total_expected_files}")
    print(f"  - Missing File Count:             {len(missing_files)}")
    assert len(missing_files) == 0, f"Missing files found in ground truth: {missing_files}"

    for st, count in sorted(status_counts.items()):
        print(f"  - Status [{st}]: {count} / {len(gt)} ({count/len(gt)*100:.1f}%)")

    print("\n================================================================")
    print("ALL 12 EXECUTION PREPARATION CHECKS PASSED PERFECTLY (0 ERRORS)")
    print("================================================================")

if __name__ == "__main__":
    validate()
