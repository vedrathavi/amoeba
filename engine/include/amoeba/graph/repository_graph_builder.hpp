#pragma once

#include "amoeba/graph/call_extractor.hpp"
#include "amoeba/graph/import_extractor.hpp"
#include "amoeba/graph/inheritance_extractor.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/parsed_file.hpp"

#include <cstddef>
#include <vector>

namespace amoeba::graph {

/**
 * @brief Aggregate cross-file metrics and storage footprint from building the unified repository
 * graph.
 */
struct GraphBuildMetrics {
    std::size_t total_source_bytes{0};
    std::size_t total_files{0};
    std::size_t total_elements{0};
    std::size_t total_relationships{0};
    std::size_t file_level_relationships{0};    ///< Imports, Includes, Contains
    std::size_t symbol_level_relationships{0};  ///< InheritsFrom, Implements, Calls, References

    std::size_t contains_relationships{0};
    std::size_t import_relationships{0};
    std::size_t include_relationships{0};
    std::size_t inheritance_relationships{0};
    std::size_t implements_relationships{0};
    std::size_t call_relationships{0};
    std::size_t reference_relationships{0};

    std::size_t estimated_graph_memory_bytes{0};
    double graph_to_source_ratio{0.0};  ///< estimated_graph_memory_bytes / total_source_bytes
};

/**
 * @brief Comprehensive build result containing the populated graph and extraction metrics across
 * all layers.
 */
struct GraphBuildResult {
    GraphBuildMetrics metrics;
    ImportExtractionResult import_result;
    InheritanceExtractionResult inheritance_result;
    CallExtractionResult call_result;
};

/**
 * @brief Unified coordinator for cross-file relationship integration.
 *
 * Integrates all multi-level structural facts without redundant passes or duplicated edges:
 * 1. Multi-level containment hierarchy (`CONTAINS`: File -> Class/Function, Class -> Method)
 * 2. File and module imports (`IMPORTS`, `INCLUDES`)
 * 3. Type hierarchies (`INHERITS_FROM`, `IMPLEMENTS`)
 * 4. Call graph and symbol references (`CALLS`, `REFERENCES`)
 */
class RepositoryGraphBuilder {
public:
    RepositoryGraphBuilder() = default;
    ~RepositoryGraphBuilder() = default;

    /**
     * @brief Builds a unified cross-file relationship graph from an InvertedIndex.
     * @param index The indexed repository elements and files.
     * @param[out] graph The RelationshipGraph to populate with all cross-file directed edges.
     * @return Full graph build results, sub-extractor results, and storage metrics.
     */
    static GraphBuildResult build_repository_graph(const index::InvertedIndex& index,
                                                   RelationshipGraph& graph);

    /**
     * @brief Builds a unified cross-file relationship graph from a vector of ParsedFiles.
     * @param files The parsed source files.
     * @param[out] graph The RelationshipGraph to populate.
     * @return Full graph build results and storage metrics.
     */
    static GraphBuildResult build_repository_graph(const std::vector<parser::ParsedFile>& files,
                                                   RelationshipGraph& graph);
};

}  // namespace amoeba::graph
