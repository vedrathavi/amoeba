#include "amoeba/rank/code_aware_ranker.hpp"

#include <algorithm>

namespace amoeba::rank {

using namespace std;

bool CodeAwareRanker::is_primary_declaration(parser::ElementKind kind) noexcept {
    switch (kind) {
    case parser::ElementKind::Class:
    case parser::ElementKind::Struct:
    case parser::ElementKind::Interface:
    case parser::ElementKind::Function:
    case parser::ElementKind::Method:
    case parser::ElementKind::Component:
    case parser::ElementKind::Hook:
    case parser::ElementKind::Route:
    case parser::ElementKind::JSXComponent:
        return true;
    case parser::ElementKind::Call:
    case parser::ElementKind::Include:
    case parser::ElementKind::JSXElement:
    case parser::ElementKind::Attribute:
    case parser::ElementKind::UtilityClass:
    case parser::ElementKind::Selector:
    case parser::ElementKind::Property:
    case parser::ElementKind::Unknown:
        return false;
    }
    return false;
}

double CodeAwareRanker::compute_score(const CandidateMatch& candidate, const CorpusStats& stats,
                                      [[maybe_unused]] string_view raw_query,
                                      const vector<string>& query_terms,
                                      const CodeAwareParams& params) noexcept {
    if (query_terms.empty() || stats.total_documents == 0) {
        return 0.0;
    }

    // 1. Base BM25 Probabilistic Score (Term Rarity & TF Saturation)
    double score = BM25Ranker::compute_score(candidate, stats, query_terms, params.bm25_params);

    // 2. Query Term Coverage Score: (matched_terms / total_query_terms) * coverage_weight
    if (!query_terms.empty()) {
        const double coverage = static_cast<double>(candidate.matched_terms.size()) /
                                static_cast<double>(query_terms.size());
        score += coverage * params.coverage_weight;
    }

    // 3. Identifier Semantics (Exact, Normalized, Prefix)
    if (candidate.exact_name_match) {
        score += params.exact_name_boost;
    }
    if (candidate.normalized_name_match) {
        score += params.normalized_name_boost;
    }
    if (candidate.prefix_name_match && !candidate.normalized_name_match) {
        score += params.prefix_name_boost;
    }

    // 4. Structural Field Weighting (Name > Context > Detail > Path)
    score += static_cast<double>(candidate.name_match_count) * params.name_match_weight;
    score += static_cast<double>(candidate.context_match_count) * params.context_match_weight;
    score += static_cast<double>(candidate.detail_match_count) * params.detail_match_weight;
    score += static_cast<double>(candidate.path_match_count) * params.path_match_weight;

    // 5. Primary Declaration Preference (boost definitions over usages/calls)
    if (is_primary_declaration(candidate.element.kind)) {
        score += params.declaration_boost;
    }

    return score;
}

vector<index::SearchResult> CodeAwareRanker::rank(vector<CandidateMatch>& candidates,
                                                  const CorpusStats& stats, string_view raw_query,
                                                  const vector<string>& query_terms,
                                                  const CodeAwareParams& params,
                                                  size_t max_results) {
    if (candidates.empty()) {
        return {};
    }

    vector<index::SearchResult> results;
    results.reserve(candidates.size());

    for (const auto& cand : candidates) {
        const double score = compute_score(cand, stats, raw_query, query_terms, params);
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
