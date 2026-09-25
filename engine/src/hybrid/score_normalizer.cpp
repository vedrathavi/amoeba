#include "amoeba/hybrid/score_normalizer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace amoeba::hybrid {

std::vector<double> ScoreNormalizer::min_max_normalize(std::span<const double> scores) {
    if (scores.empty()) {
        return {};
    }

    if (scores.size() == 1) {
        return {1.0};
    }

    double min_val = std::numeric_limits<double>::infinity();
    double max_val = -std::numeric_limits<double>::infinity();

    for (const double val : scores) {
        if (!std::isnan(val) && !std::isinf(val)) {
            min_val = std::min(min_val, val);
            max_val = std::max(max_val, val);
        }
    }

    // Handle all NaN / inf or identical scores
    if (min_val > max_val || std::abs(max_val - min_val) < 1e-9) {
        return std::vector<double>(scores.size(), 1.0);
    }

    const double range = max_val - min_val;
    std::vector<double> normalized;
    normalized.reserve(scores.size());

    for (const double val : scores) {
        if (std::isnan(val) || std::isinf(val)) {
            normalized.push_back(0.0);
        } else {
            const double norm = (val - min_val) / range;
            normalized.push_back(std::clamp(norm, 0.0, 1.0));
        }
    }

    return normalized;
}

}  // namespace amoeba::hybrid
