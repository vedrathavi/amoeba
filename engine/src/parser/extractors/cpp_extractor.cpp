#include "extractors.hpp"
#include "parser/tree_sitter_util.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace amoeba::parser::internal {

using namespace std;

namespace {

string extract_function_name(TSNode declarator_node, string_view source) {
    TSNode current = declarator_node;

    while (!ts_node_is_null(current)) {
        const string_view type = ts_node_type(current);

        if (type == "identifier" || type == "field_identifier" || type == "qualified_identifier" ||
            type == "destructor_name" || type == "operator_name") {
            return get_node_text(current, source);
        }

        if (type == "function_declarator" || type == "parenthesized_declarator" ||
            type == "pointer_declarator" || type == "reference_declarator") {
            TSNode inner = ts_node_child_by_field_name(current, "declarator", 10);
            if (!ts_node_is_null(inner)) {
                current = inner;
                continue;
            }
        }

        const uint32_t child_count = ts_node_named_child_count(current);
        bool found = false;
        for (uint32_t i = 0; i < child_count; ++i) {
            TSNode child = ts_node_named_child(current, i);
            const string_view child_type = ts_node_type(child);
            if (child_type == "identifier" || child_type == "field_identifier" ||
                child_type == "qualified_identifier" || child_type == "function_declarator") {
                current = child;
                found = true;
                break;
            }
        }
        if (!found) {
            break;
        }
    }

    return get_node_text(declarator_node, source);
}

string extract_cpp_parameters(TSNode declarator_node, string_view source) {
    TSNode current = declarator_node;
    while (!ts_node_is_null(current)) {
        TSNode params_node = ts_node_child_by_field_name(current, "parameters", 10);
        if (!ts_node_is_null(params_node)) {
            return get_node_text(params_node, source);
        }
        const uint32_t count = ts_node_named_child_count(current);
        bool found = false;
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_named_child(current, i);
            const string_view c_type = ts_node_type(child);
            if (c_type == "parameter_list") {
                return get_node_text(child, source);
            }
            if (c_type == "function_declarator" || c_type == "parenthesized_declarator" ||
                c_type == "pointer_declarator" || c_type == "reference_declarator") {
                current = child;
                found = true;
                break;
            }
        }
        if (!found) break;
    }
    return "";
}

}  // namespace

void extract_cpp(TSNode node, string_view source, const string& current_class,
                 vector<CodeElement>& out_elements) {
    if (ts_node_is_null(node)) {
        return;
    }

    const string_view type = ts_node_type(node);

    if (type == "namespace_definition") {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        string ns_name = !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";
        string ctx = current_class.empty()
                         ? ns_name
                         : (!ns_name.empty() ? current_class + "::" + ns_name : current_class);
        TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body_node)) {
            const uint32_t child_count = ts_node_named_child_count(body_node);
            for (uint32_t i = 0; i < child_count; ++i) {
                extract_cpp(ts_node_named_child(body_node, i), source, ctx, out_elements);
            }
        }
        return;
    }

    if (type == "class_specifier" || type == "struct_specifier") {
        const bool is_class = (type == "class_specifier");
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        string type_name = !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";

        string inheritance_detail;
        const uint32_t named_count = ts_node_named_child_count(node);
        vector<string> bases;
        for (uint32_t i = 0; i < named_count; ++i) {
            TSNode child = ts_node_named_child(node, i);
            string_view c_type = ts_node_type(child);
            if (c_type == "base_class_clause") {
                const uint32_t base_count = ts_node_named_child_count(child);
                for (uint32_t j = 0; j < base_count; ++j) {
                    TSNode base_spec = ts_node_named_child(child, j);
                    string_view b_type = ts_node_type(base_spec);
                    if (b_type == "access_specifier" || b_type == "virtual") {
                        continue;
                    }
                    if (b_type == "base_class_specifier") {
                        const uint32_t spec_count = ts_node_named_child_count(base_spec);
                        for (uint32_t k = 0; k < spec_count; ++k) {
                            TSNode type_node = ts_node_named_child(base_spec, k);
                            string_view t_type = ts_node_type(type_node);
                            if (t_type != "access_specifier" && t_type != "virtual") {
                                bases.push_back(get_node_text(type_node, source));
                                break;
                            }
                        }
                    } else if (b_type == "type_identifier" || b_type == "qualified_identifier" ||
                               b_type == "template_type" || b_type == "type_descriptor") {
                        bases.push_back(get_node_text(base_spec, source));
                    }
                }
            }
        }
        if (!bases.empty()) {
            inheritance_detail = "extends: ";
            for (size_t i = 0; i < bases.size(); ++i) {
                if (i > 0)
                    inheritance_detail += ", ";
                inheritance_detail += bases[i];
            }
        }

        string doc = extract_preceding_doc(node, source);
        string sig = (is_class ? "class " : "struct ") + type_name;

        if (!type_name.empty()) {
            out_elements.push_back(CodeElement{
                .kind = is_class ? ElementKind::Class : ElementKind::Struct,
                .name = type_name,
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
                extract_cpp(ts_node_named_child(body_node, i), source, type_name, out_elements);
            }
        }
        return;
    }

    if (type == "function_definition") {
        TSNode declarator = ts_node_child_by_field_name(node, "declarator", 10);
        string func_name =
            !ts_node_is_null(declarator) ? extract_function_name(declarator, source) : "";

        TSNode type_node = ts_node_child_by_field_name(node, "type", 4);
        string ret_type = !ts_node_is_null(type_node) ? get_node_text(type_node, source) : "";

        string params_text = !ts_node_is_null(declarator) ? extract_cpp_parameters(declarator, source) : "";
        string doc = extract_preceding_doc(node, source);

        string sig;
        if (!func_name.empty()) {
            if (!ret_type.empty()) {
                sig = ret_type + " ";
            }
            sig += func_name + (params_text.empty() ? "()" : params_text);

            const bool is_method = !current_class.empty() || func_name.find("::") != string::npos;
            out_elements.push_back(CodeElement{
                .kind = is_method ? ElementKind::Method : ElementKind::Function,
                .name = func_name,
                .location = get_node_range(node),
                .parent_context = current_class,
                .detail = "",
                .signature = sig,
                .return_type = ret_type,
                .documentation = doc,
            });
        }

        TSNode body = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body)) {
            extract_cpp(body, source, func_name.empty() ? current_class : func_name, out_elements);
        }
        return;
    }

    if (type == "field_declaration" || type == "declaration") {
        TSNode declarator = ts_node_child_by_field_name(node, "declarator", 10);
        if (!ts_node_is_null(declarator)) {
            const string_view decl_type = ts_node_type(declarator);
            if (decl_type == "function_declarator" ||
                !ts_node_is_null(ts_node_child_by_field_name(declarator, "parameters", 10))) {
                string method_name = extract_function_name(declarator, source);
                TSNode type_node = ts_node_child_by_field_name(node, "type", 4);
                string ret_type = !ts_node_is_null(type_node) ? get_node_text(type_node, source) : "";
                string params_text = extract_cpp_parameters(declarator, source);
                string doc = extract_preceding_doc(node, source);

                string sig;
                if (!ret_type.empty()) {
                    sig = ret_type + " ";
                }
                sig += method_name + (params_text.empty() ? "()" : params_text);

                if (!method_name.empty()) {
                    out_elements.push_back(CodeElement{
                        .kind =
                            !current_class.empty() ? ElementKind::Method : ElementKind::Function,
                        .name = method_name,
                        .location = get_node_range(node),
                        .parent_context = current_class,
                        .detail = "",
                        .signature = sig,
                        .return_type = ret_type,
                        .documentation = doc,
                    });
                }
            }
        }
    }

    if (type == "preproc_include") {
        TSNode path_node = ts_node_child_by_field_name(node, "path", 4);
        string include_path = !ts_node_is_null(path_node) ? get_node_text(path_node, source) : "";

        if (include_path.empty()) {
            const uint32_t count = ts_node_named_child_count(node);
            for (uint32_t i = 0; i < count; ++i) {
                TSNode child = ts_node_named_child(node, i);
                const string_view child_type = ts_node_type(child);
                if (child_type == "string_literal" || child_type == "system_lib_string") {
                    include_path = get_node_text(child, source);
                    break;
                }
            }
        }

        if (!include_path.empty()) {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::Include,
                .name = include_path,
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
            if (func_type == "field_expression") {
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

        TSNode args = ts_node_child_by_field_name(node, "arguments", 9);
        if (!ts_node_is_null(args)) {
            extract_cpp(args, source, current_class, out_elements);
        }
        return;
    }

    const uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        extract_cpp(ts_node_named_child(node, i), source, current_class, out_elements);
    }
}

}  // namespace amoeba::parser::internal
