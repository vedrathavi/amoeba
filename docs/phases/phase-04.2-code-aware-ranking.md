# Phase 4.2 — Code-Aware Hybrid Ranking

## 1. Motivation

While Okapi BM25 provides strong probabilistic lexical matching, source code differs fundamentally from natural language prose:
1. **Structural Hierarchy**: A function declaration is far more relevant as a primary target than an incidental reference or call site.
2. **Identifier Significance**: Exact symbol name matches carry decisive semantic weight.
3. **Container Context**: Enclosing namespaces, classes, or modules (`parent_name`) provide critical disambiguation for ambiguous method names (e.g. `User::save` vs `Order::save`).
4. **Path Signals**: Directory structure and filenames (e.g. `src/auth/service.py`) communicate component architecture and domain context.

---

## 2. Hybrid Scoring Formulation

The [CodeAwareRanker](file:///d:/amoeba/engine/include/amoeba/rank/code_aware_ranker.hpp) integrates BM25 lexical relevance with structural code signals:

$$S_{\text{CodeAware}}(e, Q) = w_{\text{bm25}} \cdot S_{\text{BM25}}(e, Q) + S_{\text{identifier}}(e, Q) + S_{\text{structural\_field}}(e, Q) + S_{\text{kind}}(e) + S_{\text{coverage}}(e, Q)$$

### Signal Weights and Components:

1. **BM25 Lexical Base**: $w_{\text{bm25}} = 1.0$ with $k_1 = 1.2, b = 0.75$.
2. **Identifier Matching Signals**:
   - **Exact Case-Insensitive Match**: $+60.0$ if normalized name equals query.
   - **Prefix Match**: $+20.0$ if element name starts with query term.
   - **Subword Whole-Token Match**: $+15.0$ if query matches a subword component (e.g. `Scanner` in `RepositoryScanner`).
3. **Structural Field Matches**:
   - Element Name term match: $+12.0$ per term
   - Parent Context term match: $+6.0$ per term
   - File Path term match: $+4.0$ per term
   - Doc Comment term match: $+1.5$ per term
4. **Element Kind Preferences**:
   - `Class`, `Struct`, `Interface`, `TypeAlias`: $+10.0$
   - `Function`, `Method`: $+8.0$
   - `Route` (Next.js / API endpoint): $+8.0$
   - `Property`, `Field`, `Variable`: $+4.0$
   - `Comment`, `Include`, `Call`: $+1.0$
5. **Query Term Coverage Multiplier**:
   $$\text{Coverage}(e, Q) = \frac{|\{t \in Q \mid t \in \text{Terms}(e)\}|}{|Q|} \times 25.0$$

---

## 3. Engineering & Performance

* Zero memory allocations during scoring inner loops by using pre-tokenized element inverted indices and string views.
* Highly optimized candidate ranking execution: sub-millisecond ranking latency across hundreds of candidates.
