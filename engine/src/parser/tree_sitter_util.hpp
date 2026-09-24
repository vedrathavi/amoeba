#pragma once

#include "amoeba/parser/code_element.hpp"

#include <string>
#include <string_view>
#include <tree_sitter/api.h>

namespace amoeba::parser::internal {

using namespace std;

[[nodiscard]] inline string get_node_text(TSNode node, string_view source) {
    const uint32_t start = ts_node_start_byte(node);
    const uint32_t end = ts_node_end_byte(node);
    if (start >= source.size() || end > source.size() || start >= end) {
        return "";
    }
    return string(source.substr(start, end - start));
}

[[nodiscard]] inline SourceRange get_node_range(TSNode node) {
    const TSPoint start = ts_node_start_point(node);
    const TSPoint end = ts_node_end_point(node);
    return SourceRange{
        .start =
            {
                .line = start.row + 1,
                .column = start.column + 1,
                .byte_offset = ts_node_start_byte(node),
            },
        .end =
            {
                .line = end.row + 1,
                .column = end.column + 1,
                .byte_offset = ts_node_end_byte(node),
            },
    };
}

}  // namespace amoeba::parser::internal
