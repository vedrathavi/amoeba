#include "amoeba/rank/baseline_ranker.hpp"

#include <algorithm>

namespace amoeba::rank {

using namespace std;

double BaselineRanker::compute_score(const CandidateMatch& candidate,
                                     [[maybe_unused]] string_view raw_query,
                                     const vector<string>& query_terms) noexcept {
    double score = 0.0;

    // 1. Query Term Coverage: (matched_terms / total_query_terms) * 50.0
    if (!query_terms.empty()) {
        const double coverage = static_cast<double>(candidate.matched_terms.size()) /
                                static_cast<double>(query_terms.size());
        score += coverage * 50.0;
    }

    // 2. Exact & Normalized Identifier Match Boosts:
    // - Exact case-preserved identifier match: +100.0
    // - Normalized identifier match (case/separator normalized): +50.0
    if (candidate.exact_name_match) {
        score += 100.0;
    }
    if (candidate.normalized_name_match) {
        score += 50.0;
    }

    // 3. Structural Field Weighting:
    // Matches directly in the symbol's declared name are prioritized over context or path
    if (candidate.matched_in_name) {
        score += 25.0;
    }
    if (candidate.matched_in_context) {
        score += 15.0;
    }
    if (candidate.matched_in_detail) {
        score += 10.0;
    }
    if (candidate.matched_in_path) {
        score += 5.0;
    }

    // 4. Absolute Match Count Weight: +5.0 per matched term
    score += static_cast<double>(candidate.matched_terms.size()) * 5.0;

    return score;
}

vector<index::SearchResult> BaselineRanker::rank(vector<CandidateMatch>& candidates,
                                                 string_view raw_query,
                                                 const vector<string>& query_terms,
                                                 size_t max_results) {
    if (candidates.empty()) {
        return {};
    }

    vector<index::SearchResult> results;
    results.reserve(candidates.size());

    for (const auto& cand : candidates) {
        const double score = compute_score(cand, raw_query, query_terms);
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
