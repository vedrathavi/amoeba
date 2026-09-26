#include "amoeba/source/source_snippet_reader.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace amoeba::source {

namespace {

struct LineIndex {
    std::vector<std::size_t> line_starts;  ///< 0-indexed byte offset of each line start

    explicit LineIndex(std::string_view source) {
        line_starts.push_back(0);
        for (std::size_t i = 0; i < source.size(); ++i) {
            if (source[i] == '\n' && (i + 1 < source.size())) {
                line_starts.push_back(i + 1);
            }
        }
    }

    [[nodiscard]] std::size_t total_lines() const noexcept { return line_starts.size(); }

    [[nodiscard]] std::size_t start_byte_for_line(uint32_t line_1indexed) const {
        if (line_1indexed == 0 || line_1indexed > line_starts.size()) {
            throw std::out_of_range("Line number out of range");
        }
        return line_starts[line_1indexed - 1];
    }

    [[nodiscard]] std::size_t end_byte_for_line(uint32_t line_1indexed,
                                                std::size_t source_size) const {
        if (line_1indexed == 0 || line_1indexed > line_starts.size()) {
            throw std::out_of_range("Line number out of range");
        }
        if (line_1indexed < line_starts.size()) {
            return line_starts[line_1indexed];
        }
        return source_size;
    }
};

}  // namespace

SourceExcerpt
SourceSnippetReader::read_range_from_text(std::string_view source, const parser::SourceRange& range,
                                          uint32_t context_lines,
                                          const std::filesystem::path& file_path) const {
    // 1. Validate start <= end
    const bool has_byte_offsets = (range.start.byte_offset > 0 || range.end.byte_offset > 0);
    if (has_byte_offsets) {
        if (range.start.byte_offset > range.end.byte_offset) {
            throw std::invalid_argument(
                "Invalid SourceRange: start byte offset is greater than end byte offset");
        }
    } else {
        if (range.start.line > range.end.line ||
            (range.start.line == range.end.line && range.start.column > range.end.column)) {
            throw std::invalid_argument(
                "Invalid SourceRange: start line/column is after end line/column");
        }
    }

    // 2. Handle empty source buffer
    if (source.empty()) {
        if (range.start.byte_offset == 0 && range.end.byte_offset == 0 && range.start.line <= 1 &&
            range.end.line <= 1) {
            return SourceExcerpt{
                .file_path = file_path,
                .requested_range = range,
                .text = "",
                .start_line = 1,
                .end_line = 1,
                .context_lines_before = 0,
                .context_lines_after = 0,
                .has_context_lines = false,
            };
        }
        throw std::out_of_range("SourceRange out of bounds for empty source");
    }

    // 3. Build line index
    const LineIndex line_index(source);
    const uint32_t total_lines = static_cast<uint32_t>(line_index.total_lines());

    // 4. Exact range vs context-expanded range
    if (context_lines == 0) {
        if (has_byte_offsets) {
            if (range.start.byte_offset > source.size() || range.end.byte_offset > source.size()) {
                throw std::out_of_range("SourceRange byte offsets exceed source length");
            }
            const std::size_t len = range.end.byte_offset - range.start.byte_offset;
            const std::string text(source.substr(range.start.byte_offset, len));
            return SourceExcerpt{
                .file_path = file_path,
                .requested_range = range,
                .text = text,
                .start_line = (range.start.line > 0 ? range.start.line : 1),
                .end_line = (range.end.line > 0 ? range.end.line : 1),
                .context_lines_before = 0,
                .context_lines_after = 0,
                .has_context_lines = false,
            };
        }

        // Fallback to line-based extraction when byte offsets are 0
        const uint32_t req_start_line = (range.start.line > 0 ? range.start.line : 1);
        const uint32_t req_end_line = (range.end.line > 0 ? range.end.line : req_start_line);
        if (req_start_line > total_lines || req_end_line > total_lines) {
            throw std::out_of_range("SourceRange line numbers exceed total lines in source");
        }
        const std::size_t start_byte = line_index.start_byte_for_line(req_start_line);
        const std::size_t end_byte = line_index.end_byte_for_line(req_end_line, source.size());
        const std::string text(source.substr(start_byte, end_byte - start_byte));
        return SourceExcerpt{
            .file_path = file_path,
            .requested_range = range,
            .text = text,
            .start_line = req_start_line,
            .end_line = req_end_line,
            .context_lines_before = 0,
            .context_lines_after = 0,
            .has_context_lines = false,
        };
    }

    // 5. Context-expanded line range
    const uint32_t base_start_line = (range.start.line > 0 ? range.start.line : 1);
    const uint32_t base_end_line = (range.end.line > 0 ? range.end.line : base_start_line);
    if (base_start_line > total_lines || base_end_line > total_lines) {
        throw std::out_of_range("SourceRange line numbers exceed total lines in source");
    }

    const uint32_t exp_start_line =
        (base_start_line > context_lines) ? (base_start_line - context_lines) : 1;
    const uint32_t exp_end_line = std::min(total_lines, base_end_line + context_lines);

    const std::size_t start_byte = line_index.start_byte_for_line(exp_start_line);
    const std::size_t end_byte = line_index.end_byte_for_line(exp_end_line, source.size());
    const std::string text(source.substr(start_byte, end_byte - start_byte));

    return SourceExcerpt{
        .file_path = file_path,
        .requested_range = range,
        .text = text,
        .start_line = exp_start_line,
        .end_line = exp_end_line,
        .context_lines_before = (base_start_line - exp_start_line),
        .context_lines_after = (exp_end_line - base_end_line),
        .has_context_lines = true,
    };
}

SourceExcerpt SourceSnippetReader::read_range(const std::filesystem::path& file_path,
                                              const parser::SourceRange& range,
                                              uint32_t context_lines) const {
    if (!std::filesystem::exists(file_path)) {
        throw std::invalid_argument("File does not exist: " + file_path.string());
    }
    if (!std::filesystem::is_regular_file(file_path)) {
        throw std::invalid_argument("Path is not a regular file: " + file_path.string());
    }

    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open()) {
        throw std::invalid_argument("Unable to open file for reading: " + file_path.string());
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string content = ss.str();

    return read_range_from_text(content, range, context_lines, file_path);
}

std::optional<SourceExcerpt>
SourceSnippetReader::try_read_range(const std::filesystem::path& file_path,
                                    const parser::SourceRange& range,
                                    uint32_t context_lines) const noexcept {
    try {
        return read_range(file_path, range, context_lines);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<SourceExcerpt> SourceSnippetReader::try_read_range_from_text(
    std::string_view source, const parser::SourceRange& range, uint32_t context_lines,
    const std::filesystem::path& file_path) const noexcept {
    try {
        return read_range_from_text(source, range, context_lines, file_path);
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace amoeba::source
