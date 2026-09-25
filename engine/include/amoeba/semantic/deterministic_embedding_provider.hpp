#pragma once

#include "amoeba/semantic/embedding_provider.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace amoeba::semantic {

/**
 * @brief Deterministic test double embedding provider.
 *
 * NOTE: This is a test double for unit testing pipeline mechanics, NOT a production semantic model.
 * Produces deterministic, unit-normalized vectors derived from token/character hash projections.
 */
class DeterministicEmbeddingProvider : public EmbeddingProvider {
public:
    explicit DeterministicEmbeddingProvider(std::size_t dimensions = 64);

    [[nodiscard]] std::size_t dimensions() const noexcept override { return dimensions_; }
    [[nodiscard]] std::string_view provider_name() const noexcept override {
        return "deterministic_test_double";
    }

    [[nodiscard]] std::vector<float> embed(std::string_view text) const override;

private:
    std::size_t dimensions_;
};

}  // namespace amoeba::semantic
