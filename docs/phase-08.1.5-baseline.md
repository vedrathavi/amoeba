# Phase 8.1.5 Baseline Specification

**Timestamp**: 2026-09-26T23:14:00+05:30  
**Repository State**: Git clean, 381 individual gtest cases passing (346 ctest entries), 0 compiler warnings.

---

## 1. Frozen Test Suite Status

- **CTest Target Count**: 346 test suites
- **Individual GoogleTest Count**: 381 test cases
- **Pass Rate**: 100% (346/346 test suites passed)
- **Total CTest Execution Time**: 13.44s

---

## 2. Frozen Numerical Parameters

| Parameter Name | Value | Location | Description |
| :--- | :---: | :--- | :--- |
| `min_candidate_similarity` | `0.50f` | `semantic_evidence_support.hpp` | Discards candidate items below this cosine score |
| `strong_candidate_similarity` | `0.65f` | `semantic_evidence_support.hpp` | Classifies candidate as strong semantic match |
| `min_coherent_support_score` | `0.60f` | `semantic_evidence_support.hpp` | Score threshold for `SemanticConceptSupport::is_supported` |
| `max_candidates_to_evaluate` | `10` | `semantic_evidence_support.hpp` | Maximum number of primary bundle items evaluated |
| Top-2 Linear Coherence | `0.5*s1 + 0.5*s2` | `semantic_evidence_support.cpp` | Averaging formula for top-2 semantic matches |
| Single Candidate Multiplier (isolated) | `0.65f` | `semantic_evidence_support.cpp` | Multiplier for single candidate without graph support |
| Single Candidate Multiplier (graph) | `0.88f` | `semantic_evidence_support.cpp` | Multiplier for single candidate with graph support |
| Structural Graph Boost Factor | `1.10f` | `semantic_evidence_support.cpp` | Boost multiplier when top-2 candidates share an edge (capped at 1.0f) |
| Lexical Sufficiency Concept Floor | `0.60f` | `evidence_sufficiency.cpp` | Score floor for lexical concept satisfaction |
| Subject Coverage Minimum | `0.34f` | `evidence_sufficiency.cpp` | Minimum fraction of subject concepts that must be satisfied |

---

## 3. Frozen Formulas

1. **Semantic Concept Score ($S$)**:
   $$\text{If } N \ge 2: \quad S = 0.5 \cdot s_1 + 0.5 \cdot s_2$$
   $$\text{If } N = 1 \land \text{has\_graph}: \quad S = s_1 \cdot 0.88$$
   $$\text{If } N = 1 \land \neg\text{has\_graph}: \quad S = s_1 \cdot 0.65$$
   $$\text{If } \text{top\_candidates\_connected}: \quad S = \min(1.0, S \cdot 1.10)$$

2. **Concept Supported Decision**:
   $$\text{is\_supported} = (S \ge 0.60)$$

3. **Sufficiency Grounding Aggregation**:
   $$\text{Subject Coverage} = \frac{\sum_{c \in \text{Subjects}} \mathbb{I}(\text{lexical\_score}(c) \ge 0.60 \lor \text{semantic\_score}(c) \ge 0.60)}{|\text{Subjects}|}$$
   $$\text{Is Sufficient} = (\text{Subject Coverage} \ge 0.34) \land (\text{EvidenceBundle is non-empty})$$

---

## 4. Current Baseline Results

### Positive Cases
- `"How does the calendar navigate between months?"` $\rightarrow$ **Sufficient** (Covered by `onPreviousMonth`, `onNextMonth`)
- `"Where is notes state persisted?"` $\rightarrow$ **Sufficient** (Covered by `useLocalStorage`, `savedNotes`)
- `"How are dates formatted for display?"` $\rightarrow$ **Sufficient** (Covered by `formatDate`, `MONTH_NAMES`)
- `"Where is calendar state managed?"` $\rightarrow$ **Sufficient** (Covered by `CalendarContext`, `useCalendarState`)

### Negative Cases
- `"Where is RBAC implemented?"` $\rightarrow$ **Refused (Insufficient Evidence)**
- `"Where is JWT authentication implemented?"` $\rightarrow$ **Refused (Insufficient Evidence)**
- `"Where is Redis configured?"` $\rightarrow$ **Refused (Insufficient Evidence)**
- `"Where is payment processing implemented?"` $\rightarrow$ **Refused (Insufficient Evidence)**
- `"Where is Kubernetes configured?"` $\rightarrow$ **Refused (Insufficient Evidence)**
- `"Where is GraphQL implemented?"` $\rightarrow$ **Refused (Insufficient Evidence)**
- `"Where is shopping cart checkout handled?"` $\rightarrow$ **Refused (Insufficient Evidence)**

### Known False Positives (Frozen Limitation)
- `"Where is OAuth configured?"` $\rightarrow$ Candidate `handleAction` scored $0.671$, passing semantic sufficiency floor before reaching LLM refusal.
- `"Where is token extraction performed?"` $\rightarrow$ Candidate `hash_token_seed` in `pretrained_embedding_provider.cpp` scored $0.85$ due to vector collision on `"token"`, falsely accepting a PRNG seed helper as token extraction.

### Latency Baseline
- Query Understanding: $\sim 0.15\text{ ms}$
- Embedding Generation: $\sim 1.5\text{ ms}$
- Semantic Retrieval: $\sim 3.0\text{ ms}$
- Semantic Evidence Support: $\sim 0.8\text{ ms}$
- Total Deterministic Pipeline: $\sim 7.1\text{ ms}$
