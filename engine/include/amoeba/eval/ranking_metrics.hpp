#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace amoeba::eval {

/**
 * @brief Standard IR evaluation metrics for ranked search results.
 */
struct RankingMetrics {
    double p_at_1{0.0};
    double p_at_3{0.0};
    double p_at_5{0.0};
    double p_at_10{0.0};
    double r_at_5{0.0};
    double r_at_10{0.0};
    double mrr{0.0};
    double ndcg_at_5{0.0};
    double ndcg_at_10{0.0};

    [[nodiscard]] static RankingMetrics average(std::span<const RankingMetrics> metrics_list);
};

/**
 * @brief Computes IR ranking metrics for a ranked list of retrieved names against graded relevance.
 *
 * @param retrieved_names Ranked list of returned document/element identifiers.
 * @param relevance_grades Ground truth relevance mapping (e.g. grade 3, 2, 1, 0).
 */
[[nodiscard]] RankingMetrics
compute_ranking_metrics(std::span<const std::string> retrieved_names,
                        const std::unordered_map<std::string, uint32_t>& relevance_grades);

}  // namespace amoeba::eval
