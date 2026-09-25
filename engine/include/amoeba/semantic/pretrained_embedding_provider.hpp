#pragma once

#include "amoeba/semantic/embedding_provider.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::semantic {

/**
 * @brief Pretrained code and text embedding provider.
 *
 * Implements local dense vector embedding inference (384 dimensions) compatible with
 * the all-MiniLM-L6-v2 semantic code embedding architecture.
 *
 * Encapsulates tokenizer subword parsing and transformer feature projection behind
 * the standard EmbeddingProvider interface without leaking model runtime types.
 */
class PretrainedEmbeddingProvider : public EmbeddingProvider {
public:
    /**
     * @brief Constructs provider with built-in code/text semantic weights.
     */
    PretrainedEmbeddingProvider();

    /**
     * @brief Constructs provider loading external model weights if provided.
     * @param model_path Path to local model file.
     */
    explicit PretrainedEmbeddingProvider(const std::filesystem::path& model_path);

    ~PretrainedEmbeddingProvider() override;

    PretrainedEmbeddingProvider(const PretrainedEmbeddingProvider&) = delete;
    PretrainedEmbeddingProvider& operator=(const PretrainedEmbeddingProvider&) = delete;
    PretrainedEmbeddingProvider(PretrainedEmbeddingProvider&&) noexcept;
    PretrainedEmbeddingProvider& operator=(PretrainedEmbeddingProvider&&) noexcept;

    [[nodiscard]] std::size_t dimensions() const noexcept override;
    [[nodiscard]] std::string_view provider_name() const noexcept override;

    [[nodiscard]] std::vector<float> embed(std::string_view text) const override;
    [[nodiscard]] std::vector<std::vector<float>>
    embed_batch(std::span<const std::string> texts) const override;

    [[nodiscard]] bool is_model_loaded() const noexcept;
    [[nodiscard]] std::optional<std::filesystem::path> model_path() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace amoeba::semantic
