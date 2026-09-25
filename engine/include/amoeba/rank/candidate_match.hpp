#pragma once

#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/code_element.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace amoeba::rank {

using namespace std;

/**
 * @brief Intermediate candidate representation before ranking.
 */
struct CandidateMatch {
    index::ElementId element_id{0};
    parser::CodeElement element;
    filesystem::path file_path;
    string language;
    vector<string> matched_terms;
    unordered_map<string, uint32_t> term_frequencies;
    size_t doc_length{0};
    bool exact_name_match{false};
    bool normalized_name_match{false};
    bool prefix_name_match{false};
    bool matched_in_name{false};
    bool matched_in_context{false};
    bool matched_in_detail{false};
    bool matched_in_path{false};
    uint32_t name_match_count{0};
    uint32_t context_match_count{0};
    uint32_t detail_match_count{0};
    uint32_t path_match_count{0};
};

/**
 * @brief Corpus-wide statistical summary needed for BM25 ranking.
 */
struct CorpusStats {
    size_t total_documents{0};                      // N: Total number of indexed CodeElements
    double avg_doc_length{0.0};                     // avgdl: Average searchable tokens per element
    unordered_map<string, size_t> doc_frequencies;  // DF(t): Document frequencies for terms
};

}  // namespace amoeba::rank
