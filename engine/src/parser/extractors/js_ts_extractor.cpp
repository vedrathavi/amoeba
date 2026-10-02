#include "extractors.hpp"
#include "parser/tree_sitter_util.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::parser::internal {

using namespace std;

namespace {

bool is_capitalized(string_view name) noexcept {
    return !name.empty() && isupper(static_cast<unsigned char>(name[0]));
}

bool is_hook_name(string_view name) noexcept {
    return name.starts_with("use") && name.size() > 3 &&
           isupper(static_cast<unsigned char>(name[3]));
}

void extract_tailwind_classes(string_view class_attr_value, TSNode node,
                              vector<CodeElement>& out_elements) {
    // Strip quotes if present
    string_view val = class_attr_value;
    if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') ||
                            (val.front() == '\'' && val.back() == '\''))) {
        val = val.substr(1, val.size() - 2);
    }

    size_t start = 0;
    while (start < val.size()) {
        while (start < val.size() && isspace(static_cast<unsigned char>(val[start]))) {
            start++;
        }
        if (start >= val.size()) {
            break;
        }
        size_t end = start;
        while (end < val.size() && !isspace(static_cast<unsigned char>(val[end]))) {
            end++;
        }
        string token(val.substr(start, end - start));
        if (!token.empty()) {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::UtilityClass,
                .name = token,
                .location = get_node_range(node),
                .parent_context = "className",
                .detail = "",
            });
        }
        start = end;
    }
}

}  // namespace

void extract_js_ts(TSNode node, string_view source, const string& current_class,
                   vector<CodeElement>& out_elements) {
    if (ts_node_is_null(node)) {
        return;
    }

    const string_view type = ts_node_type(node);

    if (type == "class_declaration" || type == "class") {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        string class_name = !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";

        string inheritance_detail;
        const uint32_t named_count = ts_node_named_child_count(node);
        vector<string> extends_list;
        vector<string> implements_list;

        for (uint32_t i = 0; i < named_count; ++i) {
            TSNode child = ts_node_named_child(node, i);
            string_view c_type = ts_node_type(child);
            if (c_type == "class_heritage") {
                const uint32_t h_count = ts_node_named_child_count(child);
                for (uint32_t j = 0; j < h_count; ++j) {
                    TSNode clause = ts_node_named_child(child, j);
                    string_view cl_type = ts_node_type(clause);
                    if (cl_type == "extends_clause") {
                        TSNode val_node = ts_node_child_by_field_name(clause, "value", 5);
                        if (!ts_node_is_null(val_node)) {
                            extends_list.push_back(get_node_text(val_node, source));
                        } else {
                            const uint32_t ext_count = ts_node_named_child_count(clause);
                            for (uint32_t k = 0; k < ext_count; ++k) {
                                extends_list.push_back(
                                    get_node_text(ts_node_named_child(clause, k), source));
                            }
                        }
                    } else if (cl_type == "implements_clause") {
                        const uint32_t imp_count = ts_node_named_child_count(clause);
                        for (uint32_t k = 0; k < imp_count; ++k) {
                            TSNode imp_child = ts_node_named_child(clause, k);
                            implements_list.push_back(get_node_text(imp_child, source));
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
        string sig = "class " + class_name;

        if (!class_name.empty()) {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::Class,
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
            const uint32_t count = ts_node_named_child_count(body_node);
            for (uint32_t i = 0; i < count; ++i) {
                extract_js_ts(ts_node_named_child(body_node, i), source, class_name, out_elements);
            }
        }
        return;
    }

    if (type == "interface_declaration") {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        string iface_name = !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";

        string inheritance_detail;
        const uint32_t named_count = ts_node_named_child_count(node);
        vector<string> extends_list;
        for (uint32_t i = 0; i < named_count; ++i) {
            TSNode child = ts_node_named_child(node, i);
            string_view c_type = ts_node_type(child);
            if (c_type == "extends_type_clause" || c_type == "extends_clause" ||
                c_type == "heritage_clause") {
                const uint32_t ext_count = ts_node_named_child_count(child);
                for (uint32_t k = 0; k < ext_count; ++k) {
                    extends_list.push_back(get_node_text(ts_node_named_child(child, k), source));
                }
            }
        }
        if (!extends_list.empty()) {
            inheritance_detail = "extends: ";
            for (size_t i = 0; i < extends_list.size(); ++i) {
                if (i > 0)
                    inheritance_detail += ", ";
                inheritance_detail += extends_list[i];
            }
        }

        string doc = extract_preceding_doc(node, source);
        string sig = "interface " + iface_name;

        if (!iface_name.empty()) {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::Interface,
                .name = iface_name,
                .location = get_node_range(node),
                .parent_context = current_class,
                .detail = inheritance_detail,
                .signature = sig,
                .return_type = "",
                .documentation = doc,
            });
        }
        return;
    }

    if (type == "type_alias_declaration") {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        string type_name = !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";
        string doc = extract_preceding_doc(node, source);
        if (!type_name.empty()) {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::Interface,
                .name = type_name,
                .location = get_node_range(node),
                .parent_context = current_class,
                .detail = "",
                .signature = "type " + type_name,
                .return_type = "",
                .documentation = doc,
            });
        }
        return;
    }

    if (type == "function_declaration" || type == "function") {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        string func_name = !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";

        TSNode params_node = ts_node_child_by_field_name(node, "parameters", 10);
        string params_text = !ts_node_is_null(params_node) ? get_node_text(params_node, source) : "";

        TSNode ret_node = ts_node_child_by_field_name(node, "return_type", 11);
        string ret_text = !ts_node_is_null(ret_node) ? get_node_text(ret_node, source) : "";

        string doc = extract_preceding_doc(node, source);

        if (!func_name.empty()) {
            const bool is_comp = is_capitalized(func_name);
            string sig = func_name + (params_text.empty() ? "()" : params_text);
            if (!ret_text.empty()) {
                sig += ": " + ret_text;
            }

            out_elements.push_back(CodeElement{
                .kind = is_comp ? ElementKind::Component
                                : (!current_class.empty() ? ElementKind::Method
                                                          : ElementKind::Function),
                .name = func_name,
                .location = get_node_range(node),
                .parent_context = current_class,
                .detail = is_comp ? "React Component" : "",
                .signature = sig,
                .return_type = ret_text,
                .documentation = doc,
            });
        }
        TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body_node)) {
            extract_js_ts(body_node, source, func_name.empty() ? current_class : func_name,
                          out_elements);
        }
        return;
    }

    if (type == "method_definition") {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        string method_name = !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";

        TSNode params_node = ts_node_child_by_field_name(node, "parameters", 10);
        string params_text = !ts_node_is_null(params_node) ? get_node_text(params_node, source) : "";

        TSNode ret_node = ts_node_child_by_field_name(node, "return_type", 11);
        string ret_text = !ts_node_is_null(ret_node) ? get_node_text(ret_node, source) : "";

        string doc = extract_preceding_doc(node, source);

        if (!method_name.empty()) {
            string sig = method_name + (params_text.empty() ? "()" : params_text);
            if (!ret_text.empty()) {
                sig += ": " + ret_text;
            }

            out_elements.push_back(CodeElement{
                .kind = ElementKind::Method,
                .name = method_name,
                .location = get_node_range(node),
                .parent_context = current_class,
                .detail = "",
                .signature = sig,
                .return_type = ret_text,
                .documentation = doc,
            });
        }
        TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body_node)) {
            extract_js_ts(body_node, source, current_class, out_elements);
        }
        return;
    }

    if (type == "variable_declarator") {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        TSNode value_node = ts_node_child_by_field_name(node, "value", 5);
        string var_name = !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";

        if (!var_name.empty() && !ts_node_is_null(value_node)) {
            const string_view val_type = ts_node_type(value_node);
            if (val_type == "arrow_function" || val_type == "function_expression" ||
                val_type == "function") {
                const bool is_comp = is_capitalized(var_name);

                TSNode params_node = ts_node_child_by_field_name(value_node, "parameters", 10);
                string params_text = !ts_node_is_null(params_node) ? get_node_text(params_node, source) : "";

                TSNode ret_node = ts_node_child_by_field_name(value_node, "return_type", 11);
                string ret_text = !ts_node_is_null(ret_node) ? get_node_text(ret_node, source) : "";

                string doc = extract_preceding_doc(node, source);
                string sig = var_name + (params_text.empty() ? "()" : params_text);
                if (!ret_text.empty()) {
                    sig += ": " + ret_text;
                }

                out_elements.push_back(CodeElement{
                    .kind = is_comp ? ElementKind::Component : ElementKind::Function,
                    .name = var_name,
                    .location = get_node_range(node),
                    .parent_context = current_class,
                    .detail = is_comp ? "Arrow Component" : "",
                    .signature = sig,
                    .return_type = ret_text,
                    .documentation = doc,
                });
            }
        }
    }

    if (type == "import_statement" || type == "import_declaration") {
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
            if (func_type == "member_expression") {
                TSNode prop_node = ts_node_child_by_field_name(func_node, "property", 8);
                if (!ts_node_is_null(prop_node)) {
                    call_name = get_node_text(prop_node, source);
                }
            }
            if (call_name.empty()) {
                call_name = get_node_text(func_node, source);
            }
        }

        if (!call_name.empty()) {
            if (call_name == "require") {
                out_elements.push_back(CodeElement{
                    .kind = ElementKind::Include,
                    .name = get_node_text(node, source),
                    .location = get_node_range(node),
                    .parent_context = current_class,
                    .detail = "",
                });
            } else if (is_hook_name(call_name)) {
                out_elements.push_back(CodeElement{
                    .kind = ElementKind::Hook,
                    .name = call_name,
                    .location = get_node_range(node),
                    .parent_context = current_class,
                    .detail = "React Hook",
                });
            } else {
                out_elements.push_back(CodeElement{
                    .kind = ElementKind::Call,
                    .name = call_name,
                    .location = get_node_range(node),
                    .parent_context = current_class,
                    .detail = "",
                });
            }
        }
    }

    // JSX Elements: <Button>, <div className="...">
    if (type == "jsx_self_closing_element" || type == "jsx_opening_element") {
        TSNode tag_node = ts_node_child_by_field_name(node, "name", 4);
        string tag_name = !ts_node_is_null(tag_node) ? get_node_text(tag_node, source) : "";
        if (!tag_name.empty()) {
            const bool is_comp = is_capitalized(tag_name);
            out_elements.push_back(CodeElement{
                .kind = is_comp ? ElementKind::JSXComponent : ElementKind::JSXElement,
                .name = tag_name,
                .location = get_node_range(node),
                .parent_context = current_class,
                .detail = "",
            });
        }
    }

    // JSX Attributes: className="...", onClick={...}
    if (type == "jsx_attribute") {
        TSNode prop_name_node = ts_node_child_by_field_name(node, "name", 4);
        TSNode prop_val_node = ts_node_child_by_field_name(node, "value", 5);
        string attr_name =
            !ts_node_is_null(prop_name_node) ? get_node_text(prop_name_node, source) : "";
        string attr_val =
            !ts_node_is_null(prop_val_node) ? get_node_text(prop_val_node, source) : "";

        if (attr_name.empty()) {
            const uint32_t count = ts_node_named_child_count(node);
            for (uint32_t i = 0; i < count; ++i) {
                TSNode child = ts_node_named_child(node, i);
                const string_view ct = ts_node_type(child);
                if (ct == "property_identifier" || ct == "jsx_identifier" || ct == "identifier") {
                    attr_name = get_node_text(child, source);
                } else if (ct == "string" || ct == "string_fragment" || ct == "jsx_expression") {
                    attr_val = get_node_text(child, source);
                }
            }
        }

        if (!attr_name.empty()) {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::Attribute,
                .name = attr_name,
                .location = get_node_range(node),
                .parent_context = current_class,
                .detail = attr_val,
            });

            if (attr_name == "className" || attr_name == "class") {
                extract_tailwind_classes(attr_val, node, out_elements);
            }
        }
    }

    const uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        extract_js_ts(ts_node_named_child(node, i), source, current_class, out_elements);
    }
}

}  // namespace amoeba::parser::internal
