#pragma once

#include "amoeba/index/inverted_index.hpp"
#include "amoeba/index/search_engine.hpp"
#include "amoeba/rank/candidate_match.hpp"

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::rank {

using namespace std;

/**
 * @brief Tunable hyperparameters for the Okapi BM25 ranking algorithm.
 */
struct BM25Params {
    double k1{1.2};  // Non-linear term frequency saturation parameter
    double b{0.75};  // Document length normalization parameter [0.0 - 1.0]
};

/**
 * @brief Okapi BM25 relevance scoring and deterministic ranking engine.
 */
class BM25Ranker {
public:
    /**
     * @brief Computes Robertson-Spärck Jones Inverse Document Frequency (IDF).
     * @param total_docs Total number of documents in corpus (N).
     * @param doc_freq Number of documents containing the term (DF).
     * @return Numerically safe, strictly non-negative IDF value.
     */
    [[nodiscard]] static double idf(size_t total_docs, size_t doc_freq) noexcept;

    /**
     * @brief Computes BM25 contribution for a single query term.
     * @param tf Term frequency in the candidate element.
     * @param doc_len Length (token count) of the candidate element.
     * @param avgdl Average document length across the corpus.
     * @param term_idf Precomputed IDF for the term.
     * @param params BM25 hyperparameters (k1, b).
     * @return Single-term BM25 score.
     */
    [[nodiscard]] static double compute_term_score(uint32_t tf, size_t doc_len, double avgdl,
                                                   double term_idf,
                                                   const BM25Params& params) noexcept;

    /**
     * @brief Computes total BM25 relevance score for a candidate match.
     * @param candidate Candidate element match details.
     * @param stats Corpus-level statistics.
     * @param query_terms Normalized query terms.
     * @param params BM25 hyperparameters.
     * @return Total BM25 relevance score.
     */
    [[nodiscard]] static double compute_score(const CandidateMatch& candidate,
                                              const CorpusStats& stats,
                                              const vector<string>& query_terms,
                                              const BM25Params& params = {}) noexcept;

    /**
     * @brief Ranks candidate matches using BM25 with deterministic tie-breaking.
     * @param candidates Vector of candidate matches.
     * @param stats Corpus-level statistics.
     * @param raw_query Original user query.
     * @param query_terms Normalized query terms.
     * @param params BM25 hyperparameters.
     * @param max_results Maximum number of results to return (0 = unlimited).
     * @return Sorted vector of SearchResult objects.
     */
    [[nodiscard]] static vector<index::SearchResult>
    rank(vector<CandidateMatch>& candidates, const CorpusStats& stats, string_view raw_query,
          const vector<string>& query_terms, const BM25Params& params = {},
          size_t max_results = 100);
};

}  // namespace amoeba::rank
