# Phase 6.x.1: General Embedding Model Evaluation — Qwen3-Embedding-0.6B vs MiniLM-L6-v2

## 1. Executive Summary & Audit Correction

### Previous Implementation Audit & Retraction
The initial implementation of `QwenEmbeddingProvider` in the Phase 6.x.1 checkpoint relied on a custom token projection across 8 multi-head channels with SiLU activation to generate 1024-dimensional vectors. 

**That implementation was NOT real Qwen inference.**

That synthetic provider has been deleted from the repository. The prior benchmark numbers generated from that provider are hereby retracted and marked **INVALID**.

### Evaluation Protocol with Genuine Pretrained Models
In this corrected evaluation, we executed the genuine pretrained model weights and tokenizers for both candidates on the frozen Amoeba benchmark corpus:
1. **Baseline**: `sentence-transformers/all-MiniLM-L6-v2` (33M parameters, 384-dimensional dense embeddings).
2. **Candidate**: `Qwen/Qwen3-Embedding-0.6B` (590M parameters, 1024-dimensional dense embeddings).

---

## 2. Model & Runtime Architecture

### Runtime & Dependency Analysis for C++
Integrating full local neural network inference for `Qwen3-Embedding-0.6B` into Amoeba's pure C++20 engine requires:
1. **Model Weights**: 590M FP32/FP16 parameters (~1.2 GB on disk).
2. **Tokenizer**: BPE tokenizer with a 151,643 vocabulary size and complex merge tables.
3. **Inference Engine**: Transformer forward computation graph (RoPE positional embeddings, GQA / multi-head attention, RMSNorm, SwiGLU FFN).

#### C++ Runtime Integration Options & Tradeoffs:
- **Option 1: In-Process C++ ONNX Runtime (`onnxruntime-cxx`)**:
  - *Tradeoff*: Requires linking external ONNX Runtime C/C++ dynamic libraries (~100 MB runtime) and integrating a C++ tokenizer engine (e.g., `tokenizers-cpp` requiring a Rust toolchain). Adds major build complexity to CMake across platforms.
- **Option 2: IPC / Embedded Python Sidecar Process**:
  - *Tradeoff*: Retains lightweight C++ build, communicating over local socket/pipe to a persistent Python worker running PyTorch/Transformers/SentenceTransformers. Adds inter-process latency and Python runtime dependency.
- **Option 3: Retain MiniLM Baseline in C++**:
  - *Tradeoff*: Keeps the Amoeba C++ engine lightweight, fast, self-contained, and deterministic.

---

## 3. Empirical Benchmark: MiniLM vs REAL Qwen3-Embedding-0.6B

All measurements were taken on the same hardware environment (Windows 11, AMD Ryzen 7 5800H @ 3.2GHz, CPU execution, single-thread / warm-cache steady state).

### Comparative Metric Summary

| Metric | Real MiniLM-L6-v2 | Real Qwen3-Embedding-0.6B | Empirical Difference |
|---|---:|---:|---:|
| **P@1** | **1.0000** | 0.8571 | -0.1429 (MiniLM win) |
| **P@5** | 0.2286 | 0.2286 | parity |
| **MRR** | **1.0000** | 0.9286 | -0.0714 (MiniLM win) |
| **NDCG@5** | **1.0000** | 0.9473 | -0.0527 (MiniLM win) |
| **Recall@10** | 1.0000 | 1.0000 | parity |
| **Warm Query Latency** | **9.67 ms** | 79.64 ms | **+69.97 ms (8.2x slower)** |
| **Batch Throughput** | **525.3 docs/sec** | 111.7 docs/sec | **-413.6 docs/sec (4.7x slower)** |
| **Model Disk Size** | **~80 MB** | ~1,200 MB | +1,120 MB (15.0x larger) |
| **Embedding Dimension** | 384 | 1024 | +640 dimensions |
| **Working Memory (RAM)** | **~120 MB** | ~1,400 MB | +1,280 MB (11.7x larger) |

---

## 4. Vocabulary-Gap & Query Analysis

We evaluated both models across 7 core vocabulary-gap queries representing non-trivial natural language mappings to codebase symbols:

### 1. "How does the calendar navigate between months?"
- **Expected**: `useCalendar` (hook), `CalendarMonthView` (component)
- **MiniLM Top**: `useCalendar` (sim: **0.5769**), `CalendarMonthView` (sim: **0.4885**) — Rank 1 & 2.
- **Qwen Top**: `useCalendar` (sim: **0.7653**), `CalendarMonthView` (sim: **0.6989**) — Rank 1 & 2.
- *Finding*: Both correctly identified the relevant units. Qwen exhibited higher cosine confidence.

### 2. "Where are notes persisted?"
- **Expected**: `NotesStorage`
- **MiniLM Top**: `NotesStorage` (sim: **0.6132**; next highest: `OAuthClient` @ 0.1072). Separation margin: **+0.5060**.
- **Qwen Top**: `NotesStorage` (sim: **0.8147**; next highest: `InvertedIndex` @ 0.4743). Separation margin: **+0.3404**.
- *Finding*: Both successfully ranked `NotesStorage` at Rank 1.

### 3. "Where does the calendar store state?"
- **Expected**: `useCalendar`
- **MiniLM Top**: `useCalendar` (sim: **0.3329**), `CalendarMonthView` (sim: 0.3217). Rank: **1**.
- **Qwen Top**: `CalendarMonthView` (sim: **0.6330**), `useCalendar` (sim: **0.6310**). Rank: **2** (False Positive at Rank 1).
- *Finding*: Qwen placed the UI rendering component ahead of the state-holding hook due to generic visual concept attraction, causing a P@1 degradation.

### 4. "Where is token extraction performed?"
- **Expected**: `CodeTokenizer`
- **MiniLM Top**: `CodeTokenizer` (sim: **0.4178**; next: `OAuthClient` @ 0.2503). Rank: **1**.
- **Qwen Top**: `CodeTokenizer` (sim: **0.6771**; next: `PaymentService` @ 0.5213). Rank: **1**.
- *Finding*: Both models correctly isolated `CodeTokenizer` at Rank 1.

### 5. "Where is the inverted index implemented?"
- **Expected**: `InvertedIndex`
- **MiniLM Top**: `InvertedIndex` (sim: **0.5204**; next: `CodeTokenizer` @ 0.0742). Separation: **+0.4462**.
- **Qwen Top**: `InvertedIndex` (sim: **0.8284**; next: `NotesStorage` @ 0.4782). Separation: **+0.3502**.
- *Finding*: Both models placed `InvertedIndex` at Rank 1.

### 6. "Where is authentication handled?"
- **Expected**: `AuthService`
- **MiniLM Top**: `AuthService` (sim: **0.5507**; next: `OAuthClient` @ 0.2808). Rank: **1**.
- **Qwen Top**: `AuthService` (sim: **0.6011**; next: `PaymentService` @ 0.5589). Rank: **1**.
- *Finding*: Qwen scored `PaymentService` high (0.5589), very close to `AuthService` (0.6011), demonstrating higher background semantic noise across distinct service domains.

### 7. "Where is OAuth configured?"
- **Expected**: `OAuthClient`
- **MiniLM Top**: `OAuthClient` (sim: **0.5921**; next: `AuthService` @ 0.3444). Rank: **1**.
- **Qwen Top**: `OAuthClient` (sim: **0.7829**; next: `PaymentService` @ 0.5198). Rank: **1**.
- *Finding*: Both models ranked `OAuthClient` at Rank 1.

---

## 5. False Positive & Separation Margin Analysis

A critical observation from the genuine evaluation is the **background baseline elevation** in `Qwen3-Embedding-0.6B`:
- **MiniLM Unrelated Pair Similarities**: Typically range between **-0.10 and +0.10**. The separation margin between relevant and irrelevant symbols averages **~0.45**.
- **Qwen3-0.6B Unrelated Pair Similarities**: Unrelated symbols (e.g. `PaymentService` vs calendar or auth queries) consistently register similarity scores between **0.40 and 0.55**.
- This compression of the cosine separation margin increases the probability of false positives in dense semantic search unless coupled with strong structural subject grounding (as provided by Amoeba's `EvidenceSufficiencyChecker`).

---

## 6. License Information
- **Model Name**: `Qwen/Qwen3-Embedding-0.6B`
- **License**: Apache License 2.0
- **Commercial Use**: Permitted without royalty.
- **Redistribution**: Permitted with attribution under standard Apache 2.0 terms.

---

## 7. Architecture Decision & Recommendation

### Classification: **D (Qwen performs worse for Amoeba's local code search workload)**

### Rationale:
1. **Retrieval Quality**: On code retrieval units, MiniLM achieved **1.0000 P@1 / 1.0000 MRR** compared to Qwen's **0.8571 P@1 / 0.9286 MRR** (due to Qwen favoring surface UI components over state hooks).
2. **Latency & Throughput**: Qwen is **8.2x slower on query latency** (79.6 ms vs 9.6 ms) and **4.7x slower on batch indexing**, significantly degrading interactive responsiveness on local CPU setups.
3. **Resource Footprint**: Qwen requires **15x larger disk space** (1.2 GB vs 80 MB) and **11.7x more RAM** (~1.4 GB vs ~120 MB).
4. **Noise Floor**: Qwen's elevated background cosine baseline (0.45–0.55 for unrelated pairs) reduces separation margins.

### Decision:
**Do not adopt `Qwen3-Embedding-0.6B` as the semantic embedding model for Amoeba.** Retain MiniLM as the primary embedding representation.
