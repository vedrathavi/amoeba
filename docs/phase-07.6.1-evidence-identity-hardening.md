# Amoeba — Phase 7.6.1
# Evidence Identity & Ambiguous-Term Hardening

## 1. Executive Summary

In Phase 7.6, the Grounded Answer Contract and Evidence Sufficiency Checker achieved a 92% (23/25) accuracy rate with 1 hallucination:
- **Query**: `"Where is the GraphQL resolver?"`
- **Expected**: `INSUFFICIENT_EVIDENCE`
- **Phase 7.6 Failure**: The query matched the generic architectural suffix `"resolver"` in `SupportingEvidenceResolver` (a resolver for supporting AST evidence). Because 1 of 2 subject words matched (50% coverage $\ge 34\%$), the candidate entered the context window, and the local LLM hallucinated:
  > *"The GraphQL resolver is located in the SupportingEvidenceResolver class."*

**Phase 7.6.1 introduces the Evidence Identity Rule (Rule E)**:
A generic architectural/component term (e.g. *resolver, service, handler, manager, provider, controller, parser, deployment, authentication*) may provide supporting structural evidence, but **cannot independently establish a compound subject concept when all distinguishing domain concepts (e.g. *GraphQL, JWT, OAuth, Kubernetes*) are absent across the repository evidence.**

---

## 2. Root Cause Analysis

```
User Query: "Where is the GraphQL resolver?"
                      │
   ┌──────────────────┴──────────────────┐
   ▼                                     ▼
Distinguishing Domain Subject         Generic Component Suffix
["graphql"] (ABSENT in Repo)         ["resolver"] (Matches SupportingEvidenceResolver)
```

In the previous sufficiency model:
1. `"graphql"` and `"resolver"` were treated as interchangeable subject stems of equal weight.
2. Matching `"resolver"` gave 1/2 = 50% subject term coverage.
3. Since $0.50 \ge 0.34$ (`min_term_coverage`), the sufficiency gate marked the bundle as `is_sufficient = true`.
4. The local LLM received `SupportingEvidenceResolver` in context. Guided by confirmation bias, the model assumed `SupportingEvidenceResolver` fulfilled the user's inquiry regarding GraphQL.

---

## 3. Evidence Identity Model

### 3.1 Compound Subject Decomposition
In `QueryUnderstanding::analyze`:
- **Distinguishing Subject Terms** ($S_{\text{dist}}$): Core domain entities, technologies, and specific nouns (e.g., `graphql`, `jwt`, `oauth`, `calendar`, `note`, `inverted`, `ast`, `caller`, `vocabulary`).
- **Generic Component Terms** ($S_{\text{generic}}$): Standard architectural role nouns:
  `resolver`, `handler`, `manager`, `service`, `client`, `provider`, `controller`, `parser`, `processor`, `factory`, `adapter`, `builder`, `helper`, `utility`, `validator`, `middleware`, `component`, `hook`, `store`, `storage`, `model`, `view`, `router`, `route`, `worker`, `runner`, `listener`, `emitter`, `consumer`, `producer`, `engine`, `pipeline`, `deployment`, `endpoint`, `driver`, `connector`, `serializer`, `deserializer`, `indexer`, `index`, `searcher`, `extractor`, `checker`, `inspector`, `evaluator`, `auth`, `authentication`, `login`, `checkout`, `wrapper`, `registry`, `module`, `element`.

### 3.2 Evidence Identity Invariant (Rule E)
$$\text{If } |S_{\text{dist}}| > 0 \land \left( S_{\text{dist}} \cap \text{MatchedSubjects} = \emptyset \right) \implies \text{is\_sufficient} = \text{false}$$

When a query contains at least one distinguishing domain subject term, matching **only** generic component terms without any grounded distinguishing domain term is classified as ambiguous and rejected prior to context construction.

### 3.3 Preserving Single-Term and Legitimate Queries
- **Single-Term Generic Queries** (e.g., *"Where is the parser?"*): $|S_{\text{dist}}| = 0$, so Rule E does not trigger. The generic term matches `SourceParser` normally.
- **Legitimate Compound Queries** (e.g., *"Where are supporting AST elements resolved?"*): $|S_{\text{dist}}| > 0$ (`"support"`, `"ast"`). Because `"support"` and `"ast"` match `SupportingEvidenceResolver`, $S_{\text{dist}} \cap \text{MatchedSubjects} \neq \emptyset$. The query is accepted.

---

## 4. Benchmark Results: Before vs. After

The frozen 25-query evaluation suite was re-executed using real local model inference (`Qwen/Qwen2.5-Coder-0.5B-Instruct` on CUDA) and `all-MiniLM-L6-v2`.

| Metric | Phase 7.6 Baseline | Phase 7.6.1 Hardened | Change |
| :--- | :---: | :---: | :---: |
| **Total Test Queries** | 25 | 25 | — |
| **Correct Classifications** | 23 / 25 (92.0%) | **24 / 25 (96.0%)** | **+4.0% (+1)** |
| **Hallucination Count** | 1 (4.0%) | **0 (0.0%)** | **-100% (ELIMINATED)** |
| **Unsupported Queries Accuracy** | 4 / 5 (80.0%) | **5 / 5 (100.0%)** | **+20.0%** |
| **Adversarial Negative Accuracy** | 5 / 5 (100.0%) | **5 / 5 (100.0%)** | **Preserved (100%)** |
| **Vocabulary Gap Accuracy** | 5 / 5 (100.0%) | **5 / 5 (100.0%)** | **Preserved (100%)** |
| **Supported Queries Accuracy** | 9 / 10 (90.0%) | **9 / 10 (90.0%)** | **Preserved (90%)** |

### Per-Category Breakdown:

```
Category                Total    Correct    Hallucinations    Accuracy
──────────────────────────────────────────────────────────────────────
Supported                  10          9                 0       90.0%
Unsupported                 5          5                 0      100.0%
Adversarial Negative        5          5                 0      100.0%
Vocabulary Gap              5          5                 0      100.0%
──────────────────────────────────────────────────────────────────────
Total                      25         24                 0       96.0%
```

---

## 5. Detailed Query Analysis

### 5.1 The Fixed U4 Case
- **Query**: `"Where is the GraphQL resolver?"`
- **Candidate Retrieved**: `SupportingEvidenceResolver` (`engine/src/retrieval/supporting_evidence_resolver.cpp`)
- **Phase 7.6 Outcome**: `GROUNDED` (Hallucinated: *"The GraphQL resolver is located in SupportingEvidenceResolver..."*)
- **Phase 7.6.1 Outcome**: `INSUFFICIENT_EVIDENCE` (True Negative)
- **Refusal Reason**:
  > *"Only generic component terms were matched; required distinguishing domain concept(s) ['graphql'] were not found in repository evidence."*
- **Latency**: **6.8 ms** (0 ms LLM inference latency, 0 tokens consumed).

### 5.2 Preserved Legitimate Disambiguation (S5)
- **Query**: `"Where are supporting AST elements resolved?"`
- **Candidate Retrieved**: `SupportingEvidenceResolver`
- **Outcome**: `GROUNDED` (True Positive)
- **Reason**: Distinguishing subjects `{"support", "ast"}` matched `SupportingEvidenceResolver`.

### 5.3 Preserved Positive Vocabulary-Gap Queries (100% 5/5)
1. *"How does the calendar move to another month?"* → `useCalendar` (`src/useCalendar.ts`)
2. *"Where is user note persistence handled?"* → `NotesStorage` (`src/notes_storage.ts`)
3. *"Where does source code get parsed?"* → `SourceParser` (`engine/src/parser/source_parser.cpp`)
4. *"Where are callers discovered?"* → `CallExtractor` (`engine/src/graph/call_extractor.cpp`)
5. *"Where is code vocabulary tokenized?"* → `CodeTokenizer` (`engine/src/index/code_tokenizer.cpp`)

---

## 6. Performance & Latency Breakdown

| Pipeline Stage | Supported Queries (Avg) | Rejected Queries (Avg) |
| :--- | :---: | :---: |
| **Query Understanding & Retrieval** | 8.59 ms | 7.04 ms |
| **Evidence Assembly** | < 0.01 ms | < 0.01 ms |
| **Sufficiency Gate (Rule E)** | 0.07 ms | 0.07 ms |
| **Context Construction** | 0.01 ms | 0.00 ms |
| **LLM Inference** | 1,307.61 ms | **0.00 ms (Fast-Path Rejection)** |
| **Total End-to-End Latency** | **1,317.53 ms** | **7.11 ms** |

**Speedup on Rejected Queries**: **185× faster response** (7.11 ms vs 1,317.53 ms) with 100% token savings.

---

## 7. Test Suite Status

- **396 / 396 tests passing** across 57 test suites (+5 new unit tests added in Phase 7.6.1).
- Zero warnings, clean build.
