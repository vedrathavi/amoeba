#pragma once

#include <span>
#include <vector>

namespace amoeba::hybrid {

/**
 * @brief Utility for normalizing retrieval scores into standard [0.0, 1.0] ranges.
 */
class ScoreNormalizer {
public:
    /**
     * @brief Normalizes a sequence of scores using Min-Max scaling.
     *
     * Scaling formula:
     *   S_norm = (S - S_min) / (S_max - S_min)
     *
     * Edge case behavior:
     * - Empty input: returns empty vector.
     * - Single element: returns {1.0}.
     * - Identical scores (S_max == S_min): returns vector of 1.0s.
     * - Non-finite values: treated safely.
     *
     * @param scores Span of input scores.
     * @return Vector of normalized scores in range [0.0, 1.0].
     */
    [[nodiscard]] static std::vector<double> min_max_normalize(std::span<const double> scores);

    /**
     * @brief Convenience overload accepting std::vector.
     */
    [[nodiscard]] static std::vector<double> min_max_normalize(const std::vector<double>& scores) {
        return min_max_normalize(std::span<const double>(scores.data(), scores.size()));
    }
};

}  // namespace amoeba::hybrid
