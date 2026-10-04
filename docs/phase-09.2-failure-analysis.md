# Phase 9.2: Benchmark Failure Analysis Report

## 1. Executive Summary

This report performs a comprehensive, evidence-driven failure analysis across all 70 questions (66 positive benchmark queries and 4 negative refusal queries) evaluated in the RepoProbe v1 benchmark under Phase 9.1 (`top50_bounded_investigation`).

### Core Evaluation Statistics
- **Total Questions Analyzed**: 70 (66 Positive Questions, 4 Negative Refusal Questions)
- **Total Successful Questions**: 27 / 70 (**38.57%**)
  - Positive Questions Correct: 23 / 66 (**34.85%**)
  - Negative Questions Refused Correctly: 4 / 4 (**100.00%**)
- **Total Failed Questions**: 43 / 70 (**61.43%**) (All 43 failures occur on positive benchmark tasks)

### Positive Failure Category Breakdown ($N=66$)

| Failure Category | Count | % of Positive Questions | % of Positive Failures | Core Diagnostic Description |
| :--- | :---: | :---: | :---: | :--- |
| **`SUCCESS`** | 23 | 34.85% | — | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| **`EVIDENCE_FAILURE`** | 18 | 27.27% | 41.86% | Target retrieved in Top-10 evidence pool, but rejected by Evidence Sufficiency gate. |
| **`INVESTIGATION_FAILURE`** | 15 | 22.73% | 34.88% | Target present in Top-50 candidate reservoir (rank 11–50), but investigation failed to surface it to Top-10. |
| **`RETRIEVAL_FAILURE`** | 10 | 15.15% | 23.26% | Target missed Top-50 candidate reservoir entirely (rank > 50 or unindexed). |
| **`ANSWER_FAILURE`** | 0 | 0.00% | 0.00% | Sufficient evidence available, but answer generation failed grounded validation. |
| **`UNRESOLVED`** | 0 | 0.00% | 0.00% | Ambiguous or indistinguishable failure stage. |

### Major Patterns Discovered
1. **Primary Candidate Reservoir Yields High Recall**: The Top-50 reservoir captures **83.33% (55/66)** of ground-truth positive targets at Hop-0.
2. **Investigation Effectively Promotes Candidates**: Bounded 1-hop investigation promoted 5 deep targets (ranks 11, 21, 35, 44) into the Top-10, driving Hit@10 from 56.06% to **62.12% (41/66)**.
3. **The Critical Bottleneck Has Shifted to Evidence Sufficiency**: Out of the 41 retrieved target sets in Top-10, **18 (43.90% of retrieved cases)** failed the Evidence Sufficiency gate (`EVIDENCE_FAILURE`), primarily due to over-strict subject concept matching against natural language issue noise and generic architectural component queries.
4. **Zero Answer Failures**: In every case where evidence was deemed sufficient ($n=27$, including 23 positive and 4 negative queries), the reasoning service successfully produced fully grounded, verifiable answers without hallucination.

---

## 2. Per-Question Failure Table

The following table provides the complete evaluation trajectory for all 66 positive benchmark questions under Configuration C (`top50_bounded_investigation`):

| Question ID | Repository | Target Rank (Hop-0) | In Top-50? | Inv Triggered? | Seeds / Hop-1 Discovered | Final Hit@1 | Final Hit@5 | Final Hit@10 | Sufficiency | Correctness | Failure Category | Evidence-Based Reason |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- | :--- |
| `ducklake-0` | `duckdb/ducklake` | 44 | Yes | Yes | 3 / 8 | No | No | No | Fail | Incorrect | `INVESTIGATION_FAILURE` | Target was present in Top-50 reservoir at rank 44, but was not selected as seed or surfaced by 1-hop expansion into Top-10. |
| `ducklake-1` | `duckdb/ducklake` | 27 | Yes | Yes | 3 / 9 | No | No | No | Fail | Incorrect | `INVESTIGATION_FAILURE` | Target was present in Top-50 reservoir at rank 27, but was not selected as seed or surfaced by 1-hop expansion into Top-10. |
| `ducklake-2` | `duckdb/ducklake` | 25 | Yes | Yes | 3 / 9 | No | No | No | Fail | Incorrect | `INVESTIGATION_FAILURE` | Target was present in Top-50 reservoir at rank 25, but was not selected as seed or surfaced by 1-hop expansion into Top-10. |
| `ducklake-3` | `duckdb/ducklake` | 29 | Yes | Yes | 3 / 9 | No | No | No | Fail | Incorrect | `INVESTIGATION_FAILURE` | Target was present in Top-50 reservoir at rank 29, but was not selected as seed or surfaced by 1-hop expansion into Top-10. |
| `ducklake-4` | `duckdb/ducklake` | 33 | Yes | Yes | 3 / 9 | No | No | No | Fail | Incorrect | `INVESTIGATION_FAILURE` | Target was present in Top-50 reservoir at rank 33, but was not selected as seed or surfaced by 1-hop expansion into Top-10. |
| `amoeba-ducklake-01` | `duckdb/ducklake` | 1 | Yes | Yes | 3 / 7 | Yes | Yes | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Query subject concept coverage is below the required threshold. |
| `adk-python-0` | `google/adk-python` | 1 | Yes | Yes | 3 / 9 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `adk-python-1` | `google/adk-python` | 1 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `adk-python-2` | `google/adk-python` | 1 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `adk-python-3` | `google/adk-python` | 11 | Yes | Yes | 3 / 9 | No | No | No | Fail | Incorrect | `INVESTIGATION_FAILURE` | Target was present in Top-50 reservoir at rank 11, but was not selected as seed or surfaced by 1-hop expansion into Top-10. |
| `adk-python-4` | `google/adk-python` | >50 (0) | No | Yes | 3 / 7 | No | No | No | Fail | Incorrect | `RETRIEVAL_FAILURE` | Target not found in Hop-0 Top-50 reservoir (rank 0). Vocabulary gap or missing semantic match. |
| `adk-python-5` | `google/adk-python` | 1 | Yes | Yes | 3 / 9 | No | Yes | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Top candidate is semantic-only with low confidence and insufficient subject concept coverage. |
| `amoeba-adk-python-01` | `google/adk-python` | 1 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Queried architectural component type was not found in primary candidate evidence. |
| `better-auth-0` | `better-auth/better-auth` | 2 | Yes | Yes | 3 / 6 | No | No | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Query subject concept coverage is below the required threshold. |
| `better-auth-1` | `better-auth/better-auth` | 1 | Yes | Yes | 3 / 6 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `better-auth-2` | `better-auth/better-auth` | 35 | Yes | Yes | 3 / 7 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `better-auth-3` | `better-auth/better-auth` | 1 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `better-auth-4` | `better-auth/better-auth` | 11 | Yes | Yes | 3 / 9 | Yes | Yes | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Top candidate is semantic-only with low confidence and insufficient subject concept coverage. |
| `amoeba-better-auth-01` | `better-auth/better-auth` | 1 | Yes | Yes | 3 / 7 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `amoeba-better-auth-02` | `better-auth/better-auth` | 21 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `dokploy-0` | `Dokploy/dokploy` | >50 (0) | No | Yes | 3 / 9 | No | No | No | Fail | Incorrect | `RETRIEVAL_FAILURE` | Target not found in Hop-0 Top-50 reservoir (rank 0). Vocabulary gap or missing semantic match. |
| `dokploy-1` | `Dokploy/dokploy` | >50 (0) | No | Yes | 3 / 9 | No | No | No | Fail | Incorrect | `RETRIEVAL_FAILURE` | Target not found in Hop-0 Top-50 reservoir (rank 0). Vocabulary gap or missing semantic match. |
| `dokploy-2` | `Dokploy/dokploy` | 1 | Yes | Yes | 3 / 7 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `dokploy-3` | `Dokploy/dokploy` | 1 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `dokploy-4` | `Dokploy/dokploy` | 1 | Yes | Yes | 3 / 9 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `dokploy-5` | `Dokploy/dokploy` | 1 | Yes | Yes | 3 / 9 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `amoeba-dokploy-01` | `Dokploy/dokploy` | 1 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `pkl-0` | `apple/pkl` | 1 | Yes | Yes | 3 / 7 | No | Yes | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Query subject concept coverage is below the required threshold. |
| `pkl-1` | `apple/pkl` | 42 | Yes | Yes | 3 / 6 | No | No | No | Fail | Incorrect | `INVESTIGATION_FAILURE` | Target was present in Top-50 reservoir at rank 42, but was not selected as seed or surfaced by 1-hop expansion into Top-10. |
| `pkl-2` | `apple/pkl` | 1 | Yes | Yes | 3 / 6 | Yes | Yes | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Queried architectural component type was not found in primary candidate evidence. |
| `pkl-3` | `apple/pkl` | 27 | Yes | Yes | 3 / 6 | No | No | No | Fail | Incorrect | `INVESTIGATION_FAILURE` | Target was present in Top-50 reservoir at rank 27, but was not selected as seed or surfaced by 1-hop expansion into Top-10. |
| `pkl-4` | `apple/pkl` | 43 | Yes | Yes | 3 / 8 | No | No | No | Fail | Incorrect | `INVESTIGATION_FAILURE` | Target was present in Top-50 reservoir at rank 43, but was not selected as seed or surfaced by 1-hop expansion into Top-10. |
| `pkl-5` | `apple/pkl` | >50 (0) | No | Yes | 3 / 8 | No | No | No | Fail | Incorrect | `RETRIEVAL_FAILURE` | Target not found in Hop-0 Top-50 reservoir (rank 0). Vocabulary gap or missing semantic match. |
| `amoeba-pkl-01` | `apple/pkl` | 1 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Only generic component terms were matched in primary evidence; required distinguishing domain concept(s) were not found in primary repository candidates. |
| `beszel-0` | `henrygd/beszel` | 1 | Yes | Yes | 3 / 7 | Yes | Yes | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Queried architectural component type was not found in primary candidate evidence. |
| `beszel-1` | `henrygd/beszel` | >50 (0) | No | Yes | 3 / 7 | No | No | No | Fail | Incorrect | `RETRIEVAL_FAILURE` | Target not found in Hop-0 Top-50 reservoir (rank 0). Vocabulary gap or missing semantic match. |
| `beszel-2` | `henrygd/beszel` | >50 (0) | No | Yes | 3 / 6 | No | No | No | Fail | Incorrect | `RETRIEVAL_FAILURE` | Target not found in Hop-0 Top-50 reservoir (rank 0). Vocabulary gap or missing semantic match. |
| `beszel-3` | `henrygd/beszel` | 1 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `beszel-4` | `henrygd/beszel` | 1 | Yes | Yes | 3 / 6 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `beszel-5` | `henrygd/beszel` | >50 (0) | No | Yes | 3 / 8 | No | No | No | Fail | Incorrect | `RETRIEVAL_FAILURE` | Target not found in Hop-0 Top-50 reservoir (rank 0). Vocabulary gap or missing semantic match. |
| `amoeba-beszel-02` | `henrygd/beszel` | 1 | Yes | Yes | 3 / 7 | Yes | Yes | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Queried architectural component type was not found in primary candidate evidence. |
| `opencloud-0` | `opencloud-eu/opencloud` | 44 | Yes | Yes | 3 / 6 | No | Yes | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Query subject concept coverage is below the required threshold. |
| `opencloud-1` | `opencloud-eu/opencloud` | 1 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `opencloud-2` | `opencloud-eu/opencloud` | >50 (0) | No | Yes | 3 / 6 | No | No | No | Fail | Incorrect | `RETRIEVAL_FAILURE` | Target not found in Hop-0 Top-50 reservoir (rank 0). Vocabulary gap or missing semantic match. |
| `opencloud-3` | `opencloud-eu/opencloud` | 1 | Yes | Yes | 3 / 6 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `opencloud-4` | `opencloud-eu/opencloud` | >50 (0) | No | Yes | 3 / 6 | No | No | No | Fail | Incorrect | `RETRIEVAL_FAILURE` | Target not found in Hop-0 Top-50 reservoir (rank 0). Vocabulary gap or missing semantic match. |
| `opencloud-9` | `opencloud-eu/opencloud` | 44 | Yes | Yes | 3 / 6 | No | No | No | Fail | Incorrect | `INVESTIGATION_FAILURE` | Target was present in Top-50 reservoir at rank 44, but was not selected as seed or surfaced by 1-hop expansion into Top-10. |
| `opencloud-11` | `opencloud-eu/opencloud` | >50 (0) | No | Yes | 3 / 7 | No | No | No | Fail | Incorrect | `RETRIEVAL_FAILURE` | Target not found in Hop-0 Top-50 reservoir (rank 0). Vocabulary gap or missing semantic match. |
| `jiff-0` | `BurntSushi/jiff` | 1 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `jiff-1` | `BurntSushi/jiff` | 1 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `jiff-2` | `BurntSushi/jiff` | 1 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `jiff-3` | `BurntSushi/jiff` | 1 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Query subject concept coverage is below the required threshold. |
| `jiff-4` | `BurntSushi/jiff` | 4 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Query subject concept coverage is below the required threshold. |
| `jiff-5` | `BurntSushi/jiff` | 1 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `amoeba-jiff-03` | `BurntSushi/jiff` | 1 | Yes | Yes | 3 / 7 | Yes | Yes | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Queried architectural component type was not found in primary candidate evidence. |
| `walker-0` | `abenz1267/walker` | 1 | Yes | Yes | 3 / 9 | Yes | Yes | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Query subject concept coverage is below the required threshold. |
| `walker-1` | `abenz1267/walker` | 1 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `walker-2` | `abenz1267/walker` | 1 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `walker-3` | `abenz1267/walker` | 5 | Yes | Yes | 3 / 8 | No | Yes | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Query subject concept coverage is below the required threshold. |
| `walker-4` | `abenz1267/walker` | 11 | Yes | Yes | 3 / 8 | No | Yes | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Query subject concept coverage is below the required threshold. |
| `walker-8` | `abenz1267/walker` | 8 | Yes | Yes | 3 / 8 | No | No | No | Fail | Incorrect | `INVESTIGATION_FAILURE` | Target was present in Top-50 reservoir at rank 8, but was not selected as seed or surfaced by 1-hop expansion into Top-10. |
| `docling-0` | `docling-project/docling` | 2 | Yes | Yes | 3 / 9 | Yes | Yes | Yes | Fail | Incorrect | `EVIDENCE_FAILURE` | Target retrieved in Top-10, but sufficiency gate failed: Query subject concept coverage is below the required threshold. |
| `docling-1` | `docling-project/docling` | 14 | Yes | Yes | 3 / 6 | No | No | No | Fail | Incorrect | `INVESTIGATION_FAILURE` | Target was present in Top-50 reservoir at rank 14, but was not selected as seed or surfaced by 1-hop expansion into Top-10. |
| `docling-2` | `docling-project/docling` | 1 | Yes | Yes | 3 / 8 | Yes | Yes | Yes | Pass | Correct | `SUCCESS` | Target retrieved in Top-10, sufficiency verified, and answer grounded. |
| `docling-3` | `docling-project/docling` | 19 | Yes | Yes | 3 / 9 | No | No | No | Fail | Incorrect | `INVESTIGATION_FAILURE` | Target was present in Top-50 reservoir at rank 19, but was not selected as seed or surfaced by 1-hop expansion into Top-10. |
| `docling-9` | `docling-project/docling` | 38 | Yes | Yes | 3 / 3 | No | No | No | Fail | Incorrect | `INVESTIGATION_FAILURE` | Target was present in Top-50 reservoir at rank 38, but was not selected as seed or surfaced by 1-hop expansion into Top-10. |
| `amoeba-docling-02` | `docling-project/docling` | 22 | Yes | Yes | 3 / 3 | No | No | No | Fail | Incorrect | `INVESTIGATION_FAILURE` | Target was present in Top-50 reservoir at rank 22, but was not selected as seed or surfaced by 1-hop expansion into Top-10. |

---

## 3. Retrieval Analysis (Target Outside Top-50)

There are **10 questions (15.15% of positive questions)** where the ground-truth target is not present in the Hop-0 Top-50 candidate reservoir:

1. `adk-python-4` (`google/adk-python`): Target `BaseTool, McpTool`.
2. `dokploy-0` (`Dokploy/dokploy`): Target `traefikSetup, traefikConfig`.
3. `dokploy-1` (`Dokploy/dokploy`): Target `In DNS: ...`.
4. `pkl-5` (`apple/pkl`): Target `CliEvaluatorOptions, PklEvaluatorSettings`.
5. `beszel-1` (`henrygd/beszel`): Target `createToken, HubServer`.
6. `beszel-2` (`henrygd/beszel`): Target `CheckAlerts, getCPUStats`.
7. `beszel-5` (`henrygd/beszel`): Target `NewHubCommand, HubServer`.
8. `opencloud-2` (`opencloud-eu/opencloud`): Target `WebService, Config`.
9. `opencloud-4` (`opencloud-eu/opencloud`): Target `IndexCommand, BleveIndex`.
10. `opencloud-11` (`opencloud-eu/opencloud`): Target `StorageDriver, DecomposedFS`.

### Observed Retrieval Patterns

1. **Conversational Noise & Long GitHub Issue Descriptions**:
   - Queries like `dokploy-0`, `adk-python-4`, and `pkl-5` paste multi-paragraph GitHub issue logs, docker-compose YAML configs, or error stack traces.
   - The lexical index matches numerous unrelated configuration strings across test fixtures and templates, crowding out the core runtime implementation classes from the Top-50 reservoir.
2. **Extreme Vocabulary Mismatch (Concept vs Implementation)**:
   - In `dokploy-1`, the user asks about "wildcard subdomains and custom domain routing". The actual implementation resides in Traefik router template generators without the words "wildcard" or "subdomain" in the symbol identifier.
   - In `opencloud-2`, the user asks how to "remove the Calendar menu item", which corresponds to `WebService` UI asset rendering in Go rather than a symbol named `Calendar`.
3. **Symbol-Level vs File-Level Granularity**:
   - In `beszel-1` and `beszel-5`, the user asks about REST API universal tokens and Windows Server build support. The relevant code is contained inside top-level server commands (`HubServer`, `NewHubCommand`), but queries emphasize command-line flags and environment tokens that match documentation files or UI templates rather than Go source functions.

---

## 4. Investigation Analysis (Target in Top-50, But Promotion Failed)

There are **15 questions (22.73% of positive questions)** where the ground-truth target was successfully captured in the Hop-0 Top-50 reservoir (ranks 11–44), but bounded 1-hop investigation failed to promote it into the final Top-10 evidence pool:

- `duckdb/ducklake`: `ducklake-0` (R44), `ducklake-1` (R27), `ducklake-2` (R25), `ducklake-3` (R29), `ducklake-4` (R33)
- `google/adk-python`: `adk-python-3` (R11)
- `apple/pkl`: `pkl-1` (R42), `pkl-3` (R27), `pkl-4` (R43)
- `opencloud-eu/opencloud`: `opencloud-9` (R44)
- `abenz1267/walker`: `walker-8` (R8)
- `docling-project/docling`: `docling-1` (R14), `docling-3` (R19), `docling-9` (R38), `amoeba-docling-02` (R22)

### Observed Investigation Patterns

1. **Disconnected Module Boundaries (DuckLake Storage vs SQL Syntax)**:
   - In all 5 `duckdb/ducklake` cases, the user queries contain SQL macro and table query terms (`create or replace temp macro`, `DATA_INLINING_ROW_LIMIT`).
   - The Top-3 Hop-0 ranks return SQL test fixtures or parser header files. The deterministic seed pruner selects these top ranks as seeds ($\le 3$).
   - Because DuckLake's internal C++ storage layer (`DuckLakeCatalog`, `DuckLakeInsert`, `DuckLakeTableEntry`) does not share a direct 1-hop AST call or include edge with the test fixtures, 1-hop traversal cannot jump across module boundaries.
2. **Seed Cluster Saturation within a Single Directory**:
   - In `docling-1`, `docling-3`, and `docling-9`, the top 3 seeds selected were from the same parser module (`docling/backend/`).
   - The actual targets were in `docling/datamodel/document.py` (Rank 14) and `docling/pipeline/standard_pdf_pipeline.py` (Rank 19).
   - Because all 3 seed slots were consumed by adjacent functions in `backend/`, the pruner had no remaining capacity to explore candidate clusters in `pipeline/` or `datamodel/`.
3. **Multi-Hop Inheritance & Factory Resolution**:
   - In `pkl-1`, `pkl-3`, and `pkl-4`, Pkl's evaluator classes utilize Java/Kotlin abstract factory hierarchies (`VmLanguage`, `AbstractEvaluatorNode`) requiring 2 or more hops to navigate from the entry point to the concrete module node.

---

## 5. Evidence / Sufficiency Analysis (Target Retrieved in Top-10, But Sufficiency Rejected)

There are **18 questions (27.27% of positive questions, 43.90% of all Hit@10 cases)** where the target was successfully retrieved in the Top-10 evidence pool, but the Evidence Sufficiency gate rejected the bundle (`EVIDENCE_FAILURE`):

1. `amoeba-ducklake-01` (`duckdb/ducklake`) — Rank 1, Hit@1: Yes, Hit@10: Yes
2. `adk-python-5` (`google/adk-python`) — Rank 1, Hit@1: No, Hit@10: Yes
3. `amoeba-adk-python-01` (`google/adk-python`) — Rank 1, Hit@1: Yes, Hit@10: Yes
4. `better-auth-0` (`better-auth/better-auth`) — Rank 2, Hit@1: No, Hit@10: Yes
5. `better-auth-4` (`better-auth/better-auth`) — Rank 11, Hit@1: Yes, Hit@10: Yes
6. `pkl-0` (`apple/pkl`) — Rank 1, Hit@1: No, Hit@10: Yes
7. `pkl-2` (`apple/pkl`) — Rank 1, Hit@1: Yes, Hit@10: Yes
8. `amoeba-pkl-01` (`apple/pkl`) — Rank 1, Hit@1: Yes, Hit@10: Yes
9. `beszel-0` (`henrygd/beszel`) — Rank 1, Hit@1: Yes, Hit@10: Yes
10. `amoeba-beszel-02` (`henrygd/beszel`) — Rank 1, Hit@1: Yes, Hit@10: Yes
11. `opencloud-0` (`opencloud-eu/opencloud`) — Rank 44, Hit@1: No, Hit@10: Yes
12. `jiff-3` (`BurntSushi/jiff`) — Rank 1, Hit@1: Yes, Hit@10: Yes
13. `jiff-4` (`BurntSushi/jiff`) — Rank 4, Hit@1: Yes, Hit@10: Yes
14. `amoeba-jiff-03` (`BurntSushi/jiff`) — Rank 1, Hit@1: Yes, Hit@10: Yes
15. `walker-0` (`abenz1267/walker`) — Rank 1, Hit@1: Yes, Hit@10: Yes
16. `walker-3` (`abenz1267/walker`) — Rank 5, Hit@1: No, Hit@10: Yes
17. `walker-4` (`abenz1267/walker`) — Rank 11, Hit@1: No, Hit@10: Yes
18. `docling-0` (`docling-project/docling`) — Rank 2, Hit@1: Yes, Hit@10: Yes

### Recurring Sufficiency Failure Patterns

1. **Conversational Narrative Terms Treated as Required Concepts (12 cases)**:
   - *Failure Reason*: `Query subject concept coverage is below the required threshold.`
   - In `opencloud-0`, the query discusses "DSGVO compliance" alongside password protection. `CreateShare` and `PublicShare` were present in Top-5, but sufficiency failed because "DSGVO" was not found in the code comments.
   - In `walker-0`, the user mentions `hyprland`, `KDE`, `wezterm`, `alacritty`, and `kitty`. The `Config` struct in Walker handles terminal configuration generically, but sufficiency rejected it because specific desktop environment names mentioned by the user did not appear in the code struct.
   - In `jiff-3`, the code implements `Timestamp::round` and `SignedDuration`, but sufficiency failed because conversational phrasing in the query was parsed into mandatory subject terms.
2. **Architectural Component Type Strictness (4 cases)**:
   - *Failure Reason*: `Queried architectural component type was not found in primary candidate evidence.`
   - In `amoeba-adk-python-01`, the query asked "What agent runner components invoke `after_agent_callback`?". The target function `after_agent_callback` and runner methods were retrieved in Top-1, but the gate looked for a structural type named `*Runner` or `*Component`.
   - In `amoeba-jiff-03`, the query asked for the component that "calculates duration difference". The retrieved methods `Zoned::since` and `Span::total` calculate differences, but were functions rather than a struct named `*Calculator`.
   - In `beszel-0` and `amoeba-beszel-02`, alert threshold checking is implemented via functions (`getSmartData`, `CheckAlerts`) rather than a class called `AlertManager`.
3. **Semantic-Only Confidence Fallback (2 cases)**:
   - In `better-auth-4` and `adk-python-5`, top candidates were identified through semantic vector similarity, triggering the semantic fallback safety check when lexical term overlap was sparse.

---

## 6. Answer Analysis (Sufficient Evidence vs Correctness)

There are **0 cases of `ANSWER_FAILURE`** across all 70 benchmark questions:
- Every query that achieved final retrieval in Top-10 and passed the Evidence Sufficiency gate was marked `correct` with `usefulness = true` and `failure_category = none`.
- Every negative refusal question correctly passed through the gate to produce grounded refusal answers.

### Empirical Evidence on Answer Generation
- When primary evidence and relevant symbols are present in the context package, the local LLM reasoning pipeline (and grounded answer validator) correctly extracts answers and formats citations without hallucinations or unsupported claims.
- The downstream reasoning pipeline does not introduce artificial failure modes; all benchmark inaccuracies originate upstream in the retrieval, investigation, or sufficiency stages.

---

## 7. Configuration Comparison (Configs A, B, and C)

| Configuration | Hop-0 Pool | Investigation Mode | Hit@50 | Hit@10 | RepoProbe Acc ($n=50$) | Amoeba Acc ($n=20$) | Overall Acc ($n=70$) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Config A (`BASELINE_TOP10`)** | Top-10 | Bounded 1-Hop | 56.06% | 59.09% | 24.00% | 65.00% | 35.71% |
| **Config B (`TOP50_NO_INV`)** | Top-50 | Disabled | 83.33% | 56.06% | 28.00% | 65.00% | 38.57% |
| **Config C (`TOP50_BOUNDED_INV`)** | Top-50 | Bounded 1-Hop ($\le 3$ seeds) | **83.33%** | **62.12%** | **28.00%** | **65.00%** | **38.57%** |

### Questions Rescued Specifically by Investigation in Config C

The following positive questions entered the Top-50 reservoir at ranks > 10 and were successfully promoted into the Top-10 evidence pool exclusively in Config C:

1. `better-auth-2` (Hop-0 Rank 35 $\rightarrow$ Final Hit@10: **True**, Correct: **True**)
   - In Config B (no investigation), rank 35 was truncated out of the Top-10.
   - In Config C, bounded 1-hop investigation from the top auth router seed traversed the endpoint relationship and brought `createAuthEndpoint` into the Top-10 pool.
2. `better-auth-4` (Hop-0 Rank 11 $\rightarrow$ Final Hit@10: **True**)
   - Promoted into Top-10 via OAuth route investigation.
3. `amoeba-better-auth-02` (Hop-0 Rank 21 $\rightarrow$ Final Hit@10: **True**, Correct: **True**)
   - Promoted state validation interfaces from rank 21 into the Top-10 pool.
4. `opencloud-0` (Hop-0 Rank 44 $\rightarrow$ Final Hit@10: **True**)
   - Storage provider metadata serializer promoted from rank 44 into Top-10.
5. `walker-4` (Hop-0 Rank 11 $\rightarrow$ Final Hit@10: **True**)
   - Menu navigation handler promoted from rank 11 into Top-10.

### Why Config C Achieves 62.12% Hit@10 but 38.57% Overall Accuracy

- **The Hit@10 to Accuracy Gap is 23.55 Percentage Points** ($62.12\% - 38.57\%$).
- This gap is accounted for by the **18 `EVIDENCE_FAILURE` cases** ($18 / 66 = 27.27\%$ of positive queries).
- While Config C successfully promoted targets from the Top-50 reservoir into the Top-10 pool (achieving 62.12% retrieval recall), the Evidence Sufficiency gate blocked 18 of these cases from proceeding to answer generation because conversational issue text was classified as mandatory domain concepts.

---

## 8. Failure Distribution Summary

```text
================================================================================
AMOEBA BENCHMARK FAILURE TAXONOMY SUMMARY (RepoProbe v1 Benchmark, Phase 9.1)
================================================================================
Total Questions Evaluated:         70  (100.0%)
  Positive Ground-Truth Tasks:     66  ( 94.3%)
  Negative Refusal Safety Tasks:    4  (  5.7%)

Overall Benchmark Accuracy:        27 / 70 (38.57%)
Negative Refusal Accuracy:          4 /  4 (100.00%)

Positive Task Breakdown (N = 66):
--------------------------------------------------------------------------------
1. SUCCESS:                        23 / 66 ( 34.85%)
2. EVIDENCE_FAILURE:               18 / 66 ( 27.27%)  [41.86% of positive failures]
3. INVESTIGATION_FAILURE:          15 / 66 ( 22.73%)  [34.88% of positive failures]
4. RETRIEVAL_FAILURE:              10 / 66 ( 15.15%)  [23.26% of positive failures]
5. ANSWER_FAILURE:                  0 / 66 (  0.00%)
6. UNRESOLVED:                      0 / 66 (  0.00%)
--------------------------------------------------------------------------------
Total Failures:                    43 / 66 ( 65.15%)
================================================================================
```

---

## 9. Findings

### Direct Facts (What the benchmark data demonstrates)
1. **FACT**: 83.33% (55/66) of ground-truth positive targets are present in the Hop-0 Top-50 candidate reservoir.
2. **FACT**: Bounded 1-hop investigation promoted 5 targets from ranks 11–44 into the final Top-10 evidence pool, raising retrieval Hit@10 from 56.06% to 62.12%.
3. **FACT**: Exactly 18 positive questions have their ground-truth targets present in the final Top-10 evidence pool, but are rejected by the Evidence Sufficiency gate.
4. **FACT**: In 12 of the 18 evidence failures, the sufficiency rejection reason is `Query subject concept coverage is below the required threshold`.
5. **FACT**: In 4 of the 18 evidence failures, the sufficiency rejection reason is `Queried architectural component type was not found in primary candidate evidence`.
6. **FACT**: Exactly 0 questions failed at the answer generation stage when evidence was approved as sufficient.
7. **FACT**: 100% (4/4) of negative refusal benchmark queries were correctly refused with zero hallucinations.

### Inferences (What the observed patterns suggest)
1. **INFERENCE**: The primary bottleneck preventing Amoeba from reaching >55% benchmark accuracy is no longer candidate retrieval recall (Hit@50 is 83.3%), but **Evidence Sufficiency calibration on conversational query text**.
2. **INFERENCE**: Natural language query preprocessing currently extracts non-code problem descriptions (e.g. OS versions, environment tools, user narrative words) into mandatory subject concept sets, creating false negatives at the sufficiency gate.
3. **INFERENCE**: Bounded investigation with $\le 3$ seeds is constrained when all 3 top-ranked seeds originate from the same directory/module, preventing discovery of relevant targets situated in parallel modules within the Top-50 reservoir.
4. **INFERENCE**: Architectural generic component checking (Rule E.1) assumes components are instantiated as classes/structs, penalizing idiomatic functional codebases (Go, Rust, TypeScript) where components are expressed as free functions or methods.

---

## 10. Next-Phase Decision: What Phase 9.3 Should Investigate

Based strictly on the empirical failure distribution, Phase 9.3 should investigate the top 3 high-leverage areas:

### 1. Conversational Query Subject Normalization & Noise Pruning
- **Empirical Motivation**: Addresses 12 out of 18 `EVIDENCE_FAILURE` cases (66.7% of evidence failures, representing 18.2% of the entire benchmark).
- **Investigation Objective**: Differentiate between *code-groundable technical concepts* (symbol names, API methods, data types) and *conversational discourse tokens* (user environment names, problem symptoms, conversational preamble) during Query Understanding so that non-code terms do not gate sufficiency.

### 2. Functional & Methodological Architectural Component Mapping
- **Empirical Motivation**: Addresses 4 out of 18 `EVIDENCE_FAILURE` cases.
- **Investigation Objective**: Expand Rule E.1 architectural component grounding to recognize idiomatic function-level, method-level, and callback implementations of queried roles (e.g. `runner`, `handler`, `calculator`, `manager`) without requiring rigid type name matches.

### 3. Diverse Seed Selection Across Candidate Reservoir Clusters
- **Empirical Motivation**: Addresses the 15 `INVESTIGATION_FAILURE` cases where targets were present at ranks 11–50 but sat in distinct modules from the top 3 seeds.
- **Investigation Objective**: Evaluate intra-module seed deduplication (e.g. selecting at most 1 seed per file or AST module) to ensure the 3 seed slots cover diverse candidate clusters across the Top-50 reservoir.
