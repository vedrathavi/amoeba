#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::semantic {

/**
 * @brief Abstract interface for generating dense vector embeddings from text.
 *
 * Keeps model generation decoupled from storage and retrieval.
 */
class EmbeddingProvider {
public:
    virtual ~EmbeddingProvider() = default;

    [[nodiscard]] virtual std::size_t dimensions() const noexcept = 0;
    [[nodiscard]] virtual std::string_view provider_name() const noexcept = 0;

    [[nodiscard]] virtual std::vector<float> embed(std::string_view text) const = 0;

    [[nodiscard]] virtual std::vector<std::vector<float>>
    embed_batch(std::span<const std::string> texts) const {
        std::vector<std::vector<float>> results;
        results.reserve(texts.size());
        for (const auto& text : texts) {
            results.push_back(embed(text));
        }
        return results;
    }
};

}  // namespace amoeba::semantic
