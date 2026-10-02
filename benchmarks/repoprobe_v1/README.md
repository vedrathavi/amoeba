# Amoeba RepoProbe v1 Benchmark Dataset

## 1. Purpose

This benchmark establishes a real-world, product-level evaluation suite for Amoeba—a deterministic, high-performance C++20 repository comprehension and code-grounding engine.

The benchmark combines:
- **50 open-ended repository comprehension questions** extracted from the official [Tencent-Hunyuan RepoProbe](https://github.com/Tencent-Hunyuan/RepoProbe) dataset, derived from real GitHub Discussions across 10 diverse open-source repositories.
- **20 Amoeba-designed diagnostic questions** specifically targeted at evaluating grounding boundaries, AST symbol discovery, cross-file reasoning, ambiguous terminology rejection, and natural-language vocabulary gaps.

---

## 2. Explicit Disclaimer & Benchmark Scope

> **IMPORTANT NOTICE**:  
> This benchmark is an **internal engineering/product evaluation of Amoeba** and is **NOT** intended to reproduce RepoProbe's published benchmark scores or run its autonomous agent scaffolding harness.  
> We use RepoProbe solely as a standardized external source of real-world repository comprehension questions, pinned repository snapshots, and checklist-based ground truth.

---

## 3. Selected Repositories (10 Repositories)

To ensure universal code-understanding evaluation across programming languages, repository sizes, and software architectures, 10 repositories were selected:

| # | Repository | Language | Stars | Size | Pinned Snapshot Commit | Domain & Architecture |
|---|---|---|---|---|---|---|
| 1 | `duckdb/ducklake` | **C++** | 2,374 | Medium | `c16de934130a7aa6c2d88a97acba935450276633` | Lakehouse storage extension for DuckDB |
| 2 | `google/adk-python` | **Python** | 16,906 | Medium | `56775afc48ee54e9cbea441a6e0fa6c8a12891b9` | Agent Development Kit, async runner, MCP protocol |
| 3 | `docling-project/docling` | **Python** | 48,782 | Large | `be085c0e39dd5c51572b883d0f795c5a7abefd5d` | Document layout extraction, OCR parsing pipelines |
| 4 | `better-auth/better-auth` | **TypeScript** | 24,695 | Large | `d343b34cf6c0383d0a5833976632462b192dbc1e` | Authentication engine, session & OAuth plugins |
| 5 | `Dokploy/dokploy` | **TypeScript** | 28,697 | Medium | `6e67864204035054822c2e6b69b5185cb1e670ff` | Application deployment platform, Traefik, Docker Swarm |
| 6 | `apple/pkl` | **Java** | 10,991 | Large | `35861240a061504374411f87df13d7bf88466f36` | Apple configuration-as-code compiler & evaluator |
| 7 | `henrygd/beszel` | **Go** | 18,289 | Medium | `2bd85e04fca688a6a1922e1bf4098fcd808c0273` | Server metrics daemon, PocketBase hub, alerts |
| 8 | `opencloud-eu/opencloud` | **Go** | 4,509 | Large | `941aa689d2c2ebb53832b3b2f02beebeda1b26d9` | Microservice cloud storage platform, gRPC/CS3 |
| 9 | `BurntSushi/jiff` | **Rust** | 2,512 | Medium | `53708b9b6e7e329e3d46e91fd8b5266adc3b4f40` | Datetime arithmetic, time zone engine, ISO parsing |
| 10 | `abenz1267/walker` | **Rust** | 2,318 | Small/Med | `1395d9205253c7038698e1dcca9c4d1d7dfd567b` | Wayland application launcher, module plugins |

---

## 4. Snapshot Policy

RepoProbe questions and reference checklists were written against exact repository states.
- Evaluating against current GitHub `HEAD` can introduce silent false positives or invalid ground truth due to subsequent refactorings.
- All evaluation runs must checkout the **exact recorded commit SHA** listed above.

---

## 5. Question Dataset Breakdown (70 Total Questions)

### A. RepoProbe Extracted Questions (50 Questions)
- 5 questions per repository across 10 repositories (with `ducklake`: 4, `adk-python`: 6).
- Sourced directly from `dataset/*.csv` in Tencent-Hunyuan RepoProbe.
- Questions preserve original verbatim text, discussion IDs, taxonomies, reference answers, and point-based checklists.

### B. Amoeba Diagnostic Questions (20 Questions)
Specially constructed to test Amoeba's grounding invariants across 10 core dimensions:

1. **Symbol Lookup** (2 questions): Pinpoints exact file definitions and struct fields (e.g., `Span` struct in Jiff, Transaction struct in DuckLake).
2. **Implementation Details** (2 questions): Examines core algorithm logic (e.g., session verification in Better-Auth, CPU stats collection in Beszel).
3. **Relationship Discovery** (2 questions): Traces callers and callees across AST symbols (e.g., `after_agent_callback` callers in ADK-Python).
4. **Cross-file Interactions** (2 questions): Validates understanding across microservice and module boundaries (e.g., storage vs metadata services in OpenCloud).
5. **Architectural Request Flow** (2 questions): Traces end-to-end request pipelines (e.g., `DocumentConverter` flow in Docling, UI query dispatch in Walker).
6. **State & Data Flow** (2 questions): Identifies persistence models and database state (e.g., deployment lifecycle status in Dokploy).
7. **Configuration & Secrets** (2 questions): Analyzes credential configuration and alert options (e.g., S3 secrets in DuckLake, alerts in Beszel).
8. **Negative / Unsupported Refusal** (2 questions): Tests grounded refusal (`RefusalReason::InsufficientEvidence`) on absent features (e.g., lunar calendar in Jiff, Qdrant in ADK).
9. **Ambiguous / Generic Term Disambiguation** (2 questions): Adversarially tests generic suffixes (`resolver`, `controller`) against non-existent subsystems (e.g., GraphQL in OpenCloud, Spring Boot in Pkl).
10. **Vocabulary Gap Recovery** (2 questions): Evaluates query understanding when user terms do not lexically match code symbol names (e.g., daylight saving duration calculation in Jiff).

---

## 6. Dataset Structure

```text
benchmarks/repoprobe_v1/
├── README.md                      # This documentation
├── repositories.json              # 10 selected repository metadata & pinned commit hashes
├── questions.json                 # 70 benchmark questions with metadata & reference answers
├── ground_truth.json              # Structured ground truth (expected files, symbols, checklists)
├── results/                       # Output directory for future benchmark runs
└── scripts/
    ├── generate_benchmark_dataset.py  # Deterministic dataset builder from RepoProbe
    └── validate_benchmark.py          # Strict schema & referential integrity validator
```

---

## 7. Ground-Truth Methodology

Each question entry in `ground_truth.json` contains:
- `expected_files`: List of repository-relative paths containing the relevant implementation.
- `expected_symbols`: Concrete AST identifiers (classes, functions, structs, hooks) that must be identified.
- `expected_relationships`: Key caller-callee or module dependencies.
- `expected_concepts`: Conceptual domain requirements.
- `reference_answer`: Full human-verified or RepoProbe ground truth response.
- `checklist`: Point-weighted rubric for scoring factual completeness.
- `ground_truth_status`: `fully_grounded` (established with precise files/symbols) or `needs_manual_review` (RepoProbe questions pending automated snapshot symbol indexing).

---

## 8. Diagnostic Evaluation Pipeline (For Future Execution)

When Amoeba is evaluated against this benchmark, the execution pipeline produces detailed diagnostic telemetry for every question:

```text
Question
  ↓
Initial Retrieval (BM25 + Semantic + CodeAware)
  ↓
Evidence Sufficiency Gate
  ├── [Pass] → Context Assembly → LLM Grounded Answer
  │
  └── [Fail] → Tool Requests (search_code, find_symbol, read_file, get_relationships)
                     ↓
               Tool Evidence Adapter
                     ↓
               Evidence Sufficiency Gate (Re-evaluation)
                     ↓
               LLM Grounded Answer OR Refusal
```

Evaluation tracks:
1. **Retrieval Recall**: Did initial retrieval or tools recover the `expected_files` and `expected_symbols`?
2. **Evidence Precision**: Did the bundle exclude misleading generic false positives?
3. **Sufficiency Correctness**: Did the gate correctly distinguish sufficient from insufficient context?
4. **Answer Grounding**: Did the generated answer adhere strictly to the evidence without hallucination?
5. **Checklist Score**: Did the answer satisfy the point-based checklist?

---

## 9. Known Limitations

1. **Snapshot Fetching**: Repositories are fetched on-demand at their pinned commits; full repository cloning is decoupled from benchmark metadata.
2. **Subjective Discussion Style**: Some RepoProbe questions reflect conversational GitHub discussion phrasing; Amoeba's query understanding must handle varying phrasing without domain-specific hardcoded rules.
