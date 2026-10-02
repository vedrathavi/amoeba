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

[[nodiscard]] inline string clean_documentation(string_view text, size_t max_len = 300) {
    if (text.empty()) return "";

    string clean;
    clean.reserve(min(text.size(), max_len + 10));

    bool in_space = false;
    size_t i = 0;

    // Skip leading comment markers
    if (text.starts_with("/**") || text.starts_with("/*!")) i += 3;
    else if (text.starts_with("/*") || text.starts_with("//") || text.starts_with("##")) i += 2;
    else if (text.starts_with("#") || text.starts_with("/")) i += 1;
    else if (text.starts_with("\"\"\"") || text.starts_with("'''")) i += 3;

    for (; i < text.size(); ++i) {
        char c = text[i];
        // Strip comment delimiters
        if (c == '*' || c == '/' || c == '#' || c == '"' || c == '\'') {
            if (i + 1 < text.size() && (text[i] == '*' && text[i+1] == '/')) { i++; continue; }
            if (i + 2 < text.size() && (text.substr(i, 3) == "\"\"\"" || text.substr(i, 3) == "'''")) { i += 2; continue; }
            if (i + 1 < text.size() && (text[i] == '/' && text[i+1] == '/')) { i++; continue; }
        }
        if (isspace(static_cast<unsigned char>(c))) {
            if (!clean.empty() && !in_space) {
                clean.push_back(' ');
                in_space = true;
            }
        } else {
            clean.push_back(c);
            in_space = false;
        }
        if (clean.size() >= max_len) break;
    }

    while (!clean.empty() && (isspace(static_cast<unsigned char>(clean.back())) || clean.back() == '*' || clean.back() == '/')) {
        clean.pop_back();
    }
    // Trim leading whitespace/asterisks that might have accumulated
    size_t start = 0;
    while (start < clean.size() && (isspace(static_cast<unsigned char>(clean[start])) || clean[start] == '*' || clean[start] == '/')) {
        start++;
    }
    if (start > 0) {
        clean = clean.substr(start);
    }
    if (clean.size() >= max_len) {
        clean.append("...");
    }
    return clean;
}

[[nodiscard]] inline string extract_preceding_doc(TSNode node, string_view source, size_t max_len = 300) {
    TSNode target = node;
    TSNode parent = ts_node_parent(node);
    if (!ts_node_is_null(parent)) {
        string_view ptype = ts_node_type(parent);
        if (ptype == "decorated_definition" || ptype == "export_statement" || ptype == "attribute_item") {
            target = parent;
        }
    }
    TSNode prev = ts_node_prev_sibling(target);
    while (!ts_node_is_null(prev)) {
        string_view ptype = ts_node_type(prev);
        if (ptype == "comment" || ptype == "line_comment" || ptype == "block_comment" || ptype == "doc_comment") {
            return clean_documentation(get_node_text(prev, source), max_len);
        }
        if (ts_node_is_named(prev)) {
            break;
        }
        prev = ts_node_prev_sibling(prev);
    }
    return "";
}

[[nodiscard]] inline string extract_python_docstring(TSNode body_node, string_view source, size_t max_len = 300) {
    if (ts_node_is_null(body_node)) return "";
    const uint32_t count = ts_node_named_child_count(body_node);
    if (count == 0) return "";
    TSNode first_stmt = ts_node_named_child(body_node, 0);
    string_view stmt_type = ts_node_type(first_stmt);
    if (stmt_type == "expression_statement") {
        if (ts_node_named_child_count(first_stmt) > 0) {
            TSNode expr = ts_node_named_child(first_stmt, 0);
            if (string_view(ts_node_type(expr)) == "string") {
                return clean_documentation(get_node_text(expr, source), max_len);
            }
        }
    }
    return "";
}

}  // namespace amoeba::parser::internal

