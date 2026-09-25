#include "amoeba/hybrid/fusion_strategy.hpp"

#include <algorithm>
#include <cmath>

namespace amoeba::hybrid {

std::vector<ScoredCandidate> FusionStrategy::fuse_weighted(std::vector<ScoredCandidate> candidates,
                                                           double alpha) {
    const double clamped_alpha = std::clamp(alpha, 0.0, 1.0);
    const double beta = 1.0 - clamped_alpha;

    for (auto& cand : candidates) {
        cand.fused_score = (clamped_alpha * cand.normalized_lexical_score) +
                           (beta * cand.normalized_semantic_score);
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [clamped_alpha](const ScoredCandidate& a, const ScoredCandidate& b) {
                         if (std::abs(a.fused_score - b.fused_score) > 1e-9) {
                             return a.fused_score > b.fused_score;
                         }

                         // Pure lexical mode: lexical rank takes precedence
                         if (clamped_alpha >= 0.999) {
                             if ((a.lexical_rank > 0) != (b.lexical_rank > 0)) {
                                 return a.lexical_rank > 0;
                             }
                             if (a.lexical_rank > 0 && b.lexical_rank > 0 &&
                                 a.lexical_rank != b.lexical_rank) {
                                 return a.lexical_rank < b.lexical_rank;
                             }
                         }

                         // Pure semantic mode: semantic rank takes precedence
                         if (clamped_alpha <= 0.001) {
                             if ((a.semantic_rank > 0) != (b.semantic_rank > 0)) {
                                 return a.semantic_rank > 0;
                             }
                             if (a.semantic_rank > 0 && b.semantic_rank > 0 &&
                                 a.semantic_rank != b.semantic_rank) {
                                 return a.semantic_rank < b.semantic_rank;
                             }
                         }

                         // General tie-breaking: retrieved in either modality > unretrieved
                         const bool a_retrieved = (a.lexical_rank > 0 || a.semantic_rank > 0);
                         const bool b_retrieved = (b.lexical_rank > 0 || b.semantic_rank > 0);
                         if (a_retrieved != b_retrieved) {
                             return a_retrieved;
                         }

                         return a.element_id < b.element_id;  // Deterministic tie-breaking
                     });

    return candidates;
}

std::vector<ScoredCandidate> FusionStrategy::fuse_rrf(std::vector<ScoredCandidate> candidates,
                                                      double k) {
    const double smooth_k = (k > 0.0) ? k : 60.0;

    for (auto& cand : candidates) {
        double rrf_score = 0.0;
        if (cand.lexical_rank > 0) {
            rrf_score += 1.0 / (smooth_k + static_cast<double>(cand.lexical_rank));
        }
        if (cand.semantic_rank > 0) {
            rrf_score += 1.0 / (smooth_k + static_cast<double>(cand.semantic_rank));
        }
        cand.fused_score = rrf_score;
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const ScoredCandidate& a, const ScoredCandidate& b) {
                         if (std::abs(a.fused_score - b.fused_score) > 1e-9) {
                             return a.fused_score > b.fused_score;
                         }

                         const bool a_retrieved = (a.lexical_rank > 0 || a.semantic_rank > 0);
                         const bool b_retrieved = (b.lexical_rank > 0 || b.semantic_rank > 0);
                         if (a_retrieved != b_retrieved) {
                             return a_retrieved;
                         }

                         return a.element_id < b.element_id;  // Deterministic tie-breaking
                     });

    return candidates;
}

}  // namespace amoeba::hybrid
