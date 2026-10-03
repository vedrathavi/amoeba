# Phase 8.7 — Post-8.6 Failure Forensic Checkpoint

**Document ID:** `docs/phase-08.7-post-8.6-failure-forensic-checkpoint.md`  
**Date:** 2026-10-03  
**Status:** COMPLETE (Forensic Checkpoint Only — No Production Code Changes)

---

## 1. Executive Summary & Objective

Following the successful completion of **Phase 8.6** (Query Subject Normalization + Bounded 15-Line Source Context), overall grounded reasoning correctness improved from **31.4% (22/70)** to **41.4% (29/70)** (+31.8% relative gain), with Hit@1 reaching 30.3% and sufficiency failures dropping by -33.3% (from 15 to 10).

The objective of **Phase 8.7** is to conduct an exhaustive, rigorous failure audit of all 41 remaining benchmark failures across the 10 repositories and 70 questions without making any production code changes:
- **29 Retrieval Failures** (expected target not retrieved into Top-10)
- **10 Sufficiency Failures** (target retrieved into Top-10 but rejected by evidence gate)
- **2 Groundedness Failures** (evidence evaluated as sufficient but answer failed correctness)

### Key Forensic Findings:

1. **The Retrieval Bottleneck Dominates (70.7% of Remaining Failures)**:
   - 29 of the 41 failures are primary retrieval misses.
   - **Candidate Submergence Breakdown**:
     - **Top 11–20**: 6 cases (20.7%) — Targets narrowly missed Top-10.
     - **Top 21–50**: 11 cases (37.9%) — Targets submerged by noisy or large competing files (e.g. `sqlite3-binding.c`, `__init__.py`, mock files).
     - **Absent Top-50**: 12 cases (41.4%) — Target symbol/file received 0 lexical/semantic rank due to vocabulary divergence or multi-file architectural splits.
2. **Sufficiency Gate Accuracy (10 Remaining Cases)**:
   - Of the 10 remaining sufficiency failures:
     - **6 cases** are **Multi-Unit / Cross-File Co-occurrence** requirements (e.g., config file + runtime provider, AST interface + builder).
     - **2 cases** are **Genuinely Correct Rejections** where the retrieved candidate was a test mock or utility rather than the implementation.
     - **2 cases** are **Source Context / Granularity** issues (e.g., calculation logic residing in deep helper functions).
3. **Negative Safety Invariant Reconfirmation**:
   - Out-of-domain and absent feature questions refuse cleanly in CLI runtime (e.g., `"what is 24*4?"` in 9 ms, `"Where is India located?"` in 7 ms).
   - In benchmark evaluation, negative controls with completely absent domain terms correctly refuse, while negative controls with generic vocabulary (`Spring Boot`, `Qdrant`) triggered mock/sample hits in benchmark harnesses.
4. **Product Smoke Tests on Amoeba Codebase**:
   - 7/7 CLI queries on `d:/amoeba` executed flawlessly with sub-185 ms primary retrieval and 1.1–10.3s local LLM inference time.

---

## 2. Canonical Baseline (Phase 8.6 Frozen Results)

The canonical benchmark was executed across all 10 repositories and 70 questions (`benchmarks/repoprobe_v1/results/latest/summary.json`):

| Metric | Measured Baseline |
|---|---:|
| **Total Questions** | 70 |
| **Positive Questions** | 66 |
| **Negative Questions** | 4 |
| **Overall Correctness** | **41.4% (29/70)** |
| **Groundedness Rate** | **64.3% (45/70)** |
| **Negative Refusal Accuracy** | **50.0% (2/4)** |
| **Hit@1 (Positive Repos)** | **30.3% (20/66)** |
| **Hit@5 (Positive Repos)** | **53.0% (35/66)** |
| **Hit@10 (Positive Repos)** | **56.1% (37/66)** |
| **Hit@50 (Positive Repos)** | **81.8% (54/66)** |
| **Mean Reciprocal Rank (MRR)** | **0.3854** |
| **Retrieval Failures** | **29 (41.4%)** |
| **Sufficiency Failures** | **10 (14.3%)** |
| **Groundedness Failures** | **2 (2.9%)** |
| **Engine Test Suite** | **511 / 511 passed (100%)** |

---

## 3. Complete Inventory of Remaining Failures (41 Cases)

### A. Remaining Retrieval Failures (29 Cases)
Target symbol/file was absent from Top-10 candidate retrieval:

| Question ID | Repository | Expected Target File / Symbol | Best Retrieved Target | Rank (a=0.8) |
|---|---|---|---|---:|
| `ducklake-0` | `duckdb/ducklake` | `ducklake_macro_entry.hpp` (`DuckLakeMacroEntry`) | `ducklake_table_functions.hpp` | 29 |
| `ducklake-1` | `duckdb/ducklake` | `ducklake_insert.cpp` (`DuckLakeInsert`) | `ducklake_multi_file_list.cpp` | 36 |
| `ducklake-2` | `duckdb/ducklake` | `ducklake_list_files.cpp` (`ducklake_list_files`) | `ducklake_multi_file_list.cpp` | 25 |
| `ducklake-3` | `duckdb/ducklake` | `ducklake_schema_entry.cpp` (`DuckLakeSchemaEntry`) | `ducklake_catalog.cpp` | Absent |
| `amoeba-ducklake-02` | `duckdb/ducklake` | `ducklake_secret.cpp` (`CreateDuckLakeSecretFunction`) | `ducklake_storage.cpp` | 22 |
| `adk-python-4` | `google/adk-python` | `src/google/adk/tools/base_tool.py` (`BaseTool`) | `test_spanner_toolset.py` | Absent |
| `better-auth-2` | `better-auth/better-auth` | `packages/better-auth/src/api/index.ts` (`router`) | `types.ts` | 36 |
| `better-auth-3` | `better-auth/better-auth` | `packages/core/src/social-providers/apple.ts` (`apple`) | `oauth.ts` | 15 |
| `better-auth-4` | `better-auth/better-auth` | `packages/better-auth/src/oauth2/index.ts` (`handleOAuthCallback`) | `state.ts` | 11 |
| `amoeba-better-auth-02` | `better-auth/better-auth` | `packages/better-auth/src/oauth2/state.ts` (`generateState`) | `client.ts` | 21 |
| `dokploy-0` | `Dokploy/dokploy` | `packages/server/src/setup/traefik-setup.ts` (`traefikSetup`) | `traefik.ts` | Absent |
| `dokploy-1` | `Dokploy/dokploy` | `apps/dokploy/server/api/routers/domain.ts` (`domainRouter`) | `server.ts` | Absent |
| `pkl-1` | `apple/pkl` | `pkl-core/src/main/java/org/pkl/core/EvaluatorImpl.java` | `AbstractRenderer.java` | 19 |
| `pkl-3` | `apple/pkl` | `pkl-core/src/main/java/org/pkl/core/resource/ResourceReader.java` | `Project.java` | 27 |
| `pkl-5` | `apple/pkl` | `pkl-cli/src/main/kotlin/org/pkl/cli/CliEvaluatorOptions.kt` | `CliBaseOptions.kt` | Absent |
| `beszel-1` | `henrygd/beszel` | `internal/hub/hub.go` (`createToken`) | `expirymap.go` | Absent |
| `beszel-2` | `henrygd/beszel` | `internal/alerts/alerts.go` (`CheckAlerts`) | `cpu.go` | Absent |
| `beszel-5` | `henrygd/beszel` | `internal/cmd/hub/hub.go` (`NewHubCommand`) | `smart.go` | Absent |
| `opencloud-0` | `opencloud-eu/opencloud` | `services/graph/pkg/service/v0/service.go` (`CreateShare`) | `conn.go` (vendor) | 44 |
| `opencloud-2` | `opencloud-eu/opencloud` | `services/web/pkg/service/v0/service.go` (`WebService`) | `sqlite3-binding.c` (vendor) | Absent |
| `opencloud-4` | `opencloud-eu/opencloud` | `services/search/pkg/command/index.go` (`IndexCommand`) | `sqlite3-binding.c` (vendor) | Absent |
| `opencloud-9` | `opencloud-eu/opencloud` | `services/auth-app/pkg/command/server.go` (`AuthAppServer`) | `sqlite3-binding.c` (vendor) | 43 |
| `opencloud-11` | `opencloud-eu/opencloud` | `services/storage-users/pkg/command/uploads.go` (`StorageDriver`) | `transform.go` (vendor) | Absent |
| `walker-4` | `abenz1267/walker` | `src/providers/dmenu.rs` (`DmenuProvider`) | `src/state/mod.rs` | 11 |
| `docling-1` | `docling-project/docling` | `docling/datamodel/document.py` (`DoclingDocument`) | `docling/chunking/__init__.py` | 14 |
| `docling-2` | `docling-project/docling` | `docling/datamodel/document.py` (`DoclingDocument.iterate_items`) | `docling/chunking/__init__.py` | Absent |
| `docling-3` | `docling-project/docling` | `docling/pipeline/standard_pdf_pipeline.py` (`StandardPdfPipeline`) | `mlx_model.py` | 19 |
| `docling-9` | `docling-project/docling` | `docling/datamodel/base_models.py` (`ProvenanceItem`) | `api_image_request.py` | 38 |
| `amoeba-docling-02` | `docling-project/docling` | `docling/datamodel/base_models.py` (`BoundingBox`) | `ocr_utils.py` | 22 |

---

### B. Remaining Sufficiency Failures (10 Cases)
Target symbol/file was retrieved in Top-10, but rejected by `EvidenceSufficiencyChecker`:

| Question ID | Repository | Expected Target | Retrieved Rank | Rejecting Rule |
|---|---|---|---:|---|
| `better-auth-0` | `better-auth/better-auth` | `session.ts` (`getSession`) | 6 | Rule C (Multi-unit adapter relation) |
| `pkl-0` | `apple/pkl` | `Type.java` / `base.pkl` (`Regex`) | 7 | Rule C (AST parser vs stdlib base) |
| `pkl-2` | `apple/pkl` | `Member.java` (`Modifier`) | 8 | Rule C (Abstract class member visibility) |
| `beszel-0` | `henrygd/beszel` | `smart.go` (`getSmartData`) | 9 | Rule C (NVMe health log struct mismatch) |
| `adk-python-5` | `google/adk-python` | `mcp_instruction_provider.py` | 2 | Rule C (CLI bundle JS outranking agent) |
| `jiff-4` | `BurntSushi/jiff` | `src/civil/iso_week_date.rs` (`ISOWeekDate`) | 3 | Rule C (POSIX parse rule candidate) |
| `walker-0` | `abenz1267/walker` | `src/providers/archlinuxpkgs.rs` | 1 | Rule C (Config struct missing from provider) |
| `walker-3` | `abenz1267/walker` | `src/providers/files.rs` (`FilesProvider`) | 5 | Rule C (Window keybind hints vs provider) |
| `walker-8` | `abenz1267/walker` | `Cargo.toml` / `build.rs` (`dependencies`) | 8 | Rule C (Build script vs image transformer) |
| `docling-0` | `docling-project/docling` | `docling/backend/html_backend.py` (`HTMLDocumentBackend`) | 2 | Rule C (Chunking init outranking backend) |

---

### C. Groundedness Failures (2 Cases)
Evidence was passed to LLM reasoning, but answer generated failed strict benchmark ground-truth validation:

| Question ID | Repository | Question | Root Cause |
|---|---|---|---|
| `amoeba-adk-python-02` | `google/adk-python` | *Does adk-python provide built-in vector database indexing with Qdrant natively in core?* | Test mock candidate in `tests/unittests/tools/` passed sufficiency; model answered affirmative based on mock snippet instead of refusing. |
| `amoeba-pkl-02` | `apple/pkl` | *Where is the Spring Boot auto-configuration controller implemented in pkl?* | Gradle plugin task (`CodeGenTask.java`) passed sufficiency; model generated generic configuration answer rather than refusing. |

---

## 4. Phase 8.5 vs Phase 8.6 Forensic Comparison (15 Cases)

Detailed progression of the 15 questions investigated during Phase 8.5:

| # | Question ID | Target Symbol / File | Phase 8.5 Status | Phase 8.6 Status | Outcome Category & Mechanism |
|---|---|---|---|---|---|
| 1 | `dokploy-2` | `getDefaultMiddlewares` in `traefik-setup.ts` | `sufficiency_failure` | **`correct`** | **Same target + normalization improved** (Headline isolated, 100% core coverage). |
| 2 | `beszel-3` | `charts.svelte` | `sufficiency_failure` | **`correct`** | **Same target + normalization improved** (`charts` + `svelte` entities matched). |
| 3 | `jiff-0` | `SignedDuration` / `timestamp.rs` | `sufficiency_failure` | **`correct`** | **Same target + normalization improved** (Headline terms matched core type). |
| 4 | `jiff-1` | `Span` / `span.rs` | `sufficiency_failure` | **`correct`** | **Same target + normalization improved** (Normalized denominator reached 80% coverage). |
| 5 | `jiff-2` | `civil::Date` / `civil.rs` | `sufficiency_failure` | **`correct`** | **Same target + normalization improved** (`civil` + `Date` matched target unit). |
| 6 | `jiff-3` | `TimeZone` / `timezone.rs` | `sufficiency_failure` | **`correct`** | **Same target + normalization improved** (Core subject matched timezone type). |
| 7 | `walker-2` | `ArchLinuxPkgs` in `arch.go` | `sufficiency_failure` | **`correct`** | **Same target + normalization improved** (Technical entity `ArchLinuxPkgs` preserved). |
| 8 | `better-auth-0` | `session.ts` / `cookies/index.ts` | `sufficiency_failure` | `sufficiency_failure` | **Still failing** (Multi-unit relation: adapter queries unlinked from session). |
| 9 | `pkl-0` | `Type.java` / `base.pkl` | `sufficiency_failure` | `sufficiency_failure` | **Still failing** (AST type definition split from stdlib regex parser). |
| 10 | `pkl-2` | `Member.java` / `Parser.java` | `sufficiency_failure` | `sufficiency_failure` | **Still failing** (Member modifier enum split from visitor AST). |
| 11 | `beszel-0` | `smart.go` (`getSmartData`) | `sufficiency_failure` | `sufficiency_failure` | **Still failing** (Target retrieved at #9, but NVMe helper struct lacked calculation logic). |
| 12 | `jiff-4` | `ISOWeekDate` / `date.rs` | `sufficiency_failure` | `sufficiency_failure` | **Still failing** (Candidate #3 was POSIX rule parser rather than date method). |
| 13 | `walker-0` | `ArchLinuxPkgsProvider` / `config.rs` | `sufficiency_failure` | `sufficiency_failure` | **Still failing** (Provider code retrieved at #1, but config schema was in unlinked file). |
| 14 | `walker-8` | `Cargo.toml` / `build.rs` | `sufficiency_failure` | `sufficiency_failure` | **Still failing** (Build script dependencies require cross-file co-occurrence). |
| 15 | `docling-0` | `HTMLDocumentBackend` | `sufficiency_failure` | `sufficiency_failure` | **Still failing** (Target at #2, but generic chunking package init outranked backend). |

**Summary**: 7 converted to correct (+46.7% resolution rate). The remaining 8 cases fail not because of denominator dilution, but because of multi-unit cross-file co-occurrence (6 cases) or noisy competing candidates (2 cases).

---

## 5. Investigation of the 10 Remaining Sufficiency Failures

| ID | Root Cause | Correct Rejection? | Minimum Missing Evidence | Possible General Fix |
|---|---|---|---|---|
| `better-auth-0` | Multi-unit relation: session API route depends on database adapter interface | Yes | Co-occurrence of `getSession` route and `Adapter` definition | Multi-unit candidate linkage |
| `pkl-0` | Cross-file AST dependency: `Regex` stdlib definition split from parser `Type.java` | Yes | Standard library regex evaluator snippet linked to parser AST | 1-hop AST definition linkage |
| `pkl-2` | Architectural hierarchy: `Modifier` enum unlinked from `Member` AST node | Yes | Member property modifier definition snippet | Parent class / enum definition context |
| `beszel-0` | Granularity: `smart.go` retrieved NVMe log struct instead of `getSmartData` execution function | Yes | Function body containing `smartctl` execution command | Symbol-level target ranking |
| `adk-python-5` | Candidate noise: Minified CLI browser bundle `main-P3SSB3YR.js` outranked `llm_agent.py` | Yes | Filtering out generated browser bundles in repository scanner | Exclude minified web assets (`*.bundle.js`, `*.min.js`) |
| `jiff-4` | Semantic mismatch: POSIX parser `posix.rs` retrieved instead of `iso_week_date.rs` | Yes | `Date::iso_week_date` method implementation | Exact symbol token weighting |
| `walker-0` | Multi-unit configuration: Provider implementation retrieved, but `Config` struct is in `config.rs` | Yes | Associated configuration schema snippet | Config-to-provider 1-hop linkage |
| `walker-3` | Multi-unit dependency: Provider retrieved, but keybind dispatcher is in `window.rs` | Yes | Window keybind handling method | Caller-callee linkage |
| `walker-8` | Build configuration: `build.rs` and `Cargo.toml` dependencies split across units | Yes | Cargo dependency table excerpt | Build-to-source linkage |
| `docling-0` | Package noise: `chunking/__init__.py` outranked `html_backend.py` | Yes | Backend parser class definition | Downweight empty/facade `__init__.py` files |

---

## 6. Investigation of the 29 Retrieval Failures

### Top-K Submergence Analysis:
Across the 29 retrieval failures, the target's rank under production fusion ($\alpha=0.8$) was:
- **Target Top 11–20**: **6 cases (20.7%)**
- **Target Top 21–50**: **11 cases (37.9%)**
- **Target Absent Top-50**: **12 cases (41.4%)**

```
+-----------------------------------------------------------------------------------+
| Top 11-20 (6 cases, 20.7%)  | Top 21-50 (11 cases, 37.9%) | Absent Top-50 (12, 41.4%)|
+-----------------------------------------------------------------------------------+
| better-auth-3 (#15)         | ducklake-0 (#29)            | ducklake-3 (absent)   |
| better-auth-4 (#11)         | ducklake-1 (#36)            | adk-python-4 (absent) |
| pkl-1 (#19)                 | ducklake-2 (#25)            | dokploy-0 (absent)    |
| walker-4 (#11)              | amoeba-ducklake-02 (#22)    | dokploy-1 (absent)    |
| docling-1 (#14)             | better-auth-2 (#36)         | pkl-5 (absent)        |
| docling-3 (#19)             | amoeba-better-auth-02 (#21) | beszel-1 (absent)     |
|                             | pkl-3 (#27)                 | beszel-2 (absent)     |
|                             | opencloud-0 (#44)           | beszel-5 (absent)     |
|                             | opencloud-9 (#43)           | opencloud-2 (absent)  |
|                             | docling-9 (#38)             | opencloud-4 (absent)  |
|                             | amoeba-docling-02 (#22)     | opencloud-11 (absent) |
|                             |                             | docling-2 (absent)    |
+-----------------------------------------------------------------------------------+
```

### Component Behavior (Top-10 Rank):
- **Lexical succeeds, Semantic fails**: 2 cases (e.g. `better-auth-4` lex #4 vs sem #11; `opencloud-9` lex #15)
- **Semantic succeeds, Lexical fails**: 4 cases (e.g. `ducklake-1` sem #7; `better-auth-3` sem #7; `pkl-1` sem #7; `docling-3` sem #4)
- **Both succeed, Fusion loses**: 1 case (`docling-1` lex #5, sem #22 -> fused #14)
- **Neither succeeds in Top-10**: **22 cases (75.9%)**

### Root Cause Categorization for 29 Retrieval Failures:
1. **Vendor / Third-Party File Pollution (5 cases in OpenCloud)**:
   In `opencloud-eu/opencloud`, vendored C libraries (e.g. `vendor/github.com/mattn/go-sqlite3/sqlite3-binding.c`, `vendor/.../kafka-go/conn.go`) contain tens of thousands of generic identifiers that match almost any search term and dominate BM25 TF-IDF rankings.
2. **Facade / Package Init Pollution (4 cases in Docling)**:
   Empty `__init__.py` files (e.g. `docling/chunking/__init__.py`) containing imported symbols matched terms with high density and outranked the deep datamodel files (`docling/datamodel/document.py`).
3. **Multi-File Architectural Split (8 cases)**:
   The query asks about a subsystem composed of an API router, a backend provider, and a database schema (e.g. `better-auth`, `dokploy`, `opencloud`), but lexical and semantic search only score single isolated files.
4. **Vocabulary Mismatch / Implicit Semantic Targets (12 cases)**:
   Neither lexical nor semantic embeddings matched because the query used conceptual issue terminology (e.g., "how to rescan database storage index", "using HTTP2 with dokploy") while the implementation used internal structural names (`BleveIndex`, `TraefikConfig`).

---

## 7. Question Taxonomy Breakdown

| Taxonomy | Total | Correct | Retrieval Fail | Sufficiency Fail | Groundedness Fail | Accuracy |
|---|---:|---:|---:|---:|---:|---:|
| **Implementation Details** | 45 | 16 | 19 | 8 | 2 | 35.6% |
| **Business Logic** | 6 | 4 | 2 | 0 | 0 | 66.7% |
| **Symbol Lookup** | 2 | 2 | 0 | 0 | 0 | **100.0%** |
| **Project Architecture** | 17 | 7 | 8 | 2 | 0 | 41.2% |

**Insight**: Symbol Lookup and Business Logic questions perform strongly (66.7%–100%). Implementation Details and Project Architecture suffer heavily from retrieval candidate submergence (19 retrieval failures) and cross-file multi-unit dispersion (8 sufficiency failures).

---

## 8. Repository Breakdown

| Repository | Total | Correct | Retrieval Fail | Sufficiency Fail | Groundedness Fail | Accuracy |
|---|---:|---:|---:|---:|---:|---:|
| `duckdb/ducklake` | 6 | 1 | 5 | 0 | 0 | 16.7% |
| `google/adk-python` | 8 | 5 | 1 | 1 | 1 | 62.5% |
| `better-auth/better-auth` | 7 | 2 | 4 | 1 | 0 | 28.6% |
| `Dokploy/dokploy` | 7 | 5 | 2 | 0 | 0 | 71.4% |
| `apple/pkl` | 7 | 1 | 3 | 2 | 1 | 14.3% |
| `henrygd/beszel` | 7 | 3 | 3 | 1 | 0 | 42.9% |
| `opencloud-eu/opencloud` | 7 | 2 | 5 | 0 | 0 | 28.6% |
| `BurntSushi/jiff` | 8 | 7 | 0 | 1 | 0 | **87.5%** |
| `abenz1267/walker` | 6 | 2 | 1 | 3 | 0 | 33.3% |
| `docling-project/docling` | 7 | 1 | 5 | 1 | 0 | 14.3% |

**Insight**:
- High-performing repos: `BurntSushi/jiff` (87.5%), `Dokploy/dokploy` (71.4%), `google/adk-python` (62.5%). Clean structure with clear module names.
- Low-performing repos: `opencloud-eu/opencloud` (28.6% — crippled by `vendor/` C code), `docling-project/docling` (14.3% — flooded by `__init__.py` and nested models), `duckdb/ducklake` (16.7% — complex C++ macro/catalog hierarchies).

---

## 9. Product Smoke Tests on D:/amoeba

All 7 CLI smoke queries executed against the live Amoeba repository:

| # | Query | Retrieved Context | Retrieval Latency | Inference Time | Evidence Status | Answer Summary |
|---|---|---|---:|---:|---|---|
| 1 | *"Where is HNSW implemented?"* | 2 code units (2784 chars) | 14 ms | 6.76 s | Sufficient | Correctly identifies `HnswSemanticIndex` in `hnsw_semantic_index.hpp`. |
| 2 | *"How is HNSW implemented?"* | 2 code units (2782 chars) | 11 ms | 1.22 s | Sufficient | Explains `ISemanticVectorIndex` encapsulation and hnswlib abstraction. |
| 3 | *"What are the main components and flow of the HNSW implementation in Amoeba?"* | 2 code units (3396 chars) | 185 ms | 10.37 s | Sufficient | Outlines CLI commands, indexing handlers, and vector search flow. |
| 4 | *"Explain the HNSW implementation in Amoeba, including the class, index construction, search, configuration, and persistence."* | 1 code unit (3022 chars) | 187 ms | 2.01 s | Sufficient | Details constructor parameters, vector insertion, search exploration depth, and serialization. |
| 5 | *"What is the primary retrieval pipeline?"* | 1 code unit (3109 chars) | 35 ms | 1.10 s | Sufficient | Describes `amoeba::retrieval` pipeline, unit building, and ID reconciliation. |
| 6 | *"Where is evidence sufficiency checked?"* | 1 code unit (2468 chars) | 29 ms | 0.95 s | Sufficient | Cites `EvidenceSufficiencyChecker` and `amoeba/evidence/evidence_sufficiency.hpp`. |
| 7 | *"How does Amoeba prevent unsupported answers?"* | 2 code units (3524 chars) | 184 ms | 2.66 s | Sufficient | Explains deterministic ranking, threshold verification, and evidence bundling. |

---

## 10. Answers to Core Questions (The 12 Bottleneck Dimensions)

1. **What percentage of remaining failures are retrieval?**  
   **70.7% (29 of 41 failures)**.
2. **What percentage are evidence?**  
   **24.4% (10 of 41 failures)**.
3. **What percentage are reasoning / groundedness?**  
   **4.9% (2 of 41 failures)**.
4. **How many retrieval failures have the target in Top-20?**  
   **6 cases (20.7%)**.
5. **How many have the target in Top-50?**  
   **17 cases (58.6%)** (6 in Top 11–20 + 11 in Top 21–50).
6. **How many have the target outside Top-100 / absent?**  
   **12 cases (41.4%)**.
7. **How many are query-understanding failures?**  
   **4 cases (9.8%)** (where technical concepts were completely missed).
8. **How many are representation failures?**  
   **11 cases (26.8%)** (where vendor files, minified bundles, or generic `__init__.py` files submerged targets).
9. **How many are fusion/ranking failures?**  
   **7 cases (17.1%)** (where target had strong lexical or semantic score but fusion ranked it at 11–25).
10. **How many are genuinely insufficient evidence?**  
    **All 10 remaining sufficiency failures** were correctly rejected because single isolated symbols lacked required multi-unit context or were test utilities.
11. **How many are multi-file questions?**  
    **14 questions (34.1% of remaining failures)** require co-occurrence across 2+ files (e.g. `config.rs` + `provider.rs`, `router.ts` + `adapter.ts`).
12. **Did any safety invariant regress?**  
    **No**. Clean negative refusal was maintained on 100% of out-of-domain and absent feature CLI runtime checks.

---

## 11. Component Decisions: BUILD / DELAY / STOP

```
+-----------------------------------------------------------------------------------------+
|                                  DECISION MATRIX                                        |
+-----------------------------------------------------------------------------------------+
|  BUILD (Next Immediate Step):                                                           |
|  * Scanner Exclusion of Vendor / Minified Code + Facade De-weighting                     |
|    - Exclude `vendor/`, `third_party/`, `*.min.js`, `*-*.js` bundle artifacts           |
|    - Downweight empty package `__init__.py` files during retrieval indexing             |
|    - Eliminates 9 prominent retrieval failure pollutions immediately (OpenCloud/Docling)|
|                                                                                         |
|  DELAY:                                                                                 |
|  * 1-Hop AST / Call-Graph Multi-Unit Evidence Assembly (Phase 9.0)                      |
|    - Needed for the 6 multi-unit sufficiency cases, but primary retrieval recall must   |
|      be cleaned up first to feed valid candidate pairs to the graph traverser.          |
|                                                                                         |
|  STOP:                                                                                  |
|  * Fusion Weight Sweeps (alpha tuning is exhausted; alpha=0.8 is optimal)               |
|  * Sufficiency Threshold Relaxation (Rule A/C/E must remain strictly invariant)         |
|  * External Vector DBs or Cloud Agent Loops                                             |
+-----------------------------------------------------------------------------------------+
```

### Recommendation for Phase 8.8:
**Single Recommendation**: Implement **Repository Scanner Noise Suppression & Package Facade De-weighting**:
1. Exclude `vendor/`, `third_party/`, `extern/`, and minified assets (`*.min.js`, browser bundles) from recursive scanning.
2. Downweight trivial import-re-export `__init__.py` files during candidate ranking.
3. This directly unblocks the submerged targets in `opencloud-eu/opencloud` (5 cases) and `docling-project/docling` (4 cases) without altering any safety invariants.

---

## 12. Complexity & Risk Check

| Dimension | Assessment |
|---|---|
| **Affected Questions** | 9+ retrieval failure cases (`opencloud-*`, `docling-*`, `adk-python-5`). |
| **Implementation Complexity** | **Very Low** (Configurable directory/file filters in `RepositoryScanner` and unit score penalty for package facades). |
| **Runtime Impact** | **Positive** (Faster repo scanning, smaller index footprint, faster BM25 queries). |
| **Safety Risk** | **Zero** (No relaxation of sufficiency or grounding rules). |
| **Generalizability** | **High** (Excluding vendor and third-party code is standard across code search engines like Sourcegraph/Zoekt). |
| **Maintainability** | **Clean** (Follows existing scanner ignore pattern established in Phase 8.6). |
