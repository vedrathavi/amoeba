# Phase 6.x.2: Code-Specialized Embedding Evaluation — MiniLM-L6-v2 vs Nomic CodeRankEmbed-137M

## 1. Objective
The goal of Phase 6.x.2 is to empirically evaluate whether a specialized code-trained embedding model (`nomic-ai/CodeRankEmbed`) provides materially better code understanding and retrieval quality for Amoeba compared to our current general-language baseline (`all-MiniLM-L6-v2`).

---

## 2. Models Evaluated

| Property | Baseline (MiniLM) | Candidate (CodeRankEmbed) |
|---|---|---|
| **Model Name** | `sentence-transformers/all-MiniLM-L6-v2` | `nomic-ai/CodeRankEmbed` |
| **Model Architecture** | BERT (6 layers, 12 attention heads) | NomicBERT / Arctic-Embed (12 layers, Rotary Position Embeddings) |
| **Pretrained Parameters** | 33,000,000 (~33M) | 137,000,000 (~137M) |
| **Embedding Dimension** | 384 | 768 |
| **Context Window** | 256 tokens | 8,192 tokens |
| **Training Domain** | General natural language | 1M+ code-query pairs (Cornstack / CodeRank dataset, arxiv:2412.01007) |
| **License** | Apache License 2.0 | MIT License |

---

## 3. Runtime & Inference Architecture

- **Execution**: Both models were evaluated using their official pretrained weights and tokenizers in FP32 on the exact same CPU environment (`AMD Ryzen 7 5800H @ 3.2GHz, Windows 11`).
- **Query Prefixing**: CodeRankEmbed uses asymmetric retrieval prefixes:
  - Queries: `search_query: <query text>`
  - Documents: `search_document: <code unit text>`
- **Normalization**: Both models produce L2-normalized unit vectors ($\|\vec{v}\|_2 = 1.0$) evaluated via standard cosine similarity (dot product).

---

## 4. Code & Query Representation

Each Amoeba `RetrievalUnit` is formatted into the dense representation:
```text
<element_kind> <element_name>: <parent_context> <signature> <structural_detail> <file_path>
```
For CodeRankEmbed, this representation is prefixed with `search_document: `, while queries are prefixed with `search_query: `.

---

## 5. Empirical Benchmark Results

### Retrieval Quality Metrics (14 Code-Specific Queries)

| Metric | Real MiniLM-L6-v2 | Real CodeRankEmbed-137M | Empirical Difference |
|---|---:|---:|---:|
| **P@1** | 0.9286 | **1.0000** | **+0.0714 (CodeRank win)** |
| **P@5** | 0.2286 | 0.2143 | -0.0143 |
| **MRR** | 0.9643 | **1.0000** | **+0.0357 (CodeRank win)** |
| **NDCG@5** | 0.9629 | **0.9724** | **+0.0095 (CodeRank win)** |
| **Recall@10** | **1.0000** | 0.9643 | -0.0357 |

### Performance & Resource Comparison

| Metric | Real MiniLM-L6-v2 | Real CodeRankEmbed-137M | Difference |
|---|---:|---:|---:|
| **Model Disk Size** | **~80 MB** | ~548 MB | +468 MB (6.8x) |
| **Embedding Dimension** | **384** | 768 | +384 (2.0x) |
| **Working-Set Memory (RAM)** | **~120 MB** | ~380 MB | +260 MB (3.1x) |
| **Warm Single-Query Latency (Mean)** | **8.34 ms** | 17.85 ms | +9.51 ms (2.1x slower) |
| **Warm Single-Query Latency (P50)** | **7.13 ms** | 17.51 ms | +10.38 ms (2.4x slower) |
| **Warm Single-Query Latency (P95)** | **9.52 ms** | 21.73 ms | +12.21 ms (2.3x slower) |
| **Batch Indexing Throughput** | **517.6 docs/sec** | 249.8 docs/sec | -267.8 docs/sec (2.1x slower) |

---

## 6. Detailed Query Analysis

### Key Wins for CodeRankEmbed

1. **"Which function parses source code?"**
   - **Expected**: `SourceParser`
   - **MiniLM**: Ranked `RepositoryScanner` at **#1** (sim: 0.4641) and `SourceParser` at **#2** (sim: 0.4176) — **Failure (False Positive at #1)**.
   - **CodeRank**: Ranked `SourceParser` at **#1** (sim: 0.5074) and `RepositoryScanner` at **#2** (sim: 0.4604) — **Success (P@1 win)**.
   - *Analysis*: CodeRank understands that "parses" specifically aligns with the parser class rather than the file scanner.

2. **"Where is the embedding provider implemented?"**
   - **Expected**: `PretrainedEmbeddingProvider`
   - **MiniLM**: Top similarity: 0.4528 (separation margin: +0.1783).
   - **CodeRank**: Top similarity: **0.6221** (separation margin: **+0.2591**).
   - *Analysis*: CodeRank significantly increases semantic confidence and margin on software-engineering infrastructure terms.

3. **"Where is the inverted index implemented?"**
   - **Expected**: `InvertedIndex`
   - **MiniLM**: Top similarity: 0.4560.
   - **CodeRank**: Top similarity: **0.5757**.

4. **"Where are relationships stored?"**
   - **Expected**: `RelationshipGraph`
   - **MiniLM**: Top similarity: 0.3501 (tight margin of +0.0209 over `RelationshipAwareSearch`).
   - **CodeRank**: Top similarity: **0.4484** (margin of +0.0218).

### Parity Queries
- Both models successfully identified:
  - `"Where are callers resolved?"` -> `CallExtractor`
  - `"Where is evidence assembled?"` -> `EvidenceAssembler`
  - `"Where is the query normalized?"` -> `QueryUnderstanding`
  - `"Where are supporting AST elements resolved?"` -> `SupportingEvidenceResolver`
  - `"How does the calendar navigate between months?"` -> `useCalendar`
  - `"Where does the calendar store state?"` -> `useCalendar`
  - `"Where is token extraction performed?"` -> `CodeTokenizer`
  - `"Where are imports resolved?"` -> `ImportExtractor`
  - `"Where are function calls resolved?"` -> `CallExtractor`
  - `"Where is relationship-aware search implemented?"` -> `RelationshipAwareSearch`

---

## 7. Adversarial Negative Query Analysis

| Query | Expected | MiniLM Top Sim | CodeRank Top Sim | Grounding Rejection Status |
|---|---|---:|---:|---|
| "Where is the Kubernetes deployment?" | None (not in repo) | 0.0797 (`SupportingEvidenceResolver`) | 0.2633 (`PretrainedEmbeddingProvider`) | Safely rejected by Evidence Sufficiency |
| "Where is the GraphQL resolver?" | None (not in repo) | 0.4009 (`RelationshipGraph`) | 0.3973 (`RelationshipAwareSearch`) | Safely rejected by Evidence Sufficiency |
| "Where is JWT authentication?" | `AuthService` | 0.4458 (`AuthService`) | 0.4394 (`AuthService`) | Validated with subject grounding |
| "Where is the payment checkout?" | `PaymentService` | 0.4072 (`PaymentService`) | 0.4525 (`PaymentService`) | Validated with subject grounding |
| "Where is the OAuth implementation?" | `OAuthClient` | 0.5257 (`OAuthClient`) | 0.4889 (`OAuthClient`) | Validated with subject grounding |

---

## 8. License Information
- **Model Identifier**: `nomic-ai/CodeRankEmbed`
- **License**: **MIT License**
- **Commercial Use & Redistribution**: Fully permitted without royalty or attribution burdens.

---

## 9. Decision & Tradeoff Evaluation

### Classification: **B (CodeRank improves quality on code queries with acceptable, moderate performance cost)**

### Rationale:
1. **Quality Gain**: CodeRankEmbed-137M achieved **1.0000 P@1** (eliminating MiniLM's parser-vs-scanner confusion on `"Which function parses source code?"`) and demonstrated stronger semantic separation margins on core software engineering concepts.
2. **Acceptable Latency**: At **17.8 ms** warm query latency and **250 docs/sec** throughput, CodeRankEmbed is **~4.5x faster than Qwen3-0.6B (79.6 ms)** and well within interactive search latency budgets (<50 ms).
3. **Manageable Footprint**: 137M parameters (~548 MB disk, ~380 MB RAM) is reasonable for developer workstations compared to multi-gigabyte LLM backends.
4. **Current Invariant**: MiniLM remains the fast lightweight default (8.3 ms, 80 MB disk), while CodeRankEmbed represents a viable code-specialized candidate for environments prioritizing retrieval precision over minimum binary size.

---

## 10. Conclusion & Recommendation
Do NOT immediately replace MiniLM as default. The next step is to evaluate whether a principled **dual-channel semantic retrieval** (or optional provider selection) provides synergy without regressions.
