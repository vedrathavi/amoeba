#pragma once

#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/graph/repository_graph_builder.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/parsed_file.hpp"
#include "amoeba/parser/source_parser.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::graph {

/**
 * @brief Result statistics and status from an incremental file addition, modification, or removal.
 */
struct IncrementalUpdateResult {
    bool success{false};
    std::size_t elements_removed{0};
    std::size_t elements_added{0};
    std::size_t relationships_removed{0};
    std::size_t relationships_added{0};
    std::string message;
};

/**
 * @brief Incremental graph update manager for single-file mutations.
 *
 * Core Principles:
 * 1. Transactional safety: A failed parse preserves previous valid graph and index state.
 * 2. Complete stale edge eradication: Deleted elements have all incoming and outgoing edges
 * removed.
 * 3. Git-independent: Operates cleanly on paths, content strings, and parsed representations.
 * 4. Rename/Move as Delete + Add: Decomposes rename/move into safe atomic removal and addition.
 */
class IncrementalGraphUpdater {
public:
    /**
     * @brief Constructs an IncrementalGraphUpdater managing an index and relationship graph.
     */
    IncrementalGraphUpdater(index::InvertedIndex& index, RelationshipGraph& graph,
                            parser::SourceParser& parser);

    /**
     * @brief Incrementally adds a newly created file.
     * @param file_path File path.
     * @param source Source code text.
     * @return Result metrics.
     */
    IncrementalUpdateResult add_file(const std::filesystem::path& file_path,
                                     std::string_view source);

    /**
     * @brief Incrementally replaces an existing file with new source code.
     * Transactionally safe: If parsing fails, existing state is preserved.
     * @param file_path File path.
     * @param new_source Updated source code text.
     * @return Result metrics.
     */
    IncrementalUpdateResult replace_file(const std::filesystem::path& file_path,
                                         std::string_view new_source);

    /**
     * @brief Incrementally removes an existing file.
     * @param file_path File path.
     * @return Result metrics.
     */
    IncrementalUpdateResult remove_file(const std::filesystem::path& file_path);

    /**
     * @brief Returns the total number of files currently tracked.
     */
    [[nodiscard]] std::size_t file_count() const noexcept { return files_.size(); }

    /**
     * @brief Checks if a specific file path is currently tracked.
     */
    [[nodiscard]] bool has_file(const std::filesystem::path& file_path) const noexcept;

    /**
     * @brief Returns a reference to the tracked parsed files.
     */
    [[nodiscard]] const std::vector<parser::ParsedFile>& tracked_files() const noexcept {
        return files_;
    }

private:
    index::InvertedIndex& index_;
    RelationshipGraph& graph_;
    parser::SourceParser& parser_;
    std::vector<parser::ParsedFile> files_;
    std::unordered_map<std::string, std::string> file_contents_;

    void rebuild_internal();
};

}  // namespace amoeba::graph
