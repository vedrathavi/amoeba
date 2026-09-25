#pragma once

#include "amoeba/semantic/embedding.hpp"
#include "amoeba/semantic/embedding_provider.hpp"
#include "amoeba/semantic/semantic_index.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace amoeba::semantic {

/**
 * @brief Search result record from semantic retrieval.
 */
struct SemanticSearchResult {
    ElementId element_id{0};
    float similarity_score{0.0f};

    [[nodiscard]] bool operator==(const SemanticSearchResult&) const = default;
};

/**
 * @brief Configuration parameters for semantic retrieval.
 */
struct SemanticRetrievalOptions {
    std::size_t top_k{10};
    float min_similarity{-1.0f};  ///< Minimum similarity score threshold
};

/**
 * @brief Brute-force exhaustive semantic retriever.
 *
 * Scans all stored embeddings in the SemanticIndex, computes cosine similarity against
 * the query vector, and returns the top-K matches with deterministic tie-breaking.
 */
class SemanticRetriever {
public:
    explicit SemanticRetriever(const SemanticIndex& index);

    /**
     * @brief Performs semantic retrieval given a query embedding vector span.
     */
    [[nodiscard]] std::vector<SemanticSearchResult>
    retrieve(std::span<const float> query_embedding,
             const SemanticRetrievalOptions& options = {}) const;

    /**
     * @brief Performs semantic retrieval given a query Embedding object.
     */
    [[nodiscard]] std::vector<SemanticSearchResult>
    retrieve(const Embedding& query_embedding, const SemanticRetrievalOptions& options = {}) const;

    /**
     * @brief Convenience method to embed a query string via an EmbeddingProvider and retrieve.
     */
    [[nodiscard]] std::vector<SemanticSearchResult>
    retrieve_text(std::string_view query_text, const EmbeddingProvider& provider,
                  const SemanticRetrievalOptions& options = {}) const;

private:
    const SemanticIndex& index_;
};

}  // namespace amoeba::semantic
