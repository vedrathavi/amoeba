# Phase 8.1.3 — Generic Query Concepts & Evidence Sufficiency

## 1. Problem Statement & Phase 8.1.2 Findings

Phase 8.1.2 investigation exposed two fundamental failure modes in repository-wide question answering and evidence sufficiency evaluation:

1. **False Negative (Indirect / Plural Evidence Mismatch)**:
   - Query: `How does the calendar navigate between months?`
   - Repository vocabulary: `CalendarHeader`, `onPreviousMonth`, `onNextMonth`, `currentMonth`.
   - Failure: Unstemmed plural `months` did not match singular substring `month` in `currentMonth`, and `navigate` was treated as an ungrounded keyword. The computed coverage became $1/3 \approx 0.3333$, falling just below the $0.34$ threshold.

2. **False Positive (Generic Inquiry Verb Accidental Match)**:
   - Query: `Where is RBAC implemented?`
   - Repository contains no RBAC implementation or symbols.
   - Semantic search retrieved a generic candidate (`RelationshipAwareSearchEngine::search`).
   - The inquiry verb `"implemented"` accidentally occurred in an unrelated source excerpt (`"Implemented by ..."`).
   - The sufficiency checker counted `"implemented"` as evidence and accepted the candidate. The LLM was called and hallucinated an RBAC implementation.

---

## 2. Why Lexical Matching Alone Is Insufficient

Lexical matching without grammatical role awareness treats query terms symmetrically. For a question such as `Where is RBAC implemented?`, words like `"where"`, `"is"`, `"rbac"`, and `"implemented"` are not conceptually equal:
- `"where"` and `"is"` are contextual stopwords / interrogatives.
- `"implemented"` is an action / inquiry verb that describes what the user is asking *about* the subject.
- `"rbac"` is the actual **Subject** concept that must exist in the repository for the answer to be valid.

Treating `"implemented"` as 50% of the query keyword budget allowed generic action words in comments or docstrings to satisfy sufficiency even when 100% of the subject concepts were absent.

---

## 3. Why Semantic Nearest-Neighbor Retrieval Cannot Prove Evidence Exists

Cosine similarity in dense embedding spaces produces relative rankings of nearest neighbors within a vector space. If a repository has no vectors for `"RBAC"` or `"JWT"`, semantic retrieval will still return the closest points in the corpus (e.g. general search engines or utility functions with $\text{similarity} \approx 0.70 - 0.76$).

Semantic similarity is candidate discovery, not proof of repository support. Without a concept-aware ground truth check, semantic-only candidates will inevitably trigger hallucinations on out-of-domain questions.

---

## 4. Query Term Roles

Phase 8.1.3 introduces generic, repository-independent query term role categorization within `QueryUnderstanding`:

```cpp
enum class QueryTermRole : uint8_t {
    Subject,  ///< Core entity, identifier, or domain concept being queried (e.g., "RBAC", "JWT", "calendar", "state", "note", "month")
    Action,   ///< Inquiry action or verb describing the operation asked (e.g., "implemented", "defined", "managed", "saved", "rendered", "navigate")
    Context   ///< Interrogative, preposition, stopword, or grammatical context (e.g., "where", "how", "between", "in", "is")
};
```

### Deterministic Categorization Rules:
1. **Context**: Term is an interrogative (`where`, `how`, `what`, `which`, `who`, `when`, `why`) or stopword (`is`, `are`, `the`, `between`, `in`, `to`, `for`, `from`, `with`, `by`, `on`).
2. **Action**: Term stem matches generic inquiry/action bases (`implement`, `define`, `manage`, `save`, `store`, `render`, `create`, `handle`, `use`, `call`, `configure`, `generate`, `load`, `display`, `update`, `delete`, `navigate`, etc.).
3. **Subject**: All domain concepts, identifiers, and technical terms (`rbac`, `jwt`, `calendar`, `month`, `state`, `note`, `checkout`, `invertedindex`).

---

## 5. Conservative Normalization (Stemming)

Lightweight, deterministic, conservative English morphological normalization was added without bringing in external NLP libraries or aggressive stemming algorithms (like Porter/Lancaster) that would corrupt technical terms:

### Normalization Examples:
- Plural nouns: `months` $\to$ `month`, `components` $\to$ `component`, `classes` $\to$ `class`, `categories` $\to$ `category`, `properties` $\to$ `property`.
- Action verb conjugations:
  - `navigating`, `navigates` $\to$ `navigate`
  - `managing`, `managed` $\to$ `manage`
  - `saving`, `saved` $\to$ `save`
  - `rendering`, `rendered` $\to$ `render`
  - `implementing`, `implemented` $\to$ `implement`
  - `creating`, `created` $\to$ `create`

### Negative Preservation (No Mangling):
- Technical abbreviations and terms are strictly preserved: `author`, `authority`, `authentication`, `authorization`, `rbac`, `jwt`, `pass`, `status`, `this`.

---

## 6. Subject-Support Requirement

The core sufficiency logic was updated to enforce the following invariant:

> **For any query with identifiable Subject concepts, at least one meaningful Subject concept MUST have grounded evidence in the retrieved code candidates. Action verbs alone NEVER establish sufficiency.**

If `subject_terms` is non-empty and `matched_subjects` is empty:
- `is_sufficient = false`
- `confidence_score = 0.0`
- The LLM is **never** invoked.
- A grounded refusal is generated explaining which subject concepts are missing from the codebase.

---

## 7. Semantic-Only Guard

If the top candidate was retrieved purely through semantic similarity (`provenance == RetrievalProvenance::SemanticOnly` and `lexical_score == 0`):
- A semantic candidate can **never** establish sufficiency if all query Subject concepts are missing.
- Even if semantic score is high ($\ge 0.70$), if `matched_subjects.empty()`, the candidate is rejected deterministically.

---

## 8. What Remains Intentionally Unsolved

> **Note**: This phase does not attempt to determine semantic truth from embeddings. It establishes a deterministic, repository-independent foundation for evidence sufficiency.

Intentionally reserved for future phases:
- Deep synonym mapping and embedding-based concept bridging (e.g., translating `authenticate` to `LoginService` without exact or normalized substring overlap).
- Dynamic ontology induction or knowledge graph embedding link prediction.

---

## 9. Unit & Integration Test Results

- **Total Tests**: 375/375 passed (100%)
- **Test Suites**: 53 test suites passed
- **Compiler Warnings**: 0
- **clang-format**: Clean
- **git diff --check**: Clean

---

## 10. Multi-Repository CLI Validation Matrix

Testing performed across 2 distinct repositories using `amoeba_cli ask`:

### Repository 1: `demo_test_projects/calendar` (React / TypeScript)

| Query | Category | Expected | Actual Sufficiency | LLM Called? | Top Grounded Evidence |
|---|---|---|---|---|---|
| `How does the calendar navigate between months?` | Valid Concept | SUFFICIENT | SUFFICIENT | YES | `CalendarDay.tsx`, `CalendarHeader.tsx` (`currentMonth`, `onPreviousMonth`) |
| `Where is calendar state managed?` | Valid Concept | SUFFICIENT | SUFFICIENT | YES | `CalendarDay.tsx` (`useState`) |
| `Where are notes saved?` | Valid Concept | SUFFICIENT | SUFFICIENT | YES | `NotesSection.tsx` (`savedNotes`, `useState`) |
| `Where is RBAC implemented?` | Deliberately Unsupported | INSUFFICIENT | INSUFFICIENT (Refusal) | NO | Missing: `"rbac"` |
| `Where is JWT authentication implemented?` | Deliberately Unsupported | INSUFFICIENT | INSUFFICIENT (Refusal) | NO | Missing: `"jwt"`, `"authentication"` |
| `Where is payment processing implemented?` | Deliberately Unsupported | INSUFFICIENT | INSUFFICIENT (Refusal) | NO | Missing: `"payment"` |

### Repository 2: `D:/amoeba` (C++ Intelligence Engine)

| Query | Category | Expected | Actual Sufficiency | LLM Called? | Top Grounded Evidence |
|---|---|---|---|---|---|
| `Where is InvertedIndex implemented?` | Valid Concept | SUFFICIENT | SUFFICIENT | YES | `relationship_aware_search.cpp` (`InvertedIndex`) |
| `Where is RelationshipGraph built?` | Valid Concept | SUFFICIENT | SUFFICIENT | YES | `relationship_graph.hpp` (`RelationshipGraph`) |
| `Where is EvidenceSufficiencyChecker defined?` | Valid Concept | SUFFICIENT | SUFFICIENT | YES | `evidence_sufficiency.hpp` (`EvidenceSufficiencyChecker`) |
| `Where is RBAC implemented?` | Deliberately Unsupported | INSUFFICIENT | INSUFFICIENT (Refusal) | NO | Missing: `"rbac"` |
| `Where is JWT authentication implemented?` | Deliberately Unsupported | INSUFFICIENT | INSUFFICIENT (Refusal) | NO | Missing: `"jwt"`, `"authentication"` |
| `Where is shopping cart checkout handled?` | Deliberately Unsupported | INSUFFICIENT | INSUFFICIENT (Refusal) | NO | Missing: `"shop"`, `"cart"`, `"checkout"` |

---

## 11. Next Step: Semantic Evidence Support

The next logical phase will investigate **Semantic Evidence Support**: leveraging existing MiniLM embeddings at the concept/evidence layer to bridge synonym gaps (e.g. `authenticate` $\leftrightarrow$ `LoginService`) in a grounded, deterministic manner while maintaining strict guards against unrelated semantic nearest neighbors.
