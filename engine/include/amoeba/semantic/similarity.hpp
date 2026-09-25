#pragma once

#include "amoeba/semantic/embedding.hpp"

#include <span>

namespace amoeba::semantic {

/**
 * @brief Computes cosine similarity between two vector spans.
 *
 * Requirements:
 * - Deterministic output
 * - Empty vectors return 0.0f safely
 * - Dimension mismatch returns 0.0f safely
 * - Zero vectors return 0.0f safely (handles zero norm without NaN / division by zero)
 * - Result clamped to [-1.0f, 1.0f]
 */
[[nodiscard]] float cosine_similarity(std::span<const float> a, std::span<const float> b) noexcept;

/**
 * @brief Computes cosine similarity between two Embedding objects.
 */
[[nodiscard]] float cosine_similarity(const Embedding& a, const Embedding& b) noexcept;

}  // namespace amoeba::semantic
