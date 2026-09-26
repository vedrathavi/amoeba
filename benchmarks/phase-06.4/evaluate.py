#!/usr/bin/env python3
"""
Phase 6.4 Benchmark — Primary Retrieval Pipeline vs Raw Element Retrieval
==========================================================================

This script evaluates the Phase 6.4 PrimaryRetrievalPipeline against the
Phase 6.2 raw CodeElement retrieval on the frozen calendar holdout corpus.

It uses the Amoeba Python bindings (amoeba_cli) to perform retrieval and
compares precision, recall, and MRR metrics.

Since C++ Python bindings are not yet available, this script performs a
REPRESENTATION-LEVEL evaluation that mirrors the C++ pipeline's behavior
in Python, using the same retrieval logic and the same corpus.

This is the same methodology used for the Phase 6.3 holdout evaluation.

Run from the amoeba/ directory:
    python benchmarks/phase-06.4/evaluate.py

Output:
    benchmarks/phase-06.4/data/results.json
    benchmarks/phase-06.4/data/per_query_results.csv
"""

import json
import csv
import time
import sys
import os
from pathlib import Path
from dataclasses import dataclass, asdict
from typing import Optional

# ── Configuration ─────────────────────────────────────────────────────────────

HOLDOUT_DIR  = Path("demo_test_projects/calendar")
OUTPUT_DIR   = Path("benchmarks/phase-06.4/data")
RESULTS_JSON = OUTPUT_DIR / "results.json"
PER_QUERY_CSV = OUTPUT_DIR / "per_query_results.csv"

# Frozen evaluation queries (unchanged from Phase 6.2 holdout).
# These were fixed BEFORE Phase 6.2 tuning and must not be changed.
EVAL_QUERIES = [
    {
        "id": "Q1",
        "query": "calendar navigation",
        "relevant_symbols": ["CalendarHeader", "NavigationControls", "useCalendarNavigation"],
        "category": "Component",
        "description": "Find the navigation control for the calendar"
    },
    {
        "id": "Q2",
        "query": "event rendering",
        "relevant_symbols": ["EventCard", "EventList", "CalendarEvent", "renderEvent"],
        "category": "Component",
        "description": "Find where events are rendered"
    },
    {
        "id": "Q3",
        "query": "date formatting utility",
        "relevant_symbols": ["formatDate", "DateFormatter", "formatEventDate"],
        "category": "Utility",
        "description": "Find date formatting functions"
    },
    {
        "id": "Q4",
        "query": "authentication login",
        "relevant_symbols": ["LoginForm", "AuthService", "authenticate", "useAuth"],
        "category": "Auth",
        "description": "Find login/authentication code"
    },
    {
        "id": "Q5",
        "query": "where is user profile displayed",
        "relevant_symbols": ["UserProfile", "ProfileCard", "Avatar", "UserAvatar"],
        "category": "Conceptual",
        "description": "Natural language conceptual query"
    },
    {
        "id": "Q6",
        "query": "API data fetching",
        "relevant_symbols": ["fetchEvents", "useEvents", "EventsApi", "getCalendarData"],
        "category": "API",
        "description": "Find data fetching logic"
    },
    {
        "id": "Q7",
        "query": "modal dialog",
        "relevant_symbols": ["Modal", "EventModal", "ConfirmDialog", "useModal"],
        "category": "Component",
        "description": "Find modal/dialog components"
    },
    {
        "id": "Q8",
        "query": "theme color styling",
        "relevant_symbols": ["ThemeProvider", "useTheme", "ColorPicker", "ThemeContext"],
        "category": "Style",
        "description": "Find theming/styling code"
    },
    {
        "id": "Q9",
        "query": "route navigation page",
        "relevant_symbols": ["Router", "AppRouter", "routes", "useRouter"],
        "category": "Route",
        "description": "Find routing configuration"
    },
    {
        "id": "Q10",
        "query": "state management",
        "relevant_symbols": ["useCalendarStore", "CalendarContext", "CalendarProvider", "useStore"],
        "category": "State",
        "description": "Find state management code"
    },
]

# ── Primary Kind Classification ──────────────────────────────────────────────

PRIMARY_KINDS = {
    "Class", "Struct", "Interface", "Function",
    "Method", "Component", "Hook", "Route"
}

SUPPORTING_KINDS = {
    "Call", "Attribute", "JSXElement", "JSXComponent",
    "UtilityClass", "Include", "Property", "Selector", "Unknown"
}

def is_primary(kind: str) -> bool:
    return kind in PRIMARY_KINDS

# ── Corpus Loading ────────────────────────────────────────────────────────────

def load_corpus(holdout_dir: Path) -> list[dict]:
    """
    Load all code elements from the calendar holdout corpus.
    Mirrors the InvertedIndex + SourceParser behavior.
    """
    try:
        sys.path.insert(0, str(Path.cwd()))
        from benchmarks.scripts.evaluate_phase_06_3 import load_corpus as _load
        return _load(holdout_dir)
    except ImportError:
        pass

    # Fallback: use amoeba_cli if available
    try:
        import subprocess
        result = subprocess.run(
            ["./build/debug/amoeba_cli.exe", "--index", str(holdout_dir),
             "--dump-elements", "--format=json"],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            return json.loads(result.stdout)
    except Exception:
        pass

    print("ERROR: Cannot load corpus. Ensure amoeba_cli or benchmarks/scripts/evaluate_phase_06_3.py is available.")
    sys.exit(1)


def build_retrieval_units(elements: list[dict]) -> list[dict]:
    """
    Mirror the Phase 6.4 RetrievalUnit construction.
    Returns primary units with supporting evidence attached.
    """
    # Group by file
    by_file: dict[str, list[dict]] = {}
    for el in elements:
        fp = el.get("file_path", "")
        by_file.setdefault(fp, []).append(el)

    units = []
    for fp, file_elements in by_file.items():
        primaries = [e for e in file_elements if is_primary(e.get("kind", "Unknown"))]
        supporting = [e for e in file_elements if not is_primary(e.get("kind", "Unknown"))]

        if not primaries:
            # Synthetic file module unit
            synthetic = {
                "kind": "Route",
                "name": Path(fp).name,
                "file_path": fp,
                "detail": "File Module",
                "parent_context": "",
                "supporting": file_elements,
            }
            units.append(synthetic)
            continue

        for p in primaries:
            unit = dict(p)
            unit["supporting"] = []
            # Attribute supporting elements
            for s in supporting:
                parent_ctx = s.get("parent_context", "")
                if parent_ctx and parent_ctx == p.get("name", ""):
                    unit["supporting"].append(s)
                    continue
                # Source range containment (simplified)
                s_start = s.get("start_line", 0)
                s_end = s.get("end_line", s_start)
                p_start = p.get("start_line", 0)
                p_end = p.get("end_line", p_start)
                if p_start > 0 and p_end >= p_start:
                    if s_start >= p_start and s_end <= p_end:
                        unit["supporting"].append(s)
            units.append(unit)

    return units


# ── Metrics ───────────────────────────────────────────────────────────────────

def precision_at_k(results: list[str], relevant: set[str], k: int) -> float:
    top_k = results[:k]
    hits = sum(1 for r in top_k if r in relevant)
    return hits / k if k > 0 else 0.0


def recall_at_k(results: list[str], relevant: set[str], k: int) -> float:
    top_k = results[:k]
    hits = sum(1 for r in top_k if r in relevant)
    return hits / len(relevant) if relevant else 0.0


def mrr(results: list[str], relevant: set[str]) -> float:
    for i, r in enumerate(results, 1):
        if r in relevant:
            return 1.0 / i
    return 0.0


def ndcg_at_k(results: list[str], relevant: set[str], k: int) -> float:
    import math
    dcg = 0.0
    for i, r in enumerate(results[:k], 1):
        if r in relevant:
            dcg += 1.0 / math.log2(i + 1)
    # Ideal DCG: all relevant at top
    idcg = sum(1.0 / math.log2(i + 1) for i in range(1, min(len(relevant), k) + 1))
    return dcg / idcg if idcg > 0 else 0.0


# ── Lexical Retrieval (simplified tokenization mirror) ────────────────────────

def tokenize(text: str) -> list[str]:
    import re
    # Split on whitespace, underscores, camelCase boundaries
    text = text.lower()
    tokens = re.findall(r"[a-z]+", text)
    return tokens


def lexical_score(element: dict, query_tokens: list[str]) -> float:
    name = element.get("name", "")
    kind = element.get("kind", "Unknown")
    name_tokens = set(tokenize(name))
    detail = element.get("detail", "")
    detail_tokens = set(tokenize(detail))

    score = 0.0
    matched = sum(1 for t in query_tokens if t in name_tokens)
    score += matched * 25.0

    detail_matched = sum(1 for t in query_tokens if t in detail_tokens)
    score += detail_matched * 5.0

    if is_primary(kind):
        score += 10.0  # declaration boost

    return score


# ── Evaluate ─────────────────────────────────────────────────────────────────

@dataclass
class QueryResult:
    query_id: str
    query: str
    category: str
    # Before: raw elements
    raw_p1: float
    raw_p3: float
    raw_p5: float
    raw_recall5: float
    raw_recall10: float
    raw_mrr: float
    raw_ndcg5: float
    raw_ndcg10: float
    # After: primary units
    unit_p1: float
    unit_p3: float
    unit_p5: float
    unit_recall5: float
    unit_recall10: float
    unit_mrr: float
    unit_ndcg5: float
    unit_ndcg10: float
    # Counts
    raw_candidate_count: int
    unit_candidate_count: int
    supporting_count: int
    # Latency
    raw_ms: float
    unit_ms: float


def evaluate(elements: list[dict], units: list[dict]) -> tuple[list[QueryResult], dict]:
    results = []

    for q in EVAL_QUERIES:
        relevant = set(q["relevant_symbols"])
        query_tokens = tokenize(q["query"])

        # ── Raw element retrieval (Before) ─────────────────────────────────
        t0 = time.monotonic()
        raw_scored = []
        for el in elements:
            s = lexical_score(el, query_tokens)
            if s > 0:
                raw_scored.append((s, el.get("name", "")))
        raw_scored.sort(key=lambda x: -x[0])
        raw_names = [r[1] for r in raw_scored]
        raw_ms = (time.monotonic() - t0) * 1000.0

        # ── Primary unit retrieval (After) ─────────────────────────────────
        t0 = time.monotonic()
        unit_scored = []
        for u in units:
            s = lexical_score(u, query_tokens)
            # Boost from supporting evidence names
            for ev in u.get("supporting", []):
                ev_tokens = set(tokenize(ev.get("name", "")))
                ev_match = sum(1 for t in query_tokens if t in ev_tokens)
                s += ev_match * 5.0  # evidence contribution (secondary)
            if s > 0:
                unit_scored.append((s, u.get("name", "")))
        unit_scored.sort(key=lambda x: -x[0])
        unit_names = [r[1] for r in unit_scored]
        unit_ms = (time.monotonic() - t0) * 1000.0

        supporting_total = sum(len(u.get("supporting", [])) for u in units)

        results.append(QueryResult(
            query_id=q["id"],
            query=q["query"],
            category=q["category"],
            raw_p1=precision_at_k(raw_names, relevant, 1),
            raw_p3=precision_at_k(raw_names, relevant, 3),
            raw_p5=precision_at_k(raw_names, relevant, 5),
            raw_recall5=recall_at_k(raw_names, relevant, 5),
            raw_recall10=recall_at_k(raw_names, relevant, 10),
            raw_mrr=mrr(raw_names, relevant),
            raw_ndcg5=ndcg_at_k(raw_names, relevant, 5),
            raw_ndcg10=ndcg_at_k(raw_names, relevant, 10),
            unit_p1=precision_at_k(unit_names, relevant, 1),
            unit_p3=precision_at_k(unit_names, relevant, 3),
            unit_p5=precision_at_k(unit_names, relevant, 5),
            unit_recall5=recall_at_k(unit_names, relevant, 5),
            unit_recall10=recall_at_k(unit_names, relevant, 10),
            unit_mrr=mrr(unit_names, relevant),
            unit_ndcg5=ndcg_at_k(unit_names, relevant, 5),
            unit_ndcg10=ndcg_at_k(unit_names, relevant, 10),
            raw_candidate_count=len(raw_scored),
            unit_candidate_count=len(unit_scored),
            supporting_count=supporting_total,
            raw_ms=raw_ms,
            unit_ms=unit_ms,
        ))

    # Aggregate
    n = len(results)
    aggregate = {
        "raw": {
            "p1":        sum(r.raw_p1       for r in results) / n,
            "p3":        sum(r.raw_p3       for r in results) / n,
            "p5":        sum(r.raw_p5       for r in results) / n,
            "recall5":   sum(r.raw_recall5  for r in results) / n,
            "recall10":  sum(r.raw_recall10 for r in results) / n,
            "mrr":       sum(r.raw_mrr      for r in results) / n,
            "ndcg5":     sum(r.raw_ndcg5    for r in results) / n,
            "ndcg10":    sum(r.raw_ndcg10   for r in results) / n,
            "avg_candidates": sum(r.raw_candidate_count for r in results) / n,
            "avg_ms":    sum(r.raw_ms       for r in results) / n,
        },
        "primary_units": {
            "p1":        sum(r.unit_p1       for r in results) / n,
            "p3":        sum(r.unit_p3       for r in results) / n,
            "p5":        sum(r.unit_p5       for r in results) / n,
            "recall5":   sum(r.unit_recall5  for r in results) / n,
            "recall10":  sum(r.unit_recall10 for r in results) / n,
            "mrr":       sum(r.unit_mrr      for r in results) / n,
            "ndcg5":     sum(r.unit_ndcg5    for r in results) / n,
            "ndcg10":    sum(r.unit_ndcg10   for r in results) / n,
            "avg_candidates": sum(r.unit_candidate_count for r in results) / n,
            "avg_ms":    sum(r.unit_ms       for r in results) / n,
        }
    }

    return results, aggregate


def main():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    print(f"Loading corpus from {HOLDOUT_DIR}...")
    try:
        elements = load_corpus(HOLDOUT_DIR)
    except SystemExit:
        # Generate synthetic results if corpus unavailable
        print("NOTE: Calendar holdout not accessible from this environment.")
        print("      Generating benchmark manifest without running evaluation.")
        manifest = {
            "phase": "6.4",
            "status": "corpus_unavailable",
            "note": "Run from the amoeba/ directory with the calendar holdout present.",
            "queries": [q["id"] for q in EVAL_QUERIES],
        }
        with open(RESULTS_JSON, "w") as f:
            json.dump(manifest, f, indent=2)
        print(f"Manifest written to {RESULTS_JSON}")
        return

    print(f"  Loaded {len(elements)} raw elements")

    print("Building primary retrieval units...")
    units = build_retrieval_units(elements)
    primary_count = sum(1 for u in units)
    supporting_count = sum(len(u.get("supporting", [])) for u in units)
    print(f"  {primary_count} primary units")
    print(f"  {supporting_count} supporting elements attached")
    print(f"  Candidate reduction: {len(elements)} → {primary_count} "
          f"({100*(1 - primary_count/len(elements)):.1f}% reduction)")

    print("Evaluating...")
    query_results, aggregate = evaluate(elements, units)

    # Write JSON
    output = {
        "phase": "6.4",
        "corpus": str(HOLDOUT_DIR),
        "corpus_stats": {
            "total_elements": len(elements),
            "primary_units": primary_count,
            "supporting_elements": supporting_count,
            "candidate_reduction_pct":
                100 * (1 - primary_count / len(elements)) if elements else 0,
        },
        "n_queries": len(query_results),
        "methodology": "Representation-level evaluation mirroring C++ PrimaryRetrievalPipeline",
        "aggregate": aggregate,
        "per_query": [asdict(r) for r in query_results],
        "limitations": [
            "Evaluation uses simplified Python tokenization, not the full C++ CodeTokenizer.",
            "Semantic retrieval is not evaluated (requires MiniLM embeddings at Python level).",
            "Results reflect the candidate population quality, not the full ranker behavior.",
            "N=10 queries is a small validation set; results are not statistically generalizable.",
        ]
    }

    with open(RESULTS_JSON, "w") as f:
        json.dump(output, f, indent=2)
    print(f"Results written to {RESULTS_JSON}")

    # Write CSV
    with open(PER_QUERY_CSV, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(asdict(query_results[0]).keys()))
        writer.writeheader()
        for r in query_results:
            writer.writerow(asdict(r))
    print(f"Per-query CSV written to {PER_QUERY_CSV}")

    # Print summary
    print("\n" + "=" * 70)
    print(f"{'Metric':<20} {'Before (raw)':>15} {'After (units)':>15}")
    print("=" * 70)
    for metric in ["p1", "p3", "p5", "recall5", "mrr", "ndcg5"]:
        raw_val  = aggregate["raw"][metric]
        unit_val = aggregate["primary_units"][metric]
        delta    = unit_val - raw_val
        flag     = "▲" if delta > 0.005 else ("▼" if delta < -0.005 else "=")
        print(f"  {metric.upper():<18} {raw_val:>15.3f} {unit_val:>15.3f}  {flag} {delta:+.3f}")
    print("-" * 70)
    print(f"  {'Avg candidates':<18} {aggregate['raw']['avg_candidates']:>15.1f} "
          f"{aggregate['primary_units']['avg_candidates']:>15.1f}")
    print("=" * 70)


if __name__ == "__main__":
    main()
