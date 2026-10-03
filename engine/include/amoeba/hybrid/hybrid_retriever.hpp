#pragma once

#include "amoeba/hybrid/fusion_strategy.hpp"
#include "amoeba/hybrid/score_normalizer.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/index/search_engine.hpp"
#include "amoeba/semantic/embedding_provider.hpp"
#include "amoeba/semantic/semantic_index.hpp"
#include "amoeba/semantic/semantic_retriever.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::hybrid {

/**
 * @brief Search options for hybrid retrieval execution.
 */
struct HybridSearchOptions {
    double alpha{0.5};  ///< Weight for lexical score (1.0 = purely lexical, 0.0 = purely semantic)
    FusionMethod fusion_method{FusionMethod::WeightedScore};
    double rrf_k{60.0};  ///< Smoothing constant for RRF
    index::RankerType lexical_ranker{index::RankerType::CodeAware};
    std::size_t lexical_top_k{50};
    std::size_t semantic_top_k{50};
    std::size_t max_results{10};
    std::optional<parser::ElementKind> kind_filter{std::nullopt};
};

/**
 * @brief Result item returned by hybrid retrieval.
 */
struct HybridSearchResult {
    uint32_t element_id{0};
    parser::CodeElement element;
    std::filesystem::path file_path;
    std::string language;

    double lexical_score{0.0};
    double normalized_lexical_score{0.0};
    double semantic_score{0.0};
    double normalized_semantic_score{0.0};
    double hybrid_score{0.0};

    uint32_t lexical_rank{0};   ///< 1-indexed rank in lexical results (0 if not present)
    uint32_t semantic_rank{0};  ///< 1-indexed rank in semantic results (0 if not present)

    [[nodiscard]] bool operator==(const HybridSearchResult&) const = default;
};

/**
 * @brief Composes lexical and semantic retrieval pipelines into a unified hybrid retriever.
 */
class HybridRetriever {
public:
    HybridRetriever(const index::InvertedIndex& index,
                    const semantic::ISemanticVectorIndex& semantic_index,
                    const semantic::EmbeddingProvider& provider);

    /**
     * @brief Executes a hybrid search combining lexical and semantic signals.
     * @param query Developer query string.
     * @param options Configuration options for candidate depth, ranker, and fusion.
     * @return Ranked list of HybridSearchResult items.
     */
    [[nodiscard]] std::vector<HybridSearchResult>
    search(std::string_view query, const HybridSearchOptions& options = {}) const;

private:
    const index::InvertedIndex& index_;
    index::SearchEngine search_engine_;
    semantic::SemanticRetriever semantic_retriever_;
    const semantic::EmbeddingProvider& provider_;
};

}  // namespace amoeba::hybrid
