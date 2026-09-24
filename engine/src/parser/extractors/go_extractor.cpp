#include "extractors.hpp"
#include "parser/tree_sitter_util.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace amoeba::parser::internal {

using namespace std;

void extract_go(TSNode node, string_view source, const string& current_class,
                vector<CodeElement>& out_elements) {
    if (ts_node_is_null(node)) {
        return;
    }

    const string_view type = ts_node_type(node);

    if (type == "type_declaration") {
        const uint32_t count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode spec = ts_node_named_child(node, i);
            if (string_view(ts_node_type(spec)) == "type_spec") {
                TSNode name_node = ts_node_child_by_field_name(spec, "name", 4);
                TSNode type_node = ts_node_child_by_field_name(spec, "type", 4);
                string type_name =
                    !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";
                string_view inner_type = !ts_node_is_null(type_node) ? ts_node_type(type_node) : "";

                if (!type_name.empty()) {
                    const bool is_interface = (inner_type == "interface_type");
                    out_elements.push_back(CodeElement{
                        .kind = is_interface ? ElementKind::Interface : ElementKind::Struct,
                        .name = type_name,
                        .location = get_node_range(spec),
                        .parent_context = current_class,
                        .detail = "",
                    });
                }
            }
        }
    }

    if (type == "function_declaration") {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        string func_name = !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";
        if (!func_name.empty()) {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::Function,
                .name = func_name,
                .location = get_node_range(node),
                .parent_context = current_class,
                .detail = "",
            });
        }
        TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body_node)) {
            extract_go(body_node, source, current_class, out_elements);
        }
        return;
    }

    if (type == "method_declaration") {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        TSNode recv_node = ts_node_child_by_field_name(node, "receiver", 8);
        string method_name = !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";
        string recv_type = !ts_node_is_null(recv_node) ? get_node_text(recv_node, source) : "";

        if (!method_name.empty()) {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::Method,
                .name = method_name,
                .location = get_node_range(node),
                .parent_context = recv_type,
                .detail = "",
            });
        }
        TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body_node)) {
            extract_go(body_node, source, recv_type, out_elements);
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

    if (type == "call_expression") {
        TSNode func_node = ts_node_child_by_field_name(node, "function", 8);
        string call_name;
        if (!ts_node_is_null(func_node)) {
            const string_view func_type = ts_node_type(func_node);
            if (func_type == "selector_expression") {
                TSNode field_node = ts_node_child_by_field_name(func_node, "field", 5);
                if (!ts_node_is_null(field_node)) {
                    call_name = get_node_text(field_node, source);
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
    }

    const uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        extract_go(ts_node_named_child(node, i), source, current_class, out_elements);
    }
}

}  // namespace amoeba::parser::internal
