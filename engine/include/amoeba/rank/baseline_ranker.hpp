#pragma once

#include "amoeba/index/inverted_index.hpp"
#include "amoeba/index/search_engine.hpp"
#include "amoeba/rank/candidate_match.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace amoeba::rank {

/**
 * @brief Baseline relevance scoring and deterministic ranking engine.
 */
class BaselineRanker {
public:
    /**
     * @brief Computes a baseline relevance score for a candidate match.
     * @param candidate Candidate element match details.
     * @param raw_query Original user query.
     * @param query_terms Normalized query terms.
     * @return Deterministic floating-point relevance score.
     */
    [[nodiscard]] static double compute_score(const CandidateMatch& candidate,
                                              string_view raw_query,
                                              const vector<string>& query_terms) noexcept;

    /**
     * @brief Ranks and sorts candidate matches into SearchResult items.
     * @param candidates Vector of candidate matches.
     * @param raw_query Original user query.
     * @param query_terms Normalized query terms.
     * @param max_results Maximum number of results to retain (0 = unlimited).
     * @return Sorted vector of SearchResult objects.
     */
    [[nodiscard]] static vector<index::SearchResult> rank(vector<CandidateMatch>& candidates,
                                                          string_view raw_query,
                                                          const vector<string>& query_terms,
                                                          size_t max_results = 100);
};

}  // namespace amoeba::rank
