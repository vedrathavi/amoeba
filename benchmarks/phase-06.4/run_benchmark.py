"""
Phase 6.4 Benchmark (inline runner using frozen Phase 6.2 holdout elements).
Run from d:/amoeba/:  python benchmarks/phase-06.4/run_benchmark.py
"""
import json, csv, os, re
from pathlib import Path

with open("benchmarks/phase-06.2/holdout/data/holdout_elements.json", "r") as f:
    RAW = json.load(f)

PRIMARY_KINDS = {"Class","Struct","Interface","Function","Method","Component","Hook","Route"}

FROZEN_QUERIES = [
    {"id":"Q1","query":"CalendarGrid","expected_name":"CalendarGrid"},
    {"id":"Q2","query":"use calendar","expected_name":"useCalendar"},
    {"id":"Q3","query":"month navigation","expected_name":"useCalendar"},
    {"id":"Q4","query":"EventCard","expected_name":"EventCard"},
    {"id":"Q5","query":"getImagePanelData","expected_name":"getImagePanelData"},
    {"id":"Q6","query":"location destination photo render month","expected_name":"getImagePanelData"},
    {"id":"Q7","query":"useCalendarData","expected_name":"useCalendarData"},
    {"id":"Q8","query":"DayView","expected_name":"DayView"},
    {"id":"Q9","query":"modal confirm","expected_name":"ConfirmModal"},
    {"id":"Q10","query":"route calendar view","expected_name":"CalendarPage"},
]

def tokenize(s): return re.findall(r"[a-z]+", (s or "").lower())
def is_primary(kind): return kind in PRIMARY_KINDS

def lex_score_raw(el, qtoks):
    name_toks = set(tokenize(el.get("name","") or ""))
    sc = sum(1 for t in qtoks if t in name_toks) * 25.0
    if is_primary(el.get("kind","")): sc += 10.0
    return sc

def lex_score_unit(u, qtoks):
    sc = lex_score_raw(u, qtoks)
    for ev in u.get("supporting", []):
        ev_toks = set(tokenize(ev.get("name","") or ""))
        sc += sum(1 for t in qtoks if t in ev_toks) * 5.0
    return sc

# Build primary units
by_file = {}
for el in RAW:
    fp = el.get("file_path","")
    by_file.setdefault(fp, []).append(el)

units = []
for fp, fels in by_file.items():
    primaries = [e for e in fels if is_primary(e.get("kind",""))]
    supporting = [e for e in fels if not is_primary(e.get("kind",""))]
    if not primaries:
        units.append({"kind":"Route","name":Path(fp).name,"file_path":fp,"detail":"File Module","supporting":fels})
        continue
    for p in primaries:
        u = dict(p)
        u["supporting"] = []
        for s in supporting:
            pc = s.get("parent_context","") or ""
            if pc and pc == (p.get("name","") or ""):
                u["supporting"].append(s)
                continue
            ss = int(s.get("start_line") or 0)
            se = int(s.get("end_line") or ss)
            ps = int(p.get("start_line") or 0)
            pe = int(p.get("end_line") or ps)
            if ps > 0 and pe >= ps and ss >= ps and se <= pe:
                u["supporting"].append(s)
        units.append(u)

total_elements = len(RAW)
primary_count = len(units)
supporting_count = sum(len(u.get("supporting",[])) for u in units)

print(f"Total elements:   {total_elements}")
print(f"Primary units:    {primary_count}")
print(f"Supporting elems: {supporting_count}")
print(f"Reduction:        {100*(1-primary_count/total_elements):.1f}%")
print()

def p_at_1(results, expected): return 1.0 if results and results[0] == expected else 0.0
def mrr_val(results, expected):
    for i,r in enumerate(results,1):
        if r == expected: return 1.0/i
    return 0.0

rows = []
raw_p1s=[]; raw_mrrs=[]; unit_p1s=[]; unit_mrrs=[]

for q in FROZEN_QUERIES:
    qtoks = tokenize(q["query"])
    exp = q["expected_name"]

    raw_sc = sorted(
        [(lex_score_raw(e, qtoks), e.get("name","")) for e in RAW if lex_score_raw(e,qtoks)>0],
        key=lambda x:-x[0])
    raw_names=[r[1] for r in raw_sc]

    unit_sc = sorted(
        [(lex_score_unit(u, qtoks), u.get("name","")) for u in units if lex_score_unit(u,qtoks)>0],
        key=lambda x:-x[0])
    unit_names=[r[1] for r in unit_sc]

    rp1=p_at_1(raw_names,exp); rm=mrr_val(raw_names,exp)
    up1=p_at_1(unit_names,exp); um=mrr_val(unit_names,exp)
    raw_p1s.append(rp1); raw_mrrs.append(rm)
    unit_p1s.append(up1); unit_mrrs.append(um)

    print(f"{q['id']:4s}  raw_p1={rp1:.0f}  unit_p1={up1:.0f}  "
          f"raw_top1={raw_names[0] if raw_names else 'none':25s}  "
          f"unit_top1={unit_names[0] if unit_names else 'none':25s}")
    rows.append({
        "id": q["id"], "query": q["query"], "expected": exp,
        "raw_p1": rp1, "raw_mrr": round(rm,3),
        "unit_p1": up1, "unit_mrr": round(um,3),
        "raw_candidates": len(raw_sc), "unit_candidates": len(unit_sc),
    })

n = len(FROZEN_QUERIES)
print()
print(f"{'Metric':<20} {'Raw':>10} {'Units':>10}  {'Delta':>10}")
print("-"*54)
rp1_avg = sum(raw_p1s)/n; up1_avg = sum(unit_p1s)/n
rm_avg  = sum(raw_mrrs)/n; um_avg  = sum(unit_mrrs)/n
print(f"{'Avg P@1':<20} {rp1_avg:>10.3f} {up1_avg:>10.3f}  {up1_avg-rp1_avg:>+10.3f}")
print(f"{'Avg MRR':<20} {rm_avg:>10.3f} {um_avg:>10.3f}  {um_avg-rm_avg:>+10.3f}")

os.makedirs("benchmarks/phase-06.4/data", exist_ok=True)
output = {
    "phase": "6.4",
    "corpus": "demo_test_projects/calendar (frozen Phase 6.2 holdout)",
    "methodology": "Representation-level evaluation mirroring C++ PrimaryRetrievalPipeline",
    "corpus_stats": {
        "total_elements": total_elements,
        "primary_units": primary_count,
        "supporting_elements": supporting_count,
        "candidate_reduction_pct": round(100*(1-primary_count/total_elements),1),
    },
    "n_queries": n,
    "aggregate": {
        "raw":           {"p1": round(rp1_avg,3), "mrr": round(rm_avg,3)},
        "primary_units": {"p1": round(up1_avg,3), "mrr": round(um_avg,3)},
    },
    "per_query": rows,
    "limitations": [
        "Simplified Python tokenization, not full C++ CodeTokenizer.",
        "Semantic retrieval not evaluated (no MiniLM embeddings in Python benchmark).",
        "N=10 query validation set — not statistically generalizable.",
        "Results reflect candidate population quality, not full ranker behavior.",
    ],
}
with open("benchmarks/phase-06.4/data/results.json", "w") as f:
    json.dump(output, f, indent=2)
with open("benchmarks/phase-06.4/data/per_query.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
    w.writeheader(); w.writerows(rows)

print()
print("Written: benchmarks/phase-06.4/data/results.json")
print("         benchmarks/phase-06.4/data/per_query.csv")
