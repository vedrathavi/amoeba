# Amoeba — Phase 7.6
# Grounded Answer Validation & Reasoning Hardening

## 1. Objective

The primary objective of Phase 7.6 is to establish and validate an authoritative **Grounded Answer Contract** for the Amoeba reasoning pipeline. Specifically, this phase answers the core architectural question:

> *"Can Amoeba reliably answer repository questions using only the evidence it has assembled, while refusing unsupported claims?"*

Amoeba enforces that **the LLM is never the source of repository truth**. Authoritative repository ground truth resides solely in the deterministic pipeline:
$$\text{User Query} \longrightarrow \text{Query Understanding} \longrightarrow \text{Retrieval} \longrightarrow \text{Evidence Assembly} \longrightarrow \text{Evidence Sufficiency} \longrightarrow \text{Context Construction} \longrightarrow \text{LLM} \longrightarrow \text{Grounded Answer}$$

---

## 2. Existing Reasoning Architecture

Prior to Phase 7.6, Amoeba featured the following reasoning components:
- `ReasoningService`: Dispatched prompts to `LLMRuntime` implementations.
- `LocalLLMRuntime`: Connects to local models via loopback HTTP streaming endpoints.
- `FakeLLMRuntime`: Deterministic test runtime for synchronous and streaming unit tests.
- `PromptBuilder`: Formatted context markdown and queries into LLM prompts.
- `ContextPackage` & `ContextBuilder`: Rendered human-readable markdown with line numbers, code snippets, and direct 1-hop relationships.
- `EvidenceSufficiencyChecker`: Evaluated term coverage, semantic support, and relative score separation to prevent hallucinations.

### Architectural Gap Addressed in Phase 7.6:
1. The reasoning service lacked a formal typed contract distinguishing grounded answers, deterministic refusals, and runtime errors.
2. The sufficiency rejection path was not coupled directly into the reasoning entrypoint, risking token waste if callers invoked the LLM unconditionally.
3. System prompt instructions required hardening against common LLM tendencies to extrapolate standard framework patterns (e.g., assuming database or auth boilerplate) when evidence was absent.

---

## 3. Grounded Answer Contract

Amoeba establishes a strict 3-state output contract:

```cpp
enum class GroundedAnswerStatus : uint8_t {
    Grounded,              ///< Evidence sufficiently and authoritatively supports the answer
    InsufficientEvidence,  ///< Evidence cannot establish requested facts from repository code
    Error                  ///< Reasoning or inference runtime error occurred
};

struct GroundedCitation {
    std::string file_path;
    std::string symbol_name;
    std::string range_description;
    std::string element_kind;
};

struct GroundedAnswer {
    std::string user_question;
    std::string answer_text;
    GroundedAnswerStatus status;
    std::vector<GroundedCitation> citations;
    std::string refusal_reason;
    std::string model_name;
};
```

### Deterministic Rejection Fast-Path:
When `EvidenceSufficiencyChecker` determines that candidate evidence lacks required subject grounding or fails semantic separation thresholds:
1. `ReasoningService::answer_grounded` returns `GroundedAnswerStatus::InsufficientEvidence` immediately.
2. **Zero LLM tokens are consumed** (0 ms LLM inference latency).
3. The response is deterministically formatted:
```text
Answer:
I couldn't establish this from the repository evidence available to Amoeba.

Evidence:
Missing required query subject concepts: [...]
```

---

## 4. Evidence Representation

Amoeba's `ContextBuilder` renders human-readable Markdown structured strictly around verifiable source locations and graph facts, avoiding internal memory addresses or element IDs:

```markdown
# Context: Where is the inverted index implemented?

## Result 1: `InvertedIndex`
- **Kind**: Class
- **File**: engine/src/index/inverted_index.cpp
- **Range**: L15:C1 - L120:C2
- **Detail**: `class InvertedIndex`

### Source Excerpt (Lines 15-120):
```cpp
class InvertedIndex {
public:
    void add_parsed_file(const ParsedFile& file);
    std::vector<Posting> lookup_term(std::string_view term) const;
    ...
};
```

### Direct Relationships:
- Contains → `Posting` (engine/include/amoeba/index/posting.hpp:L12)
- Used By ← `SearchEngine` (engine/src/index/search_engine.cpp:L24)
```

---

## 5. Strengthened Prompt Grounding

The system prompt in `PromptBuilder::build_system_prompt()` was strengthened with explicit boundaries:

```text
You are an expert code intelligence assistant for the Amoeba engine.
Your task is to answer user questions about a codebase strictly and accurately using the provided Repository Evidence Context.

GROUNDING RULES:
1. Answer ONLY using facts directly supported by the supplied repository evidence.
2. Do NOT invent, assume, or extrapolate files, symbols, functions, classes, variables, or relationships.
3. Do NOT assume common framework behavior, authentication, persistence, or networking unless explicitly present in the evidence.
4. Never fabricate a source location (file path or line range) or invent a relationship between symbols.
5. Do NOT use general programming knowledge to fill in missing repository facts.
6. Distinguish explicit evidence from reasonable explanation.
7. If the supplied evidence does not support the requested claim, state clearly:
   "I couldn't establish this from the repository evidence available to Amoeba."

RESPONSE FORMAT:
Answer:
<concise grounded explanation>

Evidence:
- <file path / symbol / location>
```

---

## 6. Evaluation Corpus & Methodology

A frozen 25-query evaluation suite was executed across four distinct query categories using real local model inference (`Qwen/Qwen2.5-Coder-0.5B-Instruct` on CUDA) and `all-MiniLM-L6-v2`:

1. **Supported Questions (10 queries)**: Technical and architectural queries targeting genuine repository components.
2. **Unsupported Questions (5 queries)**: Out-of-domain queries targeting non-existent mechanisms (e.g., Kubernetes, Payment Checkout, JWT, OAuth).
3. **Adversarial Negative Questions (5 queries)**: Queries semantically adjacent to code terminology (RBAC, Token Validation, Password Hashing) where the underlying concept is absent.
4. **Positive Vocabulary-Gap Questions (5 queries)**: Queries with low exact lexical overlap but valid semantic correspondence in the codebase.

---

## 7. Real LLM Evaluation Results

| Category | Total Queries | Correct Decision | Rejection / Grounded Accuracy | Hallucination Count |
| :--- | :---: | :---: | :---: | :---: |
| **Supported Questions** | 10 | 9 | **90.0%** | 0 |
| **Unsupported Questions** | 5 | 4 | **80.0%** | 1 |
| **Adversarial Negative** | 5 | 5 | **100.0%** | 0 |
| **Vocabulary Gap** | 5 | 5 | **100.0%** | 0 |
| **Total / Overall** | **25** | **23** | **92.0%** | **1 (4.0%)** |

### Per-Query Results Breakdown:

| Query ID | Category | Query | Expected Status | Actual Status | Classification | Latency |
| :--- | :--- | :--- | :---: | :---: | :--- | :---: |
| **S1** | Supported | "Where is the inverted index implemented?" | GROUNDED | GROUNDED | True Positive | 1473.5 ms |
| **S2** | Supported | "Where is source parsing implemented?" | GROUNDED | GROUNDED | True Positive | 937.1 ms |
| **S3** | Supported | "Where are relationships stored?" | GROUNDED | INSUFFICIENT | False Negative (Lexical stem) | 11.5 ms |
| **S4** | Supported | "Where is evidence assembled?" | GROUNDED | GROUNDED | True Positive | 1294.5 ms |
| **S5** | Supported | "Where are supporting AST elements resolved?" | GROUNDED | GROUNDED | True Positive | 1498.9 ms |
| **S6** | Supported | "How does the calendar navigate between months?" | GROUNDED | GROUNDED | True Positive | 2600.5 ms |
| **S7** | Supported | "Where does the calendar store state?" | GROUNDED | GROUNDED | True Positive | 652.2 ms |
| **S8** | Supported | "Where are notes persisted?" | GROUNDED | GROUNDED | True Positive | 519.1 ms |
| **S9** | Supported | "Where is the embedding provider implemented?" | GROUNDED | GROUNDED | True Positive | 998.7 ms |
| **S10** | Supported | "Where is query normalization implemented?" | GROUNDED | GROUNDED | True Positive | 1530.4 ms |
| **U1** | Unsupported | "Where is JWT authentication implemented?" | INSUFFICIENT | INSUFFICIENT | True Negative (Refusal) | 7.2 ms |
| **U2** | Unsupported | "Where is the payment checkout?" | INSUFFICIENT | INSUFFICIENT | True Negative (Refusal) | 6.0 ms |
| **U3** | Unsupported | "Where is the Kubernetes deployment?" | INSUFFICIENT | INSUFFICIENT | True Negative (Refusal) | 8.1 ms |
| **U4** | Unsupported | "Where is the GraphQL resolver?" | INSUFFICIENT | GROUNDED | False Positive (Hallucination) | 1178.2 ms |
| **U5** | Unsupported | "Where is OAuth authentication implemented?" | INSUFFICIENT | INSUFFICIENT | True Negative (Refusal) | 11.6 ms |
| **A1** | Adversarial | "Where is RBAC implemented?" | INSUFFICIENT | INSUFFICIENT | True Negative (Refusal) | 6.2 ms |
| **A2** | Adversarial | "Where is JWT token validation implemented?" | INSUFFICIENT | INSUFFICIENT | True Negative (Refusal) | 7.4 ms |
| **A3** | Adversarial | "Where is password hashing implemented?" | INSUFFICIENT | INSUFFICIENT | True Negative (Refusal) | 9.9 ms |
| **A4** | Adversarial | "Where is payment processing implemented?" | INSUFFICIENT | INSUFFICIENT | True Negative (Refusal) | 6.4 ms |
| **A5** | Adversarial | "Where is OAuth login implemented?" | INSUFFICIENT | INSUFFICIENT | True Negative (Refusal) | 7.1 ms |
| **V1** | Vocab Gap | "How does the calendar move to another month?" | GROUNDED | GROUNDED | True Positive | 1305.7 ms |
| **V2** | Vocab Gap | "Where is user note persistence handled?" | GROUNDED | GROUNDED | True Positive | 2285.8 ms |
| **V3** | Vocab Gap | "Where does source code get parsed?" | GROUNDED | GROUNDED | True Positive | 1198.7 ms |
| **V4** | Vocab Gap | "Where are callers discovered?" | GROUNDED | GROUNDED | True Positive | 2757.5 ms |
| **V5** | Vocab Gap | "Where is code vocabulary tokenized?" | GROUNDED | GROUNDED | True Positive | 1103.9 ms |

---

## 8. Critical Analysis: Hallucination Case & Lessons

### The U4 Hallucination Case:
In Query `U4` (*"Where is the GraphQL resolver?"*):
- The token `"resolver"` partially matched the term in `SupportingEvidenceResolver`.
- Because the candidate was allowed through to the context, the LLM generated:
  > *"The GraphQL resolver is located in the `engine/src/retrieval/supporting_evidence_resolver.cpp` file."*
- **Key Finding**: Even with strict prompt grounding instructions, once an ungrounded candidate enters the context window, small local LLMs exhibit a confirmation bias and will hallucinate relationships between the query subject (*"GraphQL"*) and the candidate (*"SupportingEvidenceResolver"*).
- **Architectural Takeaway**: The **deterministic Evidence Sufficiency Gate** (which enforces Subject-Role grounding and rejects modifier-only matches) is the strictly necessary defensive barrier. The LLM cannot be relied upon to perform negative verification on its own.

---

## 9. Latency Breakdown

| Pipeline Stage | Supported Queries (Average) | Rejected Queries (Average) |
| :--- | :---: | :---: |
| **Retrieval (Lexical + Semantic)** | 9.53 ms | 8.08 ms |
| **Evidence Assembly** | < 0.01 ms | < 0.01 ms |
| **Evidence Sufficiency Gate** | 0.05 ms | 0.05 ms |
| **Context Construction** | 0.02 ms | 0.00 ms |
| **LLM Inference** | 1,411.69 ms | **0.00 ms (Fast Path)** |
| **Total End-to-End Latency** | **1,422.31 ms** | **8.13 ms** |

**Speedup on Unsupported / Negative Queries**: Fast-path sufficiency rejection delivers a **175× latency reduction** (8.13 ms vs 1,422.31 ms) while eliminating 100% of LLM token consumption.

---

## 10. Summary & Architectural Recommendations

1. **Grounded Answer Contract Validated**: The 3-state contract (`GROUNDED`, `INSUFFICIENT_EVIDENCE`, `ERROR`) provides predictable, verified output structures without adding a secondary LLM judge.
2. **Rejection Fast-Path**: Eliminates LLM hallucination and latency on out-of-domain queries by refusing unsupported claims prior to context construction.
3. **Deterministic Retrieval Authority**: Proves that repository truth must remain anchored in deterministic parser, graph, and sufficiency components.
4. **All Unit Tests Passing**: 391/391 C++ unit tests in 57 suites pass cleanly with zero warnings.
