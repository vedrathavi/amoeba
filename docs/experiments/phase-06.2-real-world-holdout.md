# Phase 6.2 — Real-World Holdout Evaluation Report

**REAL-WORLD EXTERNAL HOLDOUT — FROZEN VALIDATION EXPERIMENT**

---

## 1. Executive Summary & Purpose

The purpose of this experiment is to determine whether the retrieval behavior observed on the controlled Phase 6.2 synthetic benchmark generalizes to a real-world frontend codebase.

In accordance with strict experimental protocols:
* The target repository ([`demo_test_projects/calendar`](file:///d:/amoeba/demo_test_projects/calendar)) was pre-inspected and frozen before Phase 6.2 evaluation.
* **Zero tuning** was performed: ranking weights, alpha parameters, candidate pool sizes, normalization schemes, and retrieval heuristics were **not modified**.
* The 10 candidate evaluation queries formulated during holdout inspection were executed verbatim without alterations or exclusions.

### Key Finding
> [!IMPORTANT]
> **Generalization Reality Check**:
> On the controlled 7-element benchmark, P@1 ranged from 0.90 to 1.00.
> On the real-world 2,820-element AST corpus, Top-1 precision drops sharply across all methods (P@1: **0.10 – 0.20**).
> 
> This is primarily driven by **AST sub-element distractor proliferation**: fine-grained elements (1,280 Tailwind utility classes, 511 JSX attributes, 322 function calls, 90 ES imports) frequently outscore top-level components and functions due to token density and exact subword matches.

---

## 2. Holdout Repository & Dataset Description

* **Repository**: [`demo_test_projects/calendar`](file:///d:/amoeba/demo_test_projects/calendar)
* **Application Framework**: Next.js 15+ (App Router), React 19, TypeScript, Tailwind CSS v4, Lucide React, Framer Motion, date-fns.
* **Corpus Scale**:
  * Total files: 70 files (excluding uninstalled `node_modules`).
  * Source files indexed: **27 source files** (18 `.tsx`, 8 `.ts`, 1 `.css`).
  * Total CodeElements: **2,820 elements**.
  * Term Vocabulary: **2,255 unique dictionary terms**.
  * Inverted Postings: **50,471 postings**.
  * Structural Graph: **193 nodes, 236 directed relationships**.
* **Embedding Model**: Local `all-MiniLM-L6-v2` (384-dimensional dense vectors, normalized cosine similarity).

---

## 3. Frozen Evaluation Query Set (10 Queries)

| Query ID | Query String | Category | Expected Relevant Symbol & File | Rationale |
| :-: | :--- | :--- | :--- | :--- |
| **Q1** | `CalendarGrid` | Exact Identifier | [`CalendarGrid`](file:///d:/amoeba/demo_test_projects/calendar/src/components/calendar/CalendarGrid.tsx) (`Component`) | Exact match for monthly 7-column calendar matrix. |
| **Q2** | `use calendar` | Normalized Identifier | [`useCalendar`](file:///d:/amoeba/demo_test_projects/calendar/src/hooks/useCalendar.ts) (`Hook`) | Space-separated normalized query for custom date hook. |
| **Q3** | `LocalStorage` | Partial / Subword | [`useLocalStorage`](file:///d:/amoeba/demo_test_projects/calendar/src/hooks/useLocalStorage.ts) (`Hook`) | Subword search for client-side storage persistence hook. |
| **Q4** | `FloatingToolbar action` | Multi-Term | [`FloatingToolbar`](file:///d:/amoeba/demo_test_projects/calendar/src/components/floating-toolbar/FloatingToolbar.tsx) (`Component`) | Multi-term query for floating action bar and dispatch handlers. |
| **Q5** | `formatDate formatStr` | Contextual Lexical | [`formatDate`](file:///d:/amoeba/demo_test_projects/calendar/src/lib/dateUtils.ts) (`Function`) | Function identifier combined with signature parameter. |
| **Q6** | `RootLayout Next.js metadata` | Framework | [`RootLayout`](file:///d:/amoeba/demo_test_projects/calendar/src/app/layout.tsx) (`Component`, `Route`) | Next.js App Router root layout managing global metadata. |
| **Q7** | `Dialog` | Ambiguous | [`Dialog`](file:///d:/amoeba/demo_test_projects/calendar/src/components/ui/dialog.tsx) (`Component`, `JSXComponent`) | Ambiguous query spanning modal primitive and page call sites. |
| **Q8** | `Where is the location destination photo and travel description rendered for each month?` | Conceptual / Semantic | [`ImagePanel`](file:///d:/amoeba/demo_test_projects/calendar/src/components/calendar/ImagePanel.tsx) (`Component`) / [`getImagePanelData`](file:///d:/amoeba/demo_test_projects/calendar/src/lib/monthLocationData.ts) (`Function`) | Natural language question targeting landscape photo showcase without explicit component names. |
| **Q9** | `MonthLocation getImagePanelData` | Cross-File | [`MonthLocation`](file:///d:/amoeba/demo_test_projects/calendar/src/lib/monthLocationData.ts) (`Interface`), [`getImagePanelData`](file:///d:/amoeba/demo_test_projects/calendar/src/lib/monthLocationData.ts) (`Function`) | Cross-file data contract connecting destination datasets with UI presentation. |
| **Q10** | `CalendarDay rendered inside CalendarGrid` | Component Relationship | [`CalendarDay`](file:///d:/amoeba/demo_test_projects/calendar/src/components/calendar/CalendarDay.tsx) (`Component`), [`CalendarGrid`](file:///d:/amoeba/demo_test_projects/calendar/src/components/calendar/CalendarGrid.tsx) (`Component`) | Structural parent-child React relationship mapping date cells. |

---

## 4. Evaluated Methods

1. **Baseline Lexical**: Term match count over inverted index.
2. **BM25**: Probabilistic term-frequency / inverse-document-frequency ranker ($k_1 = 1.2, b = 0.75$).
3. **CodeAware**: Field-weighted lexical ranker (name: 3.0, context: 1.5, file: 2.0, detail: 1.0).
4. **Semantic (`all-MiniLM-L6-v2`)**: Dense embedding cosine similarity over `SemanticTextFormatter` Representation A.
5. **Hybrid ($\alpha=0.5$ Weighted)**: Balanced linear combination of Min-Max normalized CodeAware and Semantic scores.
6. **Hybrid ($\alpha=0.3$ Weighted)**: Semantic-leaning linear combination.
7. **Hybrid RRF ($k=60$)**: Reciprocal Rank Fusion of lexical and semantic candidate top-20 pools.

---

## 5. Aggregate Experimental Results

> [!NOTE]
> Because this evaluation suite contains $N=10$ queries, these metrics are **descriptive indicators of holdout behavior**, not statistically significant claims.

| Method | P@1 | P@3 | P@5 | Recall@5 | Recall@10 | MRR | NDCG@5 | NDCG@10 | Query Latency |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Baseline Lexical** | 0.10 | 0.100 | 0.100 | 0.300 | 0.433 | 0.249 | 0.195 | 0.248 | $0.35\text{ ms}$ |
| **BM25** | **0.20** | 0.133 | 0.100 | 0.317 | **0.533** | **0.359** | **0.269** | **0.361** | $0.32\text{ ms}$ |
| **CodeAware** | 0.10 | 0.100 | 0.080 | 0.267 | **0.533** | 0.253 | 0.178 | 0.290 | $0.24\text{ ms}$ |
| **Semantic (MiniLM)** | **0.20** | 0.133 | 0.080 | 0.233 | 0.300 | 0.319 | 0.210 | 0.241 | $12.94\text{ ms}$ |
| **Hybrid ($\alpha=0.5$)** | 0.10 | 0.133 | **0.120** | **0.367** | 0.400 | 0.276 | 0.236 | 0.251 | $13.21\text{ ms}$ |
| **Hybrid ($\alpha=0.3$)** | 0.10 | 0.133 | 0.080 | 0.233 | 0.300 | 0.277 | 0.187 | 0.217 | $13.20\text{ ms}$ |
| **Hybrid (RRF $k=60$)** | 0.10 | 0.100 | 0.080 | 0.283 | 0.400 | 0.244 | 0.179 | 0.230 | $13.21\text{ ms}$ |

---

## 6. Granular Per-Query Results & Human Relevance Inspection

Every top returned element was individually inspected and classified into:
* **CORRECT**: Exact target symbol (component, function, hook, or route) returned as Rank 1.
* **PARTIALLY_RELEVANT**: An inner member, sub-call, prop attribute, or interface within the correct file/symbol returned instead of the top-level declaration.
* **INCORRECT**: An unrelated element returned.

| Query ID | Query String | Category | Method | Top-1 Returned Element | Kind | File | Target Rank | Relevance |
| :-: | :--- | :--- | :--- | :--- | :--- | :--- | :---: | :--- |
| **Q1** | `CalendarGrid` | Exact Identifier | **BM25** | `CalendarGridProps` | Interface | `CalendarGrid.tsx` | 6 | **PARTIALLY_RELEVANT** |
| | | | **CodeAware** | `className` | Attribute | `CalendarGrid.tsx` | 5 | **PARTIALLY_RELEVANT** |
| | | | **Semantic** | `isRangeEnd` | Attribute | `CalendarGrid.tsx` | 19 | **PARTIALLY_RELEVANT** |
| | | | **Hybrid 0.5** | `className` | Attribute | `CalendarGrid.tsx` | 13 | **PARTIALLY_RELEVANT** |
| **Q2** | `use calendar` | Normalized Identifier | **BM25** | `useState` | Hook | `useCalendar.ts` | 13 | **PARTIALLY_RELEVANT** |
| | | | **CodeAware** | `import React...` | Include | `page.tsx` | 22 | **INCORRECT** |
| | | | **Semantic** | `[data-month="7"]` | Selector | `globals.css` | 143 | **INCORRECT** |
| | | | **Hybrid 0.5** | `[data-month="7"]` | Selector | `globals.css` | >20 | **INCORRECT** |
| **Q3** | `LocalStorage` | Partial/Subword | **BM25** | `useLocalStorage` | Function | `useLocalStorage.ts` | **1** | **CORRECT** |
| | | | **CodeAware** | `import { useLocalStorage }...` | Include | `page.tsx` | 2 | **INCORRECT** |
| | | | **Semantic** | `import { useLocalStorage }...` | Include | `page.tsx` | 12 | **INCORRECT** |
| | | | **Hybrid 0.5** | `import { useLocalStorage }...` | Include | `page.tsx` | 5 | **INCORRECT** |
| **Q4** | `FloatingToolbar action` | Multi-Term | **BM25** | `onClick` | Attribute | `FloatingToolbar.tsx` | 19 | **PARTIALLY_RELEVANT** |
| | | | **CodeAware** | `import { FloatingToolbar }...` | Include | `page.tsx` | 12 | **INCORRECT** |
| | | | **Semantic** | `onAction` | Call | `FloatingToolbar.tsx` | >20 | **PARTIALLY_RELEVANT** |
| | | | **Hybrid 0.5** | `onAction` | Call | `FloatingToolbar.tsx` | 16 | **PARTIALLY_RELEVANT** |
| **Q5** | `formatDate formatStr` | Contextual Lexical | **BM25** | `format` | Call | `dateUtils.ts` | 2 | **PARTIALLY_RELEVANT** |
| | | | **CodeAware** | `format` | Call | `dateUtils.ts` | 3 | **PARTIALLY_RELEVANT** |
| | | | **Semantic** | `format` | Call | `dateUtils.ts` | 2 | **PARTIALLY_RELEVANT** |
| | | | **Hybrid 0.5** | `format` | Call | `dateUtils.ts` | 2 | **PARTIALLY_RELEVANT** |
| **Q6** | `RootLayout Next.js metadata` | Framework | **All Methods** | `layout.tsx` | Route | `layout.tsx` | **1** | **CORRECT** |
| **Q7** | `Dialog` | Ambiguous | **BM25** | `import { Dialog }...` | Include | `page.tsx` | 27 | **INCORRECT** |
| | | | **Semantic** | `cn` | Call | `dialog.tsx` | >20 | **PARTIALLY_RELEVANT** |
| | | | **Hybrid 0.5** | `import { Dialog }...` | Include | `page.tsx` | >20 | **INCORRECT** |
| **Q8** | `Where is location destination photo...` | Conceptual/Semantic | **Baseline/CA** | `className` | Attribute | `CalendarDay.tsx` | 24 | **INCORRECT** |
| | | | **Semantic** | `getImagePanelData` | Function | `monthLocationData.ts` | **1** | **CORRECT** |
| | | | **Hybrid 0.5** | `getMonth` | Call | `monthLocationData.ts` | 3 | **INCORRECT** |
| **Q9** | `MonthLocation getImagePanelData` | Cross-File | **BM25** | `getMonth` | Call | `monthLocationData.ts` | 2 | **PARTIALLY_RELEVANT** |
| | | | **Semantic** | `getMonth` | Call | `monthLocationData.ts` | 2 | **PARTIALLY_RELEVANT** |
| | | | **Hybrid 0.5** | `getMonth` | Call | `monthLocationData.ts` | 2 | **PARTIALLY_RELEVANT** |
| **Q10** | `CalendarDay rendered inside CalendarGrid` | Component Relationship | **BM25** | `CalendarDay` | JSXComponent | `CalendarGrid.tsx` | 9 | **PARTIALLY_RELEVANT** |
| | | | **Semantic** | `div` | JSXElement | `CalendarGrid.tsx` | 45 | **INCORRECT** |
| | | | **Hybrid 0.5** | `CalendarDay` | JSXComponent | `CalendarGrid.tsx` | 12 | **PARTIALLY_RELEVANT** |

---

## 7. Deep Analysis of Failure Modes & Distractor Proliferation

### 1. The AST Sub-Element Distractor Problem
In synthetic benchmarks, each document is a clean top-level function or class. In real TypeScript/React code:
* `src/components/calendar/CalendarGrid.tsx` contains 91 extracted elements:
  * 1 `Component` (`CalendarGrid`)
  * 1 `Interface` (`CalendarGridProps`)
  * 42 `UtilityClass` elements (`grid`, `grid-cols-7`, `gap-2`, etc.)
  * 28 `Attribute` elements (`className`, `key`, `onClick`, `isRangeEnd`)
  * 12 `JSXElement` / `JSXComponent` instances
* When querying `CalendarGrid`, lexical search finds exact token occurrences inside `className` and `CalendarGridProps`. Because `className` is short, BM25 assigns it a high term saturation score, ranking it *above* the actual React component.

### 2. Import Statement Dominance
* In `src/app/page.tsx`, 90 ES imports are extracted as `Include` elements.
* When querying `LocalStorage` or `Dialog`, the import statement `import { useLocalStorage } from "@/hooks/useLocalStorage";` in `src/app/page.tsx` matches both the symbol name and the file path, outscoring the actual declaration in `src/hooks/useLocalStorage.ts`.

### 3. Semantic Vector Dispersion
* For short keywords like `use calendar`, dense embeddings of CSS selectors (`[data-month="7"]` in `globals.css`) have spurious geometric proximity in 384-d space (cosine similarity ~0.42), overtaking the hook definition whose formatted text contains more verbose structural metadata.
* Conversely, for natural language queries (Q8: `"Where is the location destination photo and travel description rendered for each month?"`), **semantic retrieval performed flawlessly**, placing `getImagePanelData` at Rank 1 with cosine similarity 0.54, while lexical search failed completely (placing it at Rank 24).

---

## 8. Answers to Research Questions (Q1–Q8)

### Q1. Does semantic retrieval generalize beyond the controlled benchmark?
**Yes, in its core strength (conceptual intent), but with noise on fine-grained AST elements**. Semantic retrieval was the **only method** capable of answering Q8 (identifying `getImagePanelData` in `monthLocationData.ts` at Rank 1). However, on short token queries, dense vector similarities become noisy across 2,820 heterogeneous AST nodes.

### Q2. Does hybrid retrieval preserve semantic gains?
**No, not unconditionally on raw AST elements**. On Q8, pure semantic retrieval placed the correct function at Rank 1. However, linear score fusion with lexical search ($\alpha=0.5$ and $\alpha=0.3$) elevated an inner `Call` (`getMonth`) to Rank 1 because lexical tokens matched the call expression, demoting the true target to Rank 3.

### Q3. Does $\alpha=0.3$ generalize, or was it benchmark-specific?
**It was benchmark-specific**. On the controlled 7-element suite, $\alpha=0.3$ achieved P@1=1.00. On the real holdout, $\alpha=0.3$ achieved P@1=0.10 (identical to $\alpha=0.5$). Fine-tuning fusion weights on a tiny synthetic corpus does not produce optimal weights for multi-file AST trees.

### Q4. Does RRF generalize?
**RRF exhibited the same vulnerability** (P@1=0.10, MRR=0.2441). Without element-kind prioritization, RRF fuses ranks of disparate granularities (e.g. ranking an import statement equal to a function definition).

### Q5. Which query categories benefit from semantic retrieval?
**Conceptual and natural-language intent queries** that do not cite literal code identifiers (e.g. Q8).

### Q6. Which categories remain better served by lexical retrieval?
**Subword and exact symbol queries** (e.g. `LocalStorage` $\to$ BM25 finds `useLocalStorage` at Rank 1 in $0.08\text{ ms}$).

### Q7. Does the real project expose parser/indexing limitations?
**Yes, a major architectural finding**: Indexing every single fine-grained AST token (utility classes, attributes, call candidates) without **symbol-level clustering, kind weighting, or parent-container aggregation** creates an overwhelming distractor pool that hurts Top-1 precision for both lexical and semantic methods.

### Q8. Is hybrid retrieval useful enough to continue using as an experimental mode?
**Yes, as an opt-in experimental capability**, but the real-world holdout confirms that hybrid retrieval **must not be made the unconditional default** until Amoeba implements symbol-level aggregation and distractor suppression.

---

## 9. Research Artifacts Catalog

* **Data Files**:
  * [`benchmarks/phase-06.2/holdout/data/real-world-holdout-results.json`](file:///d:/amoeba/benchmarks/phase-06.2/holdout/data/real-world-holdout-results.json)
  * [`benchmarks/phase-06.2/holdout/data/real-world-holdout-results.csv`](file:///d:/amoeba/benchmarks/phase-06.2/holdout/data/real-world-holdout-results.csv)
  * [`benchmarks/phase-06.2/holdout/data/real-world-holdout-per-query.csv`](file:///d:/amoeba/benchmarks/phase-06.2/holdout/data/real-world-holdout-per-query.csv)
* **Plots**:
  * **P6.2-H01**: `benchmarks/phase-06.2/holdout/plots/P6.2-H01-method-quality-comparison.png`
  * **P6.2-H02**: `benchmarks/phase-06.2/holdout/plots/P6.2-H02-per-query-method-comparison.png`
  * **P6.2-H03**: `benchmarks/phase-06.2/holdout/plots/P6.2-H03-category-quality.png`
  * **P6.2-H04**: `benchmarks/phase-06.2/holdout/plots/P6.2-H04-latency-comparison.png`

---

## 10. Final Verification Sign-Off

* **Frozen Queries**: 10/10 queries executed verbatim.
* **Amoeba Engine Code**: Zero modifications.
* **Tuning**: Zero tuning performed.
* **C++ Test Suite**: **209 / 209 tests passing (100%)**.

```text
============================================================
PHASE 6.2 REAL-WORLD HOLDOUT COMPLETE — READY FOR REVIEW
============================================================
```
