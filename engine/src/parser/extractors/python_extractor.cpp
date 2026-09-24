#include "extractors.hpp"
#include "parser/tree_sitter_util.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace amoeba::parser::internal {

using namespace std;

void extract_python(TSNode node, string_view source, const string& current_class,
                    vector<CodeElement>& out_elements) {
    if (ts_node_is_null(node)) {
        return;
    }

    const string_view type = ts_node_type(node);

    if (type == "class_definition") {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        string class_name = !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";
        if (!class_name.empty()) {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::Class,
                .name = class_name,
                .location = get_node_range(node),
                .parent_context = current_class,
                .detail = "",
            });
        }

        TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body_node)) {
            const uint32_t child_count = ts_node_named_child_count(body_node);
            for (uint32_t i = 0; i < child_count; ++i) {
                extract_python(ts_node_named_child(body_node, i), source, class_name, out_elements);
            }
        }
        return;
    }

    if (type == "function_definition") {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        string func_name = !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";
        if (!func_name.empty()) {
            out_elements.push_back(CodeElement{
                .kind = !current_class.empty() ? ElementKind::Method : ElementKind::Function,
                .name = func_name,
                .location = get_node_range(node),
                .parent_context = current_class,
                .detail = "",
            });
        }

        TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body_node)) {
            extract_python(body_node, source, current_class, out_elements);
        }
        return;
    }

    if (type == "import_statement" || type == "import_from_statement") {
        string import_text = get_node_text(node, source);
        if (!import_text.empty()) {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::Include,
                .name = import_text,
                .location = get_node_range(node),
                .parent_context = current_class,
                .detail = "",
            });
        }
        return;
    }

    if (type == "call") {
        TSNode func_node = ts_node_child_by_field_name(node, "function", 8);
        string call_name;
        if (!ts_node_is_null(func_node)) {
            const string_view func_type = ts_node_type(func_node);
            if (func_type == "attribute") {
                TSNode attr_node = ts_node_child_by_field_name(func_node, "attribute", 9);
                if (!ts_node_is_null(attr_node)) {
                    call_name = get_node_text(attr_node, source);
                }
            }
            if (call_name.empty()) {
                call_name = get_node_text(func_node, source);
            }
        }
        if (!call_name.empty()) {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::Call,
                .name = call_name,
                .location = get_node_range(node),
                .parent_context = current_class,
                .detail = "",
            });
        }
        TSNode args = ts_node_child_by_field_name(node, "arguments", 9);
        if (!ts_node_is_null(args)) {
            extract_python(args, source, current_class, out_elements);
        }
        return;
    }

    const uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        extract_python(ts_node_named_child(node, i), source, current_class, out_elements);
    }
}

}  // namespace amoeba::parser::internal
