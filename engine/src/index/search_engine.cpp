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
    // 1. Query Tokenization
    const auto query_terms = CodeTokenizer::tokenize_query(query);
    if (query_terms.empty()) {
        return {};
    }

    const string norm_raw_query = CodeTokenizer::normalize_term(query);

    struct CandidateAccumulator {
        unordered_set<string> matched_terms;
    };

    // 2. Candidate Retrieval (Term Lookup -> Postings -> Candidate Element Accumulation)
    unordered_map<ElementId, CandidateAccumulator> candidate_map;

    for (const auto& term : query_terms) {
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
        (options.match_mode == MatchMode::AllTerms) ? query_terms.size() : 1;

    vector<rank::CandidateMatch> candidates;
    candidates.reserve(candidate_map.size());

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
        const bool is_exact = (indexed_elem.element.name == query);
        const bool is_norm_exact = (norm_elem_name == norm_raw_query);
        const bool is_prefix =
            (!norm_raw_query.empty() && norm_elem_name.starts_with(norm_raw_query));

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
    if (options.ranker_type == RankerType::Baseline) {
        return rank::BaselineRanker::rank(candidates, query, query_terms, options.max_results);
    }

    rank::CorpusStats corpus_stats{
        .total_documents = index_.element_count(),
        .avg_doc_length = index_.avg_element_length(),
        .doc_frequencies = {},
    };
    for (const auto& term : query_terms) {
        corpus_stats.doc_frequencies[term] = index_.document_frequency(term);
    }

    if (options.ranker_type == RankerType::BM25) {
        return rank::BM25Ranker::rank(candidates, corpus_stats, query, query_terms, {},
                                      options.max_results);
    }

    return rank::CodeAwareRanker::rank(candidates, corpus_stats, query, query_terms, {},
                                       options.max_results);
}

}  // namespace amoeba::index
