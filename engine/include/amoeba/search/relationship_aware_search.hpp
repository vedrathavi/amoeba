#pragma once

#include "amoeba/graph/focused_subgraph.hpp"
#include "amoeba/graph/graph_query_service.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/index/search_engine.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::search {

/**
 * @brief Representation of an adjacent related code element providing structural context.
 */
struct RelatedElementContext {
    graph::ElementId element_id{0};
    parser::CodeElement element;
    std::filesystem::path file_path;
    graph::RelationshipKind relationship_kind{graph::RelationshipKind::Calls};
    bool is_incoming{false};  ///< true if incoming (e.g. caller), false if outgoing (e.g. callee)
    std::string explanation;  ///< Human-readable, grounded structural explanation
};

/**
 * @brief Search result combining lexical ranking score with contextual relationship expansion.
 */
struct RelationshipAwareSearchResult {
    index::SearchResult primary_result;
    graph::ElementId element_id{0};
    std::vector<RelatedElementContext> related_elements;
    std::vector<std::string> structural_explanations;
    graph::FocusedSubgraph context_subgraph;
};

/**
 * @brief Configuration parameters for relationship expansion during search.
 */
struct RelationshipExpansionOptions {
    bool enable_expansion{true};
    std::size_t max_depth{1};             ///< Search depth for relationship traversal
    std::size_t max_related_elements{5};  ///< Maximum related context items per result
    std::optional<graph::RelationshipKind> kind_filter{std::nullopt};
    bool include_callers{true};
    bool include_callees{true};
    bool include_type_hierarchy{true};
    bool include_dependencies{true};
};

/**
 * @brief Relationship-aware search engine combining lexical ranking with graph context.
 *
 * Preserves Phase 4 ranking algorithms (Baseline, BM25, CodeAware) and enriches top results
 * with grounded structural context (callers, callees, hierarchies, dependencies).
 */
class RelationshipAwareSearchEngine {
public:
    /**
     * @brief Constructs a RelationshipAwareSearchEngine wrapping an InvertedIndex and
     * RelationshipGraph.
     */
    RelationshipAwareSearchEngine(const index::InvertedIndex& index,
                                  const graph::RelationshipGraph& graph);

    /**
     * @brief Executes a search query with optional structural relationship expansion.
     * @param query Search query string.
     * @param search_opts Lexical retrieval & ranking options (from Phase 4).
     * @param expansion_opts Relationship expansion parameters.
     * @return Ordered list of relationship-aware search results.
     */
    [[nodiscard]] std::vector<RelationshipAwareSearchResult>
    search(std::string_view query, const index::SearchOptions& search_opts = {},
           const RelationshipExpansionOptions& expansion_opts = {}) const;

    /**
     * @brief Expands a single search result with structural relationship context.
     * @param result Primary lexical search result.
     * @param element_id Unique ElementId in the graph.
     * @param expansion_opts Expansion configuration.
     * @return Enriched relationship-aware result.
     */
    [[nodiscard]] RelationshipAwareSearchResult
    expand_result(const index::SearchResult& result, graph::ElementId element_id,
                  const RelationshipExpansionOptions& expansion_opts = {}) const;

private:
    const index::InvertedIndex& index_;
    const graph::RelationshipGraph& graph_;
    index::SearchEngine search_engine_;
    graph::GraphQueryService query_service_;

    [[nodiscard]] std::optional<graph::ElementId>
    find_element_id(const index::SearchResult& result) const noexcept;
};

}  // namespace amoeba::search
