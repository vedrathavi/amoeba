#include "amoeba/semantic/similarity.hpp"

#include <algorithm>
#include <cmath>

namespace amoeba::semantic {

float cosine_similarity(std::span<const float> a, std::span<const float> b) noexcept {
    if (a.empty() || b.empty() || a.size() != b.size()) {
        return 0.0f;
    }

    double dot = 0.0;
    double norm_a = 0.0;
    double norm_b = 0.0;

    for (std::size_t i = 0; i < a.size(); ++i) {
        dot += static_cast<double>(a[i]) * static_cast<double>(b[i]);
        norm_a += static_cast<double>(a[i]) * static_cast<double>(a[i]);
        norm_b += static_cast<double>(b[i]) * static_cast<double>(b[i]);
    }

    if (norm_a <= 1e-12 || norm_b <= 1e-12) {
        return 0.0f;
    }

    double sim = dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
    return static_cast<float>(std::clamp(sim, -1.0, 1.0));
}

float cosine_similarity(const Embedding& a, const Embedding& b) noexcept {
    return cosine_similarity(std::span<const float>(a.values), std::span<const float>(b.values));
}

}  // namespace amoeba::semantic
