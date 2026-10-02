#include "amoeba/index/search_engine.hpp"

#include "amoeba/index/code_tokenizer.hpp"
#include "amoeba/rank/baseline_ranker.hpp"
#include "amoeba/rank/bm25_ranker.hpp"
#include "amoeba/rank/code_aware_ranker.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace amoeba::index {

using namespace std;

vector<SearchResult> SearchEngine::search(string_view query, const SearchOptions& options) const {
    // 1. Query Tokenization & Normalization
    const auto query_terms = CodeTokenizer::tokenize_query(query);
    if (query_terms.empty() && options.clean_query.empty() && options.extra_lookup_terms.empty()) {
        return {};
    }

    const string norm_raw_query = CodeTokenizer::normalize_term(query);

    // Build collapsed compound representation (e.g. "use calendar" -> "usecalendar")
    string collapsed_query;
    collapsed_query.reserve(norm_raw_query.size());
    for (char c : norm_raw_query) {
        if (!isspace(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.') {
            collapsed_query.push_back(c);
        }
    }

    // Process clean query if provided
    vector<string> clean_terms;
    string norm_clean_query;
    string collapsed_clean_query;
    if (!options.clean_query.empty()) {
        clean_terms = CodeTokenizer::tokenize_query(options.clean_query);
        norm_clean_query = CodeTokenizer::normalize_term(options.clean_query);
        for (char c : norm_clean_query) {
            if (!isspace(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.') {
                collapsed_clean_query.push_back(c);
            }
        }
    }

    // Extended lookup terms: union of raw terms, clean terms, extra terms, and compound tokens
    vector<string> lookup_terms;
    unordered_set<string> seen_lookup_terms;

    auto add_lookup_term = [&](const string& t) {
        if (!t.empty() && seen_lookup_terms.insert(t).second) {
            lookup_terms.push_back(t);
        }
    };

    for (const auto& t : query_terms) {
        add_lookup_term(t);
    }
    for (const auto& t : clean_terms) {
        add_lookup_term(t);
    }
    for (const auto& t : options.extra_lookup_terms) {
        add_lookup_term(t);
    }
    for (const auto& t : options.technical_entities) {
        for (const auto& tok : CodeTokenizer::tokenize_identifier(t)) {
            add_lookup_term(tok);
        }
    }

    if (query_terms.size() >= 2 && query_terms.size() <= 4 && !collapsed_query.empty() &&
        collapsed_query != norm_raw_query) {
        add_lookup_term(collapsed_query);
    }
    if (clean_terms.size() >= 2 && clean_terms.size() <= 4 && !collapsed_clean_query.empty() &&
        collapsed_clean_query != norm_clean_query) {
        add_lookup_term(collapsed_clean_query);
    }

    if (lookup_terms.empty()) {
        return {};
    }

    struct CandidateAccumulator {
        unordered_set<string> matched_terms;
    };

    // 2. Candidate Retrieval (Single Term Lookup -> Postings -> Candidate Element Accumulation)
    unordered_map<ElementId, CandidateAccumulator> candidate_map;

    for (const auto& term : lookup_terms) {
        const auto* postings = index_.lookup(term);
        if (postings == nullptr) {
            continue;
        }

        for (const auto elem_id : *postings) {
            candidate_map[elem_id].matched_terms.insert(term);
        }
    }

    if (candidate_map.empty()) {
        return {};
    }

    // 3. Candidate Filtering & Metadata Extraction
    const size_t required_matches =
        (options.match_mode == MatchMode::AllTerms)
            ? (!clean_terms.empty() ? clean_terms.size() : query_terms.size())
            : 1;

    vector<rank::CandidateMatch> candidates;
    candidates.reserve(candidate_map.size());

    // Prepare technical entity normalized sets for exact matching
    unordered_set<string> normalized_tech_entities;
    for (const auto& te : options.technical_entities) {
        normalized_tech_entities.insert(CodeTokenizer::normalize_term(te));
    }

    for (const auto& [elem_id, accum] : candidate_map) {
        if (accum.matched_terms.size() < required_matches) {
            continue;
        }

        const auto& indexed_elem = index_.get_element(elem_id);

        if (options.kind_filter.has_value() && indexed_elem.element.kind != *options.kind_filter) {
            continue;
        }

        const auto& indexed_file = index_.get_file(indexed_elem.file_id);
        const string norm_elem_name = CodeTokenizer::normalize_term(indexed_elem.element.name);

        bool is_exact = (indexed_elem.element.name == query ||
                         (!options.clean_query.empty() && indexed_elem.element.name == options.clean_query));
        if (!is_exact) {
            for (const auto& te : options.technical_entities) {
                if (indexed_elem.element.name == te) {
                    is_exact = true;
                    break;
                }
            }
        }

        bool is_norm_exact =
            (norm_elem_name == norm_raw_query ||
             (!collapsed_query.empty() && norm_elem_name == collapsed_query) ||
             (!norm_clean_query.empty() && norm_elem_name == norm_clean_query) ||
             (!collapsed_clean_query.empty() && norm_elem_name == collapsed_clean_query) ||
             normalized_tech_entities.contains(norm_elem_name));

        bool is_prefix =
            (!norm_raw_query.empty() && norm_elem_name.starts_with(norm_raw_query)) ||
            (!collapsed_query.empty() && norm_elem_name.starts_with(collapsed_query)) ||
            (!norm_clean_query.empty() && norm_elem_name.starts_with(norm_clean_query)) ||
            (!collapsed_clean_query.empty() && norm_elem_name.starts_with(collapsed_clean_query));

        if (!is_prefix) {
            for (const auto& te_norm : normalized_tech_entities) {
                if (!te_norm.empty() && norm_elem_name.starts_with(te_norm)) {
                    is_prefix = true;
                    break;
                }
            }
        }

        // Analyze which field matched for field-specific weighting and term frequencies
        const auto name_tokens = CodeTokenizer::tokenize_identifier(indexed_elem.element.name);
        const unordered_set<string> name_set(name_tokens.begin(), name_tokens.end());

        const auto context_tokens =
            CodeTokenizer::tokenize_identifier(indexed_elem.element.parent_context);
        const unordered_set<string> context_set(context_tokens.begin(), context_tokens.end());

        const auto detail_tokens = CodeTokenizer::tokenize_identifier(indexed_elem.element.detail);
        const unordered_set<string> detail_set(detail_tokens.begin(), detail_tokens.end());

        const auto path_tokens =
            CodeTokenizer::tokenize_path(indexed_file.file_path.generic_string());
        const unordered_set<string> path_set(path_tokens.begin(), path_tokens.end());

        bool matched_in_name = false;
        bool matched_in_context = false;
        bool matched_in_detail = false;
        bool matched_in_path = false;
        uint32_t name_match_count = 0;
        uint32_t context_match_count = 0;
        uint32_t detail_match_count = 0;
        uint32_t path_match_count = 0;
        unordered_map<string, uint32_t> tf_map;

        for (const auto& t : accum.matched_terms) {
            uint32_t tf_count = 0;
            for (const auto& token : name_tokens) {
                if (token == t) {
                    tf_count++;
                }
            }
            for (const auto& token : context_tokens) {
                if (token == t) {
                    tf_count++;
                }
            }
            for (const auto& token : detail_tokens) {
                if (token == t) {
                    tf_count++;
                }
            }
            for (const auto& token : path_tokens) {
                if (token == t) {
                    tf_count++;
                }
            }
            tf_map[t] = tf_count;

            if (name_set.contains(t)) {
                matched_in_name = true;
                name_match_count++;
            }
            if (context_set.contains(t)) {
                matched_in_context = true;
                context_match_count++;
            }
            if (detail_set.contains(t)) {
                matched_in_detail = true;
                detail_match_count++;
            }
            if (path_set.contains(t)) {
                matched_in_path = true;
                path_match_count++;
            }
        }

        vector<string> matched_terms_vec(accum.matched_terms.begin(), accum.matched_terms.end());
        const size_t doc_len = index_.get_element_length(elem_id);

        candidates.push_back(rank::CandidateMatch{
            .element_id = elem_id,
            .element = indexed_elem.element,
            .file_path = indexed_file.file_path,
            .language = indexed_file.language,
            .matched_terms = std::move(matched_terms_vec),
            .term_frequencies = std::move(tf_map),
            .doc_length = doc_len,
            .exact_name_match = is_exact,
            .normalized_name_match = is_norm_exact,
            .prefix_name_match = is_prefix,
            .matched_in_name = matched_in_name,
            .matched_in_context = matched_in_context,
            .matched_in_detail = matched_in_detail,
            .matched_in_path = matched_in_path,
            .name_match_count = name_match_count,
            .context_match_count = context_match_count,
            .detail_match_count = detail_match_count,
            .path_match_count = path_match_count,
        });
    }

    // 4. Ranking (Delegated to configured ranker)
    const auto& scoring_query_terms = (!clean_terms.empty()) ? clean_terms : query_terms;

    if (options.ranker_type == RankerType::Baseline) {
        return rank::BaselineRanker::rank(candidates, query, lookup_terms, options.max_results);
    }

    rank::CorpusStats corpus_stats{
        .total_documents = index_.element_count(),
        .avg_doc_length = index_.avg_element_length(),
        .doc_frequencies = {},
    };
    for (const auto& term : lookup_terms) {
        corpus_stats.doc_frequencies[term] = index_.document_frequency(term);
    }

    if (options.ranker_type == RankerType::BM25) {
        return rank::BM25Ranker::rank(candidates, corpus_stats, query, lookup_terms, {},
                                      options.max_results);
    }

    return rank::CodeAwareRanker::rank(candidates, corpus_stats, query, scoring_query_terms, {},
                                       options.max_results);
}

}  // namespace amoeba::index
