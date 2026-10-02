#!/usr/bin/env python3
"""
Clone and checkout the 10 selected benchmark repositories at their exact pinned commit SHAs.
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

def main():
    os.makedirs(REPOS_DIR, exist_ok=True)
    with open(os.path.join(BENCHMARK_DIR, "repositories.json"), "r", encoding="utf-8") as f:
        repos = json.load(f)

    print(f"Fetching and checking out {len(repos)} repositories in {REPOS_DIR}...\n")

    results = []
    for r in repos:
        repo_name = r["repo_name"]
        slug = repo_name.split("/")[-1]
        target_dir = os.path.join(REPOS_DIR, slug)
        url = r["repository_url"]
        pinned_sha = r["snapshot_commit"]

        print(f"=== [{repo_name}] ===")
        print(f"  Target: {target_dir}")
        print(f"  URL:    {url}")
        print(f"  Pinned: {pinned_sha}")

        if not os.path.exists(target_dir):
            print(f"  Cloning {url}...")
            code, out, err = run_cmd(f"git clone {url} {target_dir}")
            if code != 0:
                print(f"  ERROR cloning: {err}")
                results.append({"repo": repo_name, "status": "clone_failed", "error": err})
                continue
        else:
            print(f"  Directory already exists, fetching latest...")
            run_cmd("git fetch --all", cwd=target_dir)

        # Checkout pinned commit
        print(f"  Checking out pinned SHA: {pinned_sha}...")
        code, out, err = run_cmd(f"git checkout {pinned_sha}", cwd=target_dir)
        if code != 0:
            # Maybe try fetching the specific commit if not fetched
            run_cmd(f"git fetch origin {pinned_sha}", cwd=target_dir)
            code, out, err = run_cmd(f"git checkout {pinned_sha}", cwd=target_dir)

        # Verify HEAD
        code, head_sha, err = run_cmd("git rev-parse HEAD", cwd=target_dir)
        is_exact = (head_sha.lower() == pinned_sha.lower())

        if is_exact:
            print(f"  [OK] HEAD matches pinned commit: {head_sha}")
            results.append({"repo": repo_name, "dir": target_dir, "status": "verified", "head": head_sha})
        else:
            print(f"  [FAILED] Expected {pinned_sha}, got HEAD {head_sha}")
            results.append({"repo": repo_name, "dir": target_dir, "status": "mismatch", "expected": pinned_sha, "actual": head_sha})

        print()

    print("========================================")
    print("Fetch & Verification Summary:")
    print("========================================")
    all_ok = True
    for res in results:
        status = res["status"]
        if status == "verified":
            print(f"  [OK] {res['repo']}: {res['head'][:10]}")
        else:
            print(f"  [FAIL] {res['repo']}: {status}")
            all_ok = False

    if all_ok:
        print("\nAll 10 repositories successfully fetched and verified at exact pinned commit SHAs.")
    else:
        print("\nSome repositories failed verification.")

if __name__ == "__main__":
    main()
