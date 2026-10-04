# Phase 9.3C — Minimal Bounded Second-Hop Investigation

**Date:** 2026-10-04  
**Status:** COMPLETE & VERIFIED  

---

## 1. Why Hop 2 Was Needed

In Phase 9.3B forensics, 14 questions were classified as Category E (Multi-Hop Context / Evidence Sufficiency Failures). In cross-file architectural patterns—such as caller $\to$ dispatcher $\to$ executor chains (`router -> dispatcher -> database`), plugin interfaces, and OS adapter delegations (`hub -> alerts -> notifications`)—the critical implementation logic is located 2 relationship hops away from the primary retrieval seed.

Single-hop (Hop-1) investigation discovers the intermediate routing bridge but halts before reaching the execution unit. A bounded second-hop capability traverses this connection.

---

## 2. Existing Evidence Supporting It

Analysis of the Phase 9.3B and 9.2 benchmark artifacts identified:
- **6 to 8 questions** out of 14 Category E failures have the target unit 1 edge beyond the Hop-1 boundary (e.g., `better-auth-4`, `beszel-0`, `amoeba-pkl-01`, `amoeba-beszel-02`, `jiff-4`, `amoeba-jiff-03`).
- In these cases, the primary seed is correctly ranked at Hop 0, and Hop 1 discovers the intermediary, making a 2nd hop the minimal required traversal.

---

## 3. Implementation

### Investigation Types ([`investigation_types.hpp`](file:///d:/amoeba/engine/include/amoeba/investigation/investigation_types.hpp))
- Added `Hop2Relationship` and `Hop2Vocabulary` roles to `InvestigationCandidateRole`.
- Added `is_hop2_discovered` and `hop_depth` (0, 1, 2) to `InvestigatedCandidate`.
- Added `max_discovered_per_hop1` (default 2) and `hop2_score_discount` (default 0.70) to `InvestigationOptions`.
- Added `hop2_candidates_discovered` tracking to `InvestigationMetrics`.

### Bounded Investigation Service ([`bounded_investigation_service.cpp`](file:///d:/amoeba/engine/src/investigation/bounded_investigation_service.cpp))
- Added `expand_to_hop2()` and `expand_to_hop2_with_metrics()` to support conditional second-hop expansion.
- Traverses outgoing/incoming AST edges and technical symbol references from Hop-1 discovered candidates.
- Maintains strict candidate deduplication via `seen_element_ids` and `seen_keys` across all hops.
- Preserves full provenance and sets `expansion_depth = 2` in `assemble_bundle()`.

---

## 4. Exact Bounds

1. **Maximum Depth:** Strictly $\le 2$. No Hop 3+ or unbounded traversal.
2. **Maximum Seeds:** Up to 5 primary seeds from the Top-50 reservoir.
3. **Maximum Discovered Per Hop-1 Unit:** $\le 2$ candidates.
4. **Maximum Total Candidates:** Strictly capped at 15.
5. **Deterministic Ordering:** Stable tie-breaking on score, rank, and element ID.

---

## 5. When Hop 2 Executes

Hop 2 is **conditionally executed** only when Hop-1 evidence fails evidence sufficiency (`!sufficiency.is_sufficient`).

```
Query -> Hop-0 Retrieval -> Seed Selection -> Hop-1 Investigation -> Assemble Bundle -> Sufficiency Check
                                                                                               │
                                                                   ┌───────────────────────────┴───────────────────────────┐
                                                                   ▼                                                       ▼
                                                          [Sufficient: STOP]                            [Insufficient: Perform Hop 2]
                                                                                                                   │
                                                                                                                   ▼
                                                                                                          Assemble Hop-2 Bundle
                                                                                                                   │
                                                                                                                   ▼
                                                                                                           Sufficiency Check
                                                                                                                   │
                                                                                                                   ▼
                                                                                                            Answer / Refusal
```

When Hop-1 produces sufficient evidence, Hop 2 is completely bypassed ($0\text{ ms}$ Hop-2 cost).

---

## 6. Provenance

Every investigated candidate records:
- `role`: `Hop0Primary`, `Hop1Relationship`, `Hop1Vocabulary`, `Hop2Relationship`, or `Hop2Vocabulary`.
- `hop_depth`: `0`, `1`, or `2`.
- `seed_element_id` & `seed_symbol_name`: Primary seed anchor from Hop-0.
- `direction`: `"Outgoing"`, `"Incoming"`, or `"Vocabulary"`.
- `expansion_depth`: Propagated into `EvidenceItem` (0, 1, 2) during bundle assembly.

---

## 7. Safety Invariants Preserved

- **Rule A.1:** Hop-2 evidence cannot independently manufacture primary sufficiency when primary evidence is absent or ungrounded.
- **Rule E:** Distinguishing domain concept grounding must be established in primary candidates.
- **Negative Query Refusal:** Unsupported and negative queries remain rejected with 100% precision.

---

## 8. Focused Validation & Test Results

Executed 18 unit tests in [`bounded_investigation_service_test.cpp`](file:///d:/amoeba/engine/tests/investigation/bounded_investigation_service_test.cpp):
1. `Hop0RemainsUnchangedWhenInvestigationDisabled` (PASSED)
2. `RelevantCandidateTriggersHop1AndDiscoversTarget` (PASSED)
3. `WeakCandidateDoesNotTriggerUncontrolledInvestigation` (PASSED)
4. `MaximumHopAndCandidateBoundsAreEnforced` (PASSED)
5. `DeduplicationPreservesSingleInstanceOfDiscoveredCandidate` (PASSED)
6. `ExpandedEvidenceCannotManufacturePrimarySufficiency` (PASSED)
7. `MultiUnitEvidenceAssemblesCallerAndCalleeCorrectly` (PASSED)
8. `Hop2CanDiscoverGenuinelyUsefulTarget` (PASSED — discovers 2-hop chained execution target)
9. `Hop2RespectsMaximumDepth` (PASSED — depth capped at 2, hop 3 rejected)
10. `CandidateLimitRemains15` (PASSED — candidate pool capped at 15)
11. `SeedLimitRemains5` (PASSED — seeds capped at 5)
12. `DeterministicOrderingAcrossExecutions` (PASSED — 100% reproducible ordering)
13. `DuplicateCandidatesAreRemovedAcrossHops` (PASSED — diamond DAG merges duplicate candidates)
14. `ProvenanceIsPreservedInBundleAssembly` (PASSED — depths 0, 1, 2 recorded in bundle)
15. `RuleA1RemainsIntactWithHop2Evidence` (PASSED — Rule A.1 holds for Hop 2)
16. `RuleERemainsIntactWithHop2Evidence` (PASSED — Rule E holds for Hop 2)
17. `NegativeUnsupportedQueryRemainsRejected` (PASSED — 100% negative refusal)
18. `Hop2DoesNotExecuteWhenExistingEvidenceIsSufficient` (PASSED — Hop 2 bypassed when Hop 1 is sufficient)

**Full Test Suite:** 540 engine tests passed with 0 failures in 9.1s.

---

## 9. Performance Impact

- **Normal Path (Sufficient at Hop 1):** Zero overhead ($0\text{ ms}$).
- **Second-Hop Expansion (when triggered):** $< 0.5\text{ ms}$ graph traversal latency.

---

## 10. What Is Intentionally Deferred

- Depth 3+ traversal (unnecessary and prone to topic drift).
- Vector graph search / ML ranking loops.
- Dynamic autonomous agent planning.
