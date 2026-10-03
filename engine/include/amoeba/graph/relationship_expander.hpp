#pragma once

#include "amoeba/graph/relationship_evidence.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/graph/relationship_kind.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/code_element.hpp"
#include "amoeba/retrieval/primary_search_result.hpp"
#include "amoeba/source/source_snippet_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace amoeba::graph {

/**
 * @brief Configuration options for bounded relationship expansion.
 */
struct ExpansionConfig {
    /// Maximum traversal depth: 0 = none, 1 = direct 1-hop, 2 = 2-hop.
    uint32_t max_depth{1};

    /// Maximum total related evidence units to select and attach.
    std::size_t max_units{3};

    /// Maximum additional source code characters allowed across all expanded units.
    std::size_t max_source_characters{4000};

    /// Number of context lines before/after location when extracting snippet.
    uint32_t snippet_context_lines{5};

    /// Whether to prioritize neighbors whose names or files match query terms.
    bool prioritize_query_relevance{true};

    /// Set of relationship kinds allowed for traversal.
    std::unordered_set<RelationshipKind> allowed_kinds{
        RelationshipKind::Contains,
        RelationshipKind::Calls,
        RelationshipKind::References,
        RelationshipKind::Imports,
        RelationshipKind::Includes,
        RelationshipKind::InheritsFrom,
        RelationshipKind::Implements
    };
};

/**
 * @brief Represents a single expanded relationship candidate with full provenance.
 */
struct ExpandedCandidate {
    index::ElementId element_id{0};
    std::string name;
    parser::ElementKind kind{parser::ElementKind::Unknown};
    std::filesystem::path file_path;
    parser::SourceRange location;
    std::string parent_context;
    std::string detail;

    // Provenance details
    index::ElementId seed_element_id{0};
    std::string seed_symbol_name;
    RelationshipKind relationship_kind{RelationshipKind::Calls};
    RelationshipDirection direction{RelationshipDirection::Outgoing};
    uint32_t depth{1};

    // Ranking and excerpt
    double relevance_score{0.0};
    std::string source_excerpt_text;

    [[nodiscard]] bool operator==(const ExpandedCandidate&) const = default;
};

/**
 * @brief Performs bounded, typed relationship expansion from primary retrieval seeds.
 *
 * Responsibilities:
 *  1. Traverse 1-hop and 2-hop edges from primary seeds within configurable limits.
 *  2. Deduplicate units reached through multiple paths or cycles.
 *  3. Score and rank candidates using relationship kind, direction, depth, and query overlap.
 *  4. Enforce strict unit and character budgets.
 *  5. Preserve full provenance (seed ID, symbol, edge kind, direction, depth, location).
 */
class RelationshipExpander {
public:
    RelationshipExpander(const RelationshipGraph& graph, const index::InvertedIndex& index,
                         const source::SourceSnippetReader& snippet_reader);

    ~RelationshipExpander() = default;

    RelationshipExpander(const RelationshipExpander&) = delete;
    RelationshipExpander& operator=(const RelationshipExpander&) = delete;
    RelationshipExpander(RelationshipExpander&&) = delete;
    RelationshipExpander& operator=(RelationshipExpander&&) = delete;

    /**
     * @brief Expands related evidence from a set of primary search results.
     *
     * @param query The user question text (used for relevance ranking).
     * @param seed_results The primary retrieved search results.
     * @param config The expansion budget and depth configuration.
     * @return Deterministically ordered and budgeted list of expanded candidates.
     */
    [[nodiscard]] std::vector<ExpandedCandidate>
    expand(std::string_view query,
           std::span<const retrieval::PrimarySearchResult> seed_results,
           const ExpansionConfig& config = {}) const;

private:
    const RelationshipGraph& graph_;
    const index::InvertedIndex& index_;
    const source::SourceSnippetReader& snippet_reader_;

    [[nodiscard]] double compute_candidate_score(
        const ExpandedCandidate& cand,
        const std::vector<std::string>& query_subjects,
        double seed_hybrid_score) const;
};

}  // namespace amoeba::graph
