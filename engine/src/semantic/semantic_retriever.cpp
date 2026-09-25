#include "amoeba/semantic/semantic_retriever.hpp"

#include "amoeba/semantic/similarity.hpp"

#include <algorithm>

namespace amoeba::semantic {

SemanticRetriever::SemanticRetriever(const SemanticIndex& index) : index_(index) {}

std::vector<SemanticSearchResult>
SemanticRetriever::retrieve(std::span<const float> query_embedding,
                            const SemanticRetrievalOptions& options) const {
    if (query_embedding.empty() || index_.empty() || options.top_k == 0) {
        return {};
    }

    std::vector<SemanticSearchResult> scored_candidates;
    scored_candidates.reserve(index_.size());

    for (const auto& [id, emb] : index_.entries()) {
        float sim = cosine_similarity(query_embedding, std::span<const float>(emb.values));
        if (sim >= options.min_similarity) {
            scored_candidates.push_back(SemanticSearchResult{
                .element_id = id,
                .similarity_score = sim,
            });
        }
    }

    // Sort by similarity descending, with deterministic tie-breaking on element_id ascending
    std::sort(scored_candidates.begin(), scored_candidates.end(),
              [](const SemanticSearchResult& a, const SemanticSearchResult& b) {
                  if (std::abs(a.similarity_score - b.similarity_score) > 1e-6f) {
                      return a.similarity_score > b.similarity_score;
                  }
                  return a.element_id < b.element_id;
              });

    if (scored_candidates.size() > options.top_k) {
        scored_candidates.resize(options.top_k);
    }

    return scored_candidates;
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
