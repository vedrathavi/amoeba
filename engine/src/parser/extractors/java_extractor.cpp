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

        string inheritance_detail;
        const uint32_t named_count = ts_node_named_child_count(node);
        vector<string> extends_list;
        vector<string> implements_list;

        for (uint32_t i = 0; i < named_count; ++i) {
            TSNode child = ts_node_named_child(node, i);
            string_view c_type = ts_node_type(child);
            if (c_type == "superclass" || c_type == "extends_interfaces") {
                const uint32_t inner_count = ts_node_named_child_count(child);
                for (uint32_t j = 0; j < inner_count; ++j) {
                    TSNode type_node = ts_node_named_child(child, j);
                    string_view t_type = ts_node_type(type_node);
                    if (t_type == "type_identifier" || t_type == "generic_type") {
                        extends_list.push_back(get_node_text(type_node, source));
                    }
                }
            } else if (c_type == "super_interfaces" || c_type == "interfaces") {
                const uint32_t inner_count = ts_node_named_child_count(child);
                for (uint32_t j = 0; j < inner_count; ++j) {
                    TSNode iface_node = ts_node_named_child(child, j);
                    string_view t_type = ts_node_type(iface_node);
                    if (t_type == "type_identifier" || t_type == "generic_type") {
                        implements_list.push_back(get_node_text(iface_node, source));
                    } else if (t_type == "type_list" || t_type == "interface_type_list") {
                        const uint32_t list_count = ts_node_named_child_count(iface_node);
                        for (uint32_t k = 0; k < list_count; ++k) {
                            TSNode item = ts_node_named_child(iface_node, k);
                            implements_list.push_back(get_node_text(item, source));
                        }
                    } else {
                        // Fallback: collect all named children or text
                        const uint32_t list_count = ts_node_named_child_count(iface_node);
                        if (list_count == 0) {
                            implements_list.push_back(get_node_text(iface_node, source));
                        } else {
                            for (uint32_t k = 0; k < list_count; ++k) {
                                implements_list.push_back(
                                    get_node_text(ts_node_named_child(iface_node, k), source));
                            }
                        }
                    }
                }
            }
        }

        if (!extends_list.empty()) {
            inheritance_detail += "extends: ";
            for (size_t i = 0; i < extends_list.size(); ++i) {
                if (i > 0)
                    inheritance_detail += ", ";
                inheritance_detail += extends_list[i];
            }
        }
        if (!implements_list.empty()) {
            if (!inheritance_detail.empty())
                inheritance_detail += "; ";
            inheritance_detail += "implements: ";
            for (size_t i = 0; i < implements_list.size(); ++i) {
                if (i > 0)
                    inheritance_detail += ", ";
                inheritance_detail += implements_list[i];
            }
        }

        string doc = extract_preceding_doc(node, source);
        string sig = (is_interface ? "interface " : "class ") + class_name;

        if (!class_name.empty()) {
            out_elements.push_back(CodeElement{
                .kind = is_interface ? ElementKind::Interface : ElementKind::Class,
                .name = class_name,
                .location = get_node_range(node),
                .parent_context = current_class,
                .detail = inheritance_detail,
                .signature = sig,
                .return_type = "",
                .documentation = doc,
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

        TSNode type_node = ts_node_child_by_field_name(node, "type", 4);
        string ret_type = !ts_node_is_null(type_node) ? get_node_text(type_node, source) : "";

        TSNode params_node = ts_node_child_by_field_name(node, "parameters", 10);
        string params_text = !ts_node_is_null(params_node) ? get_node_text(params_node, source) : "";

        string doc = extract_preceding_doc(node, source);

        string sig;
        if (!method_name.empty()) {
            if (!ret_type.empty()) {
                sig = ret_type + " ";
            }
            sig += method_name + (params_text.empty() ? "()" : params_text);

            out_elements.push_back(CodeElement{
                .kind = ElementKind::Method,
                .name = method_name,
                .location = get_node_range(node),
                .parent_context = current_class,
                .detail = "",
                .signature = sig,
                .return_type = ret_type,
                .documentation = doc,
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
