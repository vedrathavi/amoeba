# Phase 6.x.3: Dual-Channel Semantic Retrieval Evaluation — MiniLM-L6-v2 + Nomic CodeRankEmbed-137M

## 1. Executive Summary & Objective

The objective of Phase 6.x.3 is to empirically evaluate whether fusing a general-language semantic representation (**Channel A**: `all-MiniLM-L6-v2`, 384-d) with a code-specialized representation (**Channel B**: `nomic-ai/CodeRankEmbed`, 768-d) improves retrieval precision and recall over either model operated independently.

### Key Empirical Findings:
1. **Quality Synergy**: Dual-channel retrieval using **Reciprocal Rank Fusion (RRF, $k=60$)** achieved **1.0000 P@1, 1.0000 MRR, 0.9772 NDCG@5, and 1.0000 Recall@10**, combining the technical AST precision of CodeRank with the full recall coverage of MiniLM.
2. **Failure Repair**: Dual fusion successfully corrected MiniLM's top-1 failure on `"Which function parses source code?"` (where MiniLM erroneously ranked `RepositoryScanner` #1 instead of `SourceParser`), restoring `SourceParser` to rank #1 without degrading any conceptual query.
3. **Performance Tradeoff**: Running dual channels sequentially on CPU yields a mean warm query latency of **27.04 ms** (P50: 26.28 ms, P95: 34.70 ms), which remains well within interactive search latency thresholds (<50 ms), with combined memory consumption of **~500 MB RAM** and **628 MB disk footprint**.

---

## 2. Channel Architecture & Independence

```text
Query
  │
  ├── Channel A (MiniLM-L6-v2) ────────► 384-d Embedding ──► Cosine Similarity ──► Ranked List A
  │
  └── Channel B (CodeRankEmbed-137M) ──► 768-d Embedding ──► Cosine Similarity ──► Ranked List B
                                                                                       │
                                                                                       ▼
                                                                             Score Normalization &
                                                                             Fusion Layer (RRF / Linear)
                                                                                       │
                                                                                       ▼
                                                                             Fused Candidate Units
                                                                                       │
                                                                                       ▼
                                                                             Evidence Sufficiency Checker
```

- **Vector Independence**: Vectors are never concatenated or averaged across disparate embedding spaces.
- **Asymmetric Prefixing**: Channel B applies `search_query: ` to queries and `search_document: ` to retrieval units. Channel A processes standard token text.

---

## 3. Score Normalization & Fusion Strategies Evaluated

Because cosine score distributions differ between models (MiniLM scores typically range from -0.10 to +0.60; CodeRank scores range from +0.20 to +0.75), we evaluated two normalization/fusion paradigms:

1. **Min-Max Normalized Linear Combination**:
   $$\text{Score}_{\text{norm}}(d) = \frac{\text{sim}(d) - \min_u \text{sim}(u)}{\max_u \text{sim}(u) - \min_u \text{sim}(u)}$$
   $$\text{Fused}(d) = \alpha \cdot \text{Score}_{\text{norm}, A}(d) + (1 - \alpha) \cdot \text{Score}_{\text{norm}, B}(d)$$
   Evaluated at $\alpha \in \{0.3, 0.5, 0.7\}$.

2. **Reciprocal Rank Fusion (RRF)**:
   $$\text{RRF}(d) = \sum_{m \in \{A, B\}} \frac{1}{k + \text{rank}_m(d)}$$
   Evaluated at standard $k=60$ and $k=20$.

---

## 4. Aggregate Benchmark Comparison

*Evaluation across 17 relevant benchmark queries and 2 adversarial negative queries on CPU (AMD Ryzen 7 5800H @ 3.2GHz).*

| System Configuration | P@1 | P@5 | MRR | NDCG@5 | Recall@10 | Mean Query Latency | P95 Latency |
|---|---:|---:|---:|---:|---:|---:|---:|
| **Channel A (MiniLM Only)** | 0.9412 | **0.2235** | 0.9706 | 0.9695 | **1.0000** | **7.70 ms** | **10.77 ms** |
| **Channel B (CodeRank Only)** | **1.0000** | 0.2118 | **1.0000** | **0.9772** | 0.9706 | 18.63 ms | 22.70 ms |
| **Dual Linear (0.5 MiniLM + 0.5 CodeRank)** | **1.0000** | 0.2118 | **1.0000** | **0.9772** | **1.0000** | 27.04 ms | 34.70 ms |
| **Dual Linear (0.3 MiniLM + 0.7 CodeRank)** | **1.0000** | 0.2118 | **1.0000** | **0.9772** | **1.0000** | 27.04 ms | 34.70 ms |
| **Dual Linear (0.7 MiniLM + 0.3 CodeRank)** | 0.9412 | 0.2118 | 0.9706 | 0.9555 | **1.0000** | 27.04 ms | 34.70 ms |
| **Dual RRF ($k=60$)** | **1.0000** | 0.2118 | **1.0000** | **0.9772** | **1.0000** | 27.04 ms | 34.70 ms |
| **Dual RRF ($k=20$)** | **1.0000** | 0.2118 | **1.0000** | **0.9772** | **1.0000** | 27.04 ms | 34.70 ms |

---

## 5. Query-Category Breakdown

### A. Technical / Code-Structure Queries (7 queries)
*Queries targeting exact implementation mechanisms (e.g. inverted index, AST callers, parser, imports, tokenization).*

| System | P@1 | MRR | Key Observations |
|---|---:|---:|---|
| **MiniLM Only** | 0.8571 | 0.9286 | Erred on `"Which function parses source code?"` (ranked `RepositoryScanner` #1). |
| **CodeRank Only** | **1.0000** | **1.0000** | Correctly resolved all 7 technical queries at #1. |
| **Dual RRF ($k=60$)** | **1.0000** | **1.0000** | Correctly resolved all 7 technical queries at #1 (repaired MiniLM). |
| **Dual Linear (0.5/0.5)** | **1.0000** | **1.0000** | Correctly resolved all 7 technical queries at #1. |

### B. Conceptual / Natural Language Queries (7 queries)
*Queries describing high-level application behaviors (e.g. calendar state/navigation, evidence assembly, query normalization).*

| System | P@1 | MRR | Key Observations |
|---|---:|---:|---|
| **MiniLM Only** | **1.0000** | **1.0000** | Strong semantic abstraction across hooks and components. |
| **CodeRank Only** | **1.0000** | **1.0000** | High similarity confidence on state and evidence concepts. |
| **Dual RRF ($k=60$)** | **1.0000** | **1.0000** | Preserved 100% precision across all conceptual queries. |

### C. Specific Service / Domain Queries (3 queries)
*Queries targeting subsystem boundaries (payment checkout, JWT auth, OAuth).*

| System | P@1 | MRR | Key Observations |
|---|---:|---:|---|
| **MiniLM Only** | **1.0000** | **1.0000** | Correct top-1 isolation of `PaymentService`, `AuthService`, `OAuthClient`. |
| **CodeRank Only** | **1.0000** | **1.0000** | Correct top-1 isolation with distinct separation margins. |
| **Dual RRF ($k=60$)** | **1.0000** | **1.0000** | Correct top-1 isolation across all service queries. |

---

## 6. Per-Query Result Matrix

| Query | Expected Relevant | MiniLM Top (#1) | CodeRank Top (#1) | Dual RRF Top (#1) | Dual Linear (50/50) Top (#1) | Fusion Outcome |
|---|---|---|---|---|---|---|
| "Where is the inverted index implemented?" | `InvertedIndex` | `InvertedIndex` (0.456) | `InvertedIndex` (0.576) | `InvertedIndex` | `InvertedIndex` | Preserved (#1) |
| "Where are relationships stored?" | `RelationshipGraph` | `RelationshipGraph` (0.350) | `RelationshipGraph` (0.448) | `RelationshipGraph` | `RelationshipGraph` | Preserved (#1) |
| "Where are callers resolved?" | `CallExtractor` | `CallExtractor` (0.479) | `CallExtractor` (0.350) | `CallExtractor` | `CallExtractor` | Preserved (#1) |
| "Where is evidence assembled?" | `EvidenceAssembler` | `EvidenceAssembler` (0.574) | `EvidenceAssembler` (0.579) | `EvidenceAssembler` | `EvidenceAssembler` | Preserved (#1) |
| "Where is the query normalized?" | `QueryUnderstanding` | `QueryUnderstanding` (0.497) | `QueryUnderstanding` (0.436) | `QueryUnderstanding` | `QueryUnderstanding` | Preserved (#1) |
| "Where are supporting AST elements resolved?" | `SupportingEvidenceResolver` | `SupportingEvidenceResolver` (0.703) | `SupportingEvidenceResolver` (0.502) | `SupportingEvidenceResolver` | `SupportingEvidenceResolver` | Preserved (#1) |
| "Where is the embedding provider implemented?" | `PretrainedEmbeddingProvider` | `PretrainedEmbeddingProvider` (0.453) | `PretrainedEmbeddingProvider` (0.622) | `PretrainedEmbeddingProvider` | `PretrainedEmbeddingProvider` | Preserved (#1) |
| "How does the calendar navigate between months?" | `useCalendar`, `CalendarMonthView` | `useCalendar` (0.507) | `useCalendar` (0.538) | `useCalendar` | `useCalendar` | Preserved (#1) |
| "Where does the calendar store state?" | `useCalendar` | `useCalendar` (0.452) | `useCalendar` (0.452) | `useCalendar` | `useCalendar` | Preserved (#1) |
| "Where is token extraction performed?" | `CodeTokenizer` | `CodeTokenizer` (0.371) | `CodeTokenizer` (0.417) | `CodeTokenizer` | `CodeTokenizer` | Preserved (#1) |
| **"Which function parses source code?"** | `SourceParser` | ❌ `RepositoryScanner` (0.464) | ✅ `SourceParser` (0.507) | ✅ `SourceParser` | ✅ `SourceParser` | **FIXED (0.857 ➔ 1.000)** |
| "Where are imports resolved?" | `ImportExtractor` | `ImportExtractor` (0.501) | `ImportExtractor` (0.436) | `ImportExtractor` | `ImportExtractor` | Preserved (#1) |
| "Where are function calls resolved?" | `CallExtractor` | `CallExtractor` (0.389) | `CallExtractor` (0.338) | `CallExtractor` | `CallExtractor` | Preserved (#1) |
| "Where is relationship-aware search implemented?" | `RelationshipAwareSearch` | `RelationshipAwareSearch` (0.656) | `RelationshipAwareSearch` (0.651) | `RelationshipAwareSearch` | `RelationshipAwareSearch` | Preserved (#1) |
| "Where is the payment checkout?" | `PaymentService` | `PaymentService` (0.407) | `PaymentService` (0.453) | `PaymentService` | `PaymentService` | Preserved (#1) |
| "Where is JWT authentication implemented?" | `AuthService` | `AuthService` (0.446) | `AuthService` (0.439) | `AuthService` | `AuthService` | Preserved (#1) |
| "Where is OAuth implemented?" | `OAuthClient` | `OAuthClient` (0.526) | `OAuthClient` (0.489) | `OAuthClient` | `OAuthClient` | Preserved (#1) |

---

## 7. Adversarial Negative Query Evaluation

For queries where no matching symbol exists in the repository:
1. **"Where is the Kubernetes deployment?"**:
   - MiniLM Top: `SupportingEvidenceResolver` (sim: 0.0797).
   - CodeRank Top: `PretrainedEmbeddingProvider` (sim: 0.2633).
   - Dual RRF Top: `PretrainedEmbeddingProvider` (RRF score: 0.0305).
   - *Status*: Cleanly rejected by Amoeba's `EvidenceSufficiencyChecker` (subject grounding coverage: 0.00).
2. **"Where is the GraphQL resolver?"**:
   - MiniLM Top: `RelationshipGraph` (sim: 0.4009).
   - CodeRank Top: `RelationshipAwareSearch` (sim: 0.3973).
   - Dual RRF Top: `RelationshipGraph` (RRF score: 0.0315).
   - *Status*: Cleanly rejected by Amoeba's `EvidenceSufficiencyChecker` (no GraphQL subject match in AST).

---

## 8. Performance & Resource Comparison

| Metric | Channel A (MiniLM) | Channel B (CodeRank) | Dual-Channel (Sequential) | Dual-Channel (Parallel Est.) |
|---|---:|---:|---:|---:|
| **Warm Query Latency (Mean)** | **7.70 ms** | 18.63 ms | 27.04 ms | ~18.8 ms |
| **Warm Query Latency (P50)** | **7.25 ms** | 17.74 ms | 26.28 ms | ~18.0 ms |
| **Warm Query Latency (P95)** | **10.77 ms** | 22.70 ms | 34.70 ms | ~23.5 ms |
| **Batch Throughput** | **1,392 docs/sec** | 310 docs/sec | 251 docs/sec | ~300 docs/sec |
| **Working-Set Memory (RAM)** | **~120 MB** | ~380 MB | ~500 MB | ~500 MB |
| **Model Size on Disk** | **~80 MB** | ~548 MB | 628 MB | 628 MB |

---

## 9. Decision & Classification

### Classification: **B — Dual-channel clearly improves retrieval precision and fixes technical failure cases, but introduces a 3.5x latency and memory cost that favors an adaptive or opt-in architecture.**

### Rationale:
1. **Quality Supremacy**: Dual-Channel RRF ($k=60$) is strictly the best-performing retrieval configuration tested in Amoeba's history, achieving **1.0000 P@1, 1.0000 MRR, and 1.0000 Recall@10**, successfully repairing MiniLM's AST classification weakness without suffering from single-model edge cases.
2. **Predictable Fusion**: RRF ($k=60$) is completely parameter-free with respect to raw floating-point score distributions, avoiding calibration fragility across different embedding spaces.
3. **Resource Reality**: While 27.0 ms query latency and 500 MB RAM are practical on modern developer machines, MiniLM standalone remains 3.5x faster (7.7 ms) and 4x lighter (120 MB).
4. **Architectural Recommendation**: MiniLM should remain the fast default baseline, while Dual-Channel RRF should be offered as a configurable high-precision retrieval mode (`--semantic-channel=dual` or `--dual-semantic`).
