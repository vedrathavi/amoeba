#include "amoeba/rank/bm25_ranker.hpp"

#include <algorithm>
#include <cmath>

namespace amoeba::rank {

using namespace std;

double BM25Ranker::idf(size_t total_docs, size_t doc_freq) noexcept {
    if (total_docs == 0 || doc_freq == 0) {
        return 0.0;
    }
    const double N = static_cast<double>(total_docs);
    const double n = static_cast<double>(doc_freq);

    // Robertson-Spärck Jones Okapi formula with +1.0 floor to guarantee non-negative values:
    // ln(1.0 + (N - n + 0.5) / (n + 0.5))
    const double numerator = N - n + 0.5;
    const double denominator = n + 0.5;
    if (denominator <= 0.0) {
        return 0.0;
    }
    return std::log(1.0 + (numerator / denominator));
}

double BM25Ranker::compute_term_score(uint32_t tf, size_t doc_len, double avgdl, double term_idf,
                                      const BM25Params& params) noexcept {
    if (tf == 0 || term_idf <= 0.0) {
        return 0.0;
    }

    const double tf_d = static_cast<double>(tf);
    const double len_norm = (avgdl > 0.0) ? (static_cast<double>(doc_len) / avgdl) : 1.0;

    // BM25 non-linear term saturation and length normalization:
    // (TF * (k1 + 1)) / (TF + k1 * (1 - b + b * (|D| / avgdl)))
    const double numerator = tf_d * (params.k1 + 1.0);
    const double denominator = tf_d + params.k1 * (1.0 - params.b + params.b * len_norm);

    if (denominator <= 0.0) {
        return 0.0;
    }

    return term_idf * (numerator / denominator);
}

double BM25Ranker::compute_score(const CandidateMatch& candidate, const CorpusStats& stats,
                                 const vector<string>& query_terms,
                                 const BM25Params& params) noexcept {
    if (query_terms.empty() || stats.total_documents == 0) {
        return 0.0;
    }

    double total_score = 0.0;

    for (const auto& term : query_terms) {
        const auto it_tf = candidate.term_frequencies.find(term);
        const uint32_t tf = (it_tf != candidate.term_frequencies.end()) ? it_tf->second : 0;
        if (tf == 0) {
            continue;
        }

        const auto it_df = stats.doc_frequencies.find(term);
        const size_t df = (it_df != stats.doc_frequencies.end()) ? it_df->second : 0;
        const double term_idf = idf(stats.total_documents, df);

        total_score +=
            compute_term_score(tf, candidate.doc_length, stats.avg_doc_length, term_idf, params);
    }

    return total_score;
}

vector<index::SearchResult> BM25Ranker::rank(vector<CandidateMatch>& candidates,
                                             const CorpusStats& stats,
                                             [[maybe_unused]] string_view raw_query,
                                             const vector<string>& query_terms,
                                             const BM25Params& params, size_t max_results) {
    if (candidates.empty()) {
        return {};
    }

    vector<index::SearchResult> results;
    results.reserve(candidates.size());

    for (const auto& cand : candidates) {
        const double score = compute_score(cand, stats, query_terms, params);
        results.push_back(index::SearchResult{
            .element = cand.element,
            .file_path = cand.file_path,
            .language = cand.language,
            .match_count = static_cast<uint32_t>(cand.matched_terms.size()),
            .exact_name_match = cand.exact_name_match,
            .score = score,
        });
    }

    // Deterministic tie-breaking:
    // 1. Highest relevance score
    // 2. Exact name match
    // 3. Higher match count
    // 4. Alphabetical file path
    // 5. Line number ascending
    ranges::stable_sort(results, [](const index::SearchResult& a, const index::SearchResult& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        if (a.exact_name_match != b.exact_name_match) {
            return a.exact_name_match > b.exact_name_match;
        }
        if (a.match_count != b.match_count) {
            return a.match_count > b.match_count;
        }
        if (a.file_path != b.file_path) {
            return a.file_path < b.file_path;
        }
        return a.element.location.start.line < b.element.location.start.line;
    });

    if (max_results > 0 && results.size() > max_results) {
        results.resize(max_results);
    }

    return results;
}

}  // namespace amoeba::rank
