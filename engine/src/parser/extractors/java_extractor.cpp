#include "extractors.hpp"
#include "parser/tree_sitter_util.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace amoeba::parser::internal {

using namespace std;

void extract_java(TSNode node, string_view source, const string& current_class,
                  vector<CodeElement>& out_elements) {
    if (ts_node_is_null(node)) {
        return;
    }

    const string_view type = ts_node_type(node);

    if (type == "class_declaration" || type == "interface_declaration" ||
        type == "enum_declaration" || type == "record_declaration") {
        const bool is_interface = (type == "interface_declaration");
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        string class_name = !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";
        if (!class_name.empty()) {
            out_elements.push_back(CodeElement{
                .kind = is_interface ? ElementKind::Interface : ElementKind::Class,
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
                extract_java(ts_node_named_child(body_node, i), source, class_name, out_elements);
            }
        }
        return;
    }

    if (type == "method_declaration" || type == "constructor_declaration") {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        string method_name = !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";
        if (!method_name.empty()) {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::Method,
                .name = method_name,
                .location = get_node_range(node),
                .parent_context = current_class,
                .detail = "",
            });
        }
        TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body_node)) {
            extract_java(body_node, source, current_class, out_elements);
        }
        return;
    }

    if (type == "import_declaration") {
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

    if (type == "method_invocation") {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        string call_name = !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";
        if (!call_name.empty()) {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::Call,
                .name = call_name,
                .location = get_node_range(node),
                .parent_context = current_class,
                .detail = "",
            });
        }
    }

    const uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        extract_java(ts_node_named_child(node, i), source, current_class, out_elements);
    }
}

}  // namespace amoeba::parser::internal
