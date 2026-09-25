#include "amoeba/index/search_engine.hpp"

#include "amoeba/index/code_tokenizer.hpp"

#include <algorithm>
#include <unordered_map>

namespace amoeba::index {

using namespace std;

vector<SearchResult> SearchEngine::search(string_view query, const SearchOptions& options) const {
    const auto query_terms = CodeTokenizer::tokenize_query(query);
    if (query_terms.empty()) {
        return {};
    }

    const string norm_raw_query = CodeTokenizer::normalize_term(query);

    struct ElementMatchInfo {
        uint32_t matched_terms_count{0};
    };

    unordered_map<ElementId, ElementMatchInfo> match_map;

    for (const auto& term : query_terms) {
        const auto* postings = index_.lookup(term);
        if (postings == nullptr) {
            continue;
        }

        for (const auto elem_id : *postings) {
            match_map[elem_id].matched_terms_count++;
        }
    }

    if (match_map.empty()) {
        return {};
    }

    vector<SearchResult> results;
    results.reserve(match_map.size());

    const size_t required_matches =
        (options.match_mode == MatchMode::AllTerms) ? query_terms.size() : 1;

    for (const auto& [elem_id, match_info] : match_map) {
        if (match_info.matched_terms_count < required_matches) {
            continue;
        }

        const auto& indexed_elem = index_.get_element(elem_id);

        if (options.kind_filter.has_value() && indexed_elem.element.kind != *options.kind_filter) {
            continue;
        }

        const auto& indexed_file = index_.get_file(indexed_elem.file_id);
        const bool is_exact =
            (CodeTokenizer::normalize_term(indexed_elem.element.name) == norm_raw_query);
        const double baseline_score =
            (is_exact ? 100.0 : 0.0) + static_cast<double>(match_info.matched_terms_count) * 10.0;

        results.push_back(SearchResult{
            .element = indexed_elem.element,
            .file_path = indexed_file.file_path,
            .language = indexed_file.language,
            .match_count = match_info.matched_terms_count,
            .exact_name_match = is_exact,
            .score = baseline_score,
        });
    }

    // Deterministic ranking: exact name matches first, then higher score, then file/line
    ranges::stable_sort(results, [](const SearchResult& a, const SearchResult& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        if (a.match_count != b.match_count) {
            return a.match_count > b.match_count;
        }
        if (a.file_path != b.file_path) {
            return a.file_path < b.file_path;
        }
        return a.element.location.start.line < b.element.location.start.line;
    });

    if (options.max_results > 0 && results.size() > options.max_results) {
        results.resize(options.max_results);
    }

    return results;
}

}  // namespace amoeba::index
