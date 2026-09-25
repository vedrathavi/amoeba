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
 * @brief Resolution record for an individual import or include statement.
 */
struct ImportResolution {
    ElementId source_element_id{0};
    std::filesystem::path source_file_path;
    std::string raw_import_target;
    RelationshipKind kind{RelationshipKind::Imports};
    std::optional<ElementId> target_element_id{std::nullopt};
    std::optional<index::FileId> target_file_id{std::nullopt};
    std::filesystem::path resolved_file_path;
    bool is_resolved{false};
};

/**
 * @brief Aggregate statistics and detailed resolution records from import extraction.
 */
struct ImportExtractionResult {
    std::size_t total_imports_found{0};
    std::size_t resolved_imports{0};
    std::size_t unresolved_imports{0};
    std::vector<ImportResolution> resolutions;
};

/**
 * @brief Extracts syntactic import and include dependencies and builds the RelationshipGraph.
 *
 * Implements sound resolution boundaries:
 * - Direct repository files are matched and connected via ElementIds.
 * - External/standard library dependencies (e.g., <vector>, "react", "os") are recognized as
 * unresolved without fabricating synthetic graph nodes.
 */
class ImportExtractor {
public:
    ImportExtractor() = default;
    ~ImportExtractor() = default;

    /**
     * @brief Extracts import/include relationships from an InvertedIndex and populates the graph.
     * @param index The indexed repository elements and files.
     * @param[out] graph The relationship graph to populate with resolved directed edges.
     * @return Detailed extraction metrics and individual resolution records.
     */
    static ImportExtractionResult extract_and_populate(const index::InvertedIndex& index,
                                                       RelationshipGraph& graph);

    /**
     * @brief Extracts import/include relationships from a vector of ParsedFile representations.
     * @param files The parsed source files.
     * @param[out] graph The relationship graph to populate.
     * @return Detailed extraction metrics.
     */
    static ImportExtractionResult extract_and_populate(const std::vector<parser::ParsedFile>& files,
                                                       RelationshipGraph& graph);

    /**
     * @brief Normalizes raw import strings (strips quotes, angle brackets, whitespace, trailing
     * semicolons).
     */
    [[nodiscard]] static std::string normalize_import_path(std::string_view raw);

    /**
     * @brief Determines whether an import represents an include (C/C++/HTML/CSS) or an import
     * (JS/Python/Java/Go/Rust).
     */
    [[nodiscard]] static RelationshipKind determine_kind(std::string_view language,
                                                         std::string_view raw_target);
};

}  // namespace amoeba::graph
