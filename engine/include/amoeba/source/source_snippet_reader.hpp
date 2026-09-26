#pragma once

#include "amoeba/parser/code_element.hpp"
#include "amoeba/source/source_excerpt.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace amoeba::source {

/**
 * @brief Reads and extracts exact source code excerpts corresponding to SourceRanges.
 *
 * Responsibilities:
 *  1. Open/read a source file from disk or process in-memory source buffers.
 *  2. Locate requested SourceRange (using exact UTF-8 byte offsets or line boundaries).
 *  3. Extract verbatim source text preserving indentation, comments, and whitespace.
 *  4. Optionally include surrounding context lines.
 *  5. Provide safe, deterministic error handling for missing files, invalid ranges, and EOF.
 */
class SourceSnippetReader {
public:
    SourceSnippetReader() = default;
    ~SourceSnippetReader() = default;

    /**
     * @brief Extracts source excerpt from a file on disk.
     *
     * @param file_path Path to the source file.
     * @param range Source range specifying start and end locations.
     * @param context_lines Number of surrounding source lines to include before and after.
     * @throws std::invalid_argument if file does not exist, is not regular, cannot be opened, or
     * range is inverted.
     * @throws std::out_of_range if range is out of bounds for the file.
     * @return SourceExcerpt containing verbatim source text.
     */
    [[nodiscard]] SourceExcerpt read_range(const std::filesystem::path& file_path,
                                           const parser::SourceRange& range,
                                           uint32_t context_lines = 0) const;

    /**
     * @brief Extracts source excerpt from an in-memory source buffer.
     *
     * @param source Raw source code text.
     * @param range Source range specifying start and end locations.
     * @param context_lines Number of surrounding source lines to include before and after.
     * @param file_path Optional path for metadata in the returned SourceExcerpt.
     * @throws std::invalid_argument if range is inverted (start > end).
     * @throws std::out_of_range if range is out of bounds for the source buffer.
     * @return SourceExcerpt containing verbatim source text.
     */
    [[nodiscard]] SourceExcerpt
    read_range_from_text(std::string_view source, const parser::SourceRange& range,
                         uint32_t context_lines = 0,
                         const std::filesystem::path& file_path = {}) const;

    /**
     * @brief Non-throwing variant of read_range for file on disk.
     *
     * Returns std::nullopt on any error (missing file, invalid range, out of bounds).
     */
    [[nodiscard]] std::optional<SourceExcerpt>
    try_read_range(const std::filesystem::path& file_path, const parser::SourceRange& range,
                   uint32_t context_lines = 0) const noexcept;

    /**
     * @brief Non-throwing variant of read_range_from_text for in-memory source buffer.
     *
     * Returns std::nullopt on any error (invalid range, out of bounds).
     */
    [[nodiscard]] std::optional<SourceExcerpt>
    try_read_range_from_text(std::string_view source, const parser::SourceRange& range,
                             uint32_t context_lines = 0,
                             const std::filesystem::path& file_path = {}) const noexcept;
};

}  // namespace amoeba::source
