#include "amoeba/semantic/semantic_retriever.hpp"

namespace amoeba::semantic {

SemanticRetriever::SemanticRetriever(const ISemanticVectorIndex& index) : index_(index) {}

std::vector<SemanticSearchResult>
SemanticRetriever::retrieve(std::span<const float> query_embedding,
                            const SemanticRetrievalOptions& options) const {
    if (query_embedding.empty() || index_.empty() || options.top_k == 0) {
        return {};
    }

    return index_.search(query_embedding, options.top_k, options.min_similarity);
}

std::vector<SemanticSearchResult>
SemanticRetriever::retrieve(const Embedding& query_embedding,
                            const SemanticRetrievalOptions& options) const {
    return retrieve(std::span<const float>(query_embedding.values), options);
}

std::vector<SemanticSearchResult>
SemanticRetriever::retrieve_text(std::string_view query_text, const EmbeddingProvider& provider,
                                 const SemanticRetrievalOptions& options) const {
    auto query_vec = provider.embed(query_text);
    return retrieve(std::span<const float>(query_vec), options);
}

}  // namespace amoeba::semantic
