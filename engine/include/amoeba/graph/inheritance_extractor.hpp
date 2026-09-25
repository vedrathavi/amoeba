#pragma once

#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/parsed_file.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::graph {

/**
 * @brief Resolution record for an individual inheritance or implements clause.
 */
struct InheritanceResolution {
    ElementId source_element_id{0};
    std::string source_class_name;
    std::string raw_target_name;
    RelationshipKind kind{RelationshipKind::InheritsFrom};
    std::optional<ElementId> target_element_id{std::nullopt};
    bool is_resolved{false};
};

/**
 * @brief Aggregate statistics and detailed resolution records from inheritance extraction.
 */
struct InheritanceExtractionResult {
    std::size_t total_clauses_found{0};
    std::size_t resolved_inheritances{0};
    std::size_t unresolved_inheritances{0};
    std::vector<InheritanceResolution> resolutions;
};

/**
 * @brief Extracts structural inheritance (INHERITS_FROM) and interface implementation (IMPLEMENTS)
 * relationships.
 *
 * Implements sound resolution boundaries:
 * - Confidently resolved class/interface names are connected via ElementId edges.
 * - External/unresolved base types (e.g. std::exception, Object, third-party interfaces) are
 * tracked as unresolved without fabricating synthetic graph nodes.
 */
class InheritanceExtractor {
public:
    InheritanceExtractor() = default;
    ~InheritanceExtractor() = default;

    /**
     * @brief Extracts inheritance/implements relationships from an InvertedIndex and populates the
     * graph.
     * @param index The indexed repository elements and files.
     * @param[out] graph The relationship graph to populate with resolved directed edges.
     * @return Detailed extraction metrics and individual resolution records.
     */
    static InheritanceExtractionResult extract_and_populate(const index::InvertedIndex& index,
                                                            RelationshipGraph& graph);

    /**
     * @brief Extracts inheritance/implements relationships from a vector of ParsedFile
     * representations.
     * @param files The parsed source files.
     * @param[out] graph The relationship graph to populate.
     * @return Detailed extraction metrics.
     */
    static InheritanceExtractionResult
    extract_and_populate(const std::vector<parser::ParsedFile>& files, RelationshipGraph& graph);
};

}  // namespace amoeba::graph
