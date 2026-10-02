#!/usr/bin/env python3
"""
Inspect each question and verify its ground-truth files and symbols against the pinned repository snapshots.
"""

import json
import os
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")

BENCHMARK_DIR = r"d:\amoeba\benchmarks\repoprobe_v1"
REPOS_DIR = os.path.join(BENCHMARK_DIR, "repos")

def search_repo_for_terms(repo_slug, terms):
    repo_path = os.path.join(REPOS_DIR, repo_slug)
    matches = {}
    for root, dirs, files in os.walk(repo_path):
        if ".git" in dirs: dirs.remove(".git")
        if "node_modules" in dirs: dirs.remove("node_modules")
        if "target" in dirs: dirs.remove("target")
        if ".venv" in dirs: dirs.remove(".venv")
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            if ext not in [".cpp", ".hpp", ".h", ".c", ".cc", ".py", ".ts", ".tsx", ".js", ".jsx", ".java", ".go", ".rs", ".md", ".json", ".prisma", ".toml"]:
                continue
            full_path = os.path.join(root, f)
            rel_path = os.path.relpath(full_path, repo_path).replace("\\", "/")
            try:
                with open(full_path, "r", encoding="utf-8", errors="ignore") as file_obj:
                    content = file_obj.read()
                    for t in terms:
                        if t.lower() in content.lower():
                            matches.setdefault(t, []).append(rel_path)
            except Exception:
                pass
    return matches

def main():
    with open(os.path.join(BENCHMARK_DIR, "questions.json"), "r", encoding="utf-8") as f:
        questions = json.load(f)

    for q in questions:
        qid = q["question_id"]
        repo_name = q["repo_name"]
        repo_slug = repo_name.split("/")[-1]
        print("==================================================")
        print(f"[{qid}] Repo: {repo_name}")
        print("Q:", q["question"].split("\n")[0][:90])
        print("Ans:", q["reference_answer"].split("\n")[0][:90])

if __name__ == "__main__":
    main()
