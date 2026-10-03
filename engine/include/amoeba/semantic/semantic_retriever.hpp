#pragma once

#include "amoeba/semantic/embedding.hpp"
#include "amoeba/semantic/embedding_provider.hpp"
#include "amoeba/semantic/semantic_index.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace amoeba::semantic {

/**
 * @brief Semantic retriever coordinating vector search against an ISemanticVectorIndex backend.
 *
 * Supports both exact exhaustive search (ExactSemanticIndex) and approximate nearest
 * neighbor search (HnswSemanticIndex) polymorphically through ISemanticVectorIndex.
 */
class SemanticRetriever {
public:
    explicit SemanticRetriever(const ISemanticVectorIndex& index);

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
    const ISemanticVectorIndex& index_;
};

}  // namespace amoeba::semantic
