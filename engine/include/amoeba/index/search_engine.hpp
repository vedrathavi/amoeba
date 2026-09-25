#pragma once

#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/code_element.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::index {

using namespace std;
using namespace std::filesystem;

/**
 * @brief Strategy for combining multi-term queries.
 */
enum class MatchMode {
    AnyTerm,   // OR: element matches at least one term
    AllTerms,  // AND: element must match all terms
};

/**
 * @brief Ranking algorithm to use for scoring retrieved candidates.
 */
enum class RankerType {
    Baseline,
    BM25,
    CodeAware,
};

/**
 * @brief Configuration options for search execution.
 */
struct SearchOptions {
    MatchMode match_mode{MatchMode::AnyTerm};
    RankerType ranker_type{RankerType::Baseline};
    size_t max_results{100};
    optional<parser::ElementKind> kind_filter{nullopt};
};

/**
 * @brief Search result item containing matched CodeElement and file location.
 */
struct SearchResult {
    parser::CodeElement element;
    path file_path;
    string language;
    uint32_t match_count{0};
    bool exact_name_match{false};
    double score{0.0};

    [[nodiscard]] bool operator==(const SearchResult&) const = default;
};

/**
 * @brief High-level retrieval engine for searching an InvertedIndex.
 */
class SearchEngine {
public:
    explicit SearchEngine(const InvertedIndex& index) noexcept : index_(index) {}

    /**
     * @brief Executes a lexical query against the inverted index.
     * @param query User query string (e.g. "authenticate", "UserAuth", "service").
     * @param options Search configuration options.
     * @return Ranked list of matching SearchResults.
     */
    [[nodiscard]] vector<SearchResult> search(string_view query,
                                              const SearchOptions& options = {}) const;

private:
    const InvertedIndex& index_;
};

}  // namespace amoeba::index
