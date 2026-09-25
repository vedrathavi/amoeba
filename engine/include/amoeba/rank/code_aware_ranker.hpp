#pragma once

#include "amoeba/index/inverted_index.hpp"
#include "amoeba/index/search_engine.hpp"
#include "amoeba/rank/bm25_ranker.hpp"
#include "amoeba/rank/candidate_match.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace amoeba::rank {

using namespace std;

/**
 * @brief Tunable hyperparameters and signal weights for Code-Aware hybrid ranking.
 */
struct CodeAwareParams {
    BM25Params bm25_params{.k1 = 1.2, .b = 0.75};
    double exact_name_boost{10.0};      // Case-sensitive exact symbol match
    double normalized_name_boost{5.0};  // Normalized (case/separator) exact symbol match
    double prefix_name_boost{2.0};      // Prefix match on declared symbol name
    double coverage_weight{5.0};        // Query term coverage multiplier (coverage * 5.0)
    double name_match_weight{3.0};      // Per-term match weight in declared name
    double context_match_weight{1.5};   // Per-term match weight in parent context
    double detail_match_weight{1.0};    // Per-term match weight in detail/metadata
    double path_match_weight{0.5};      // Per-term match weight in file path
    double declaration_boost{2.0};      // Primary declaration vs Call/Include usage
};

/**
 * @brief Code-Aware hybrid relevance ranking engine combining BM25 with AST structure.
 */
class CodeAwareRanker {
public:
    /**
     * @brief Determines whether a symbol kind is a primary declaration.
     */
    [[nodiscard]] static bool is_primary_declaration(parser::ElementKind kind) noexcept;

    /**
     * @brief Computes hybrid code-aware relevance score for a candidate match.
     * @param candidate Candidate element match details.
     * @param stats Corpus-level statistics.
     * @param raw_query Original user query.
     * @param query_terms Normalized query terms.
     * @param params Code-aware ranking parameters.
     * @return Deterministic floating-point relevance score.
     */
    [[nodiscard]] static double compute_score(const CandidateMatch& candidate,
                                              const CorpusStats& stats, string_view raw_query,
                                              const vector<string>& query_terms,
                                              const CodeAwareParams& params = {}) noexcept;

    /**
     * @brief Ranks candidate matches using Code-Aware hybrid scoring with deterministic
     * tie-breaking.
     * @param candidates Vector of candidate matches.
     * @param stats Corpus-level statistics.
     * @param raw_query Original user query.
     * @param query_terms Normalized query terms.
     * @param params Code-aware ranking parameters.
     * @param max_results Maximum number of results to return (0 = unlimited).
     * @return Sorted vector of SearchResult objects.
     */
    [[nodiscard]] static vector<index::SearchResult>
    rank(vector<CandidateMatch>& candidates, const CorpusStats& stats, string_view raw_query,
          const vector<string>& query_terms, const CodeAwareParams& params = {},
          size_t max_results = 100);
};

}  // namespace amoeba::rank
