#pragma once

#include "amoeba/parser/code_element.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace amoeba::source {

/**
 * @brief Represents extracted source code text corresponding to a SourceRange.
 *
 * Value object preserving exact original source text (indentation, line breaks,
 * whitespace, punctuation, comments) without tokenization or normalization.
 */
struct SourceExcerpt {
    std::filesystem::path file_path;
    parser::SourceRange requested_range;
    std::string text;
    uint32_t start_line{1};
    uint32_t end_line{1};
    uint32_t context_lines_before{0};
    uint32_t context_lines_after{0};
    bool has_context_lines{false};

    [[nodiscard]] bool operator==(const SourceExcerpt&) const = default;
};

}  // namespace amoeba::source
