#pragma once

#include "amoeba/parser/code_element.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace amoeba::hybrid {

/**
 * @brief Available candidate fusion strategies.
 */
enum class FusionMethod {
    WeightedScore,   ///< Linear combination: H = alpha * L_norm + (1 - alpha) * S_norm
    ReciprocalRank,  ///< Reciprocal Rank Fusion: RRF = sum(1 / (k + rank))
};

/**
 * @brief Candidate entity with both lexical and semantic scores and ranks.
 */
struct ScoredCandidate {
    uint32_t element_id{0};
    parser::CodeElement element;
    std::filesystem::path file_path;
    std::string language;

    double raw_lexical_score{0.0};
    double normalized_lexical_score{0.0};
    double raw_semantic_score{0.0};
    double normalized_semantic_score{0.0};
    double fused_score{0.0};

    uint32_t lexical_rank{0};   ///< 1-indexed rank in lexical results (0 if not retrieved)
    uint32_t semantic_rank{0};  ///< 1-indexed rank in semantic results (0 if not retrieved)

    [[nodiscard]] bool operator==(const ScoredCandidate&) const = default;
};

/**
 * @brief Algorithms for fusing multi-modal candidate rankings.
 */
class FusionStrategy {
public:
    /**
     * @brief Performs weighted score fusion.
     *
     * H = alpha * L_norm + (1.0 - alpha) * S_norm
     *
     * Ties are broken deterministically by element_id ascending.
     *
     * @param candidates Vector of candidate entities with normalized scores.
     * @param alpha Weight assigned to lexical score [0.0, 1.0].
     * @return Sorted vector of candidates in descending fused_score order.
     */
    [[nodiscard]] static std::vector<ScoredCandidate>
    fuse_weighted(std::vector<ScoredCandidate> candidates, double alpha);

    /**
     * @brief Performs Reciprocal Rank Fusion (RRF).
     *
     * RRF(d) = sum_{m in {lex, sem}} (rank_m(d) > 0 ? 1.0 / (k + rank_m(d)) : 0.0)
     *
     * @param candidates Vector of candidate entities with assigned ranks.
     * @param k Smoothing constant (default: 60.0).
     * @return Sorted vector of candidates in descending fused_score order.
     */
    [[nodiscard]] static std::vector<ScoredCandidate>
    fuse_rrf(std::vector<ScoredCandidate> candidates, double k = 60.0);
};

}  // namespace amoeba::hybrid
