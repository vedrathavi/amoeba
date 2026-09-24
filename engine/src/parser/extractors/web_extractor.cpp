#include "extractors.hpp"
#include "parser/tree_sitter_util.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::parser::internal {

using namespace std;

namespace {

void extract_tailwind_classes(string_view class_attr_value, TSNode node,
                              vector<CodeElement>& out_elements) {
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

void extract_html(TSNode node, string_view source, const string& current_parent,
                  vector<CodeElement>& out_elements) {
    if (ts_node_is_null(node)) {
        return;
    }

    const string_view type = ts_node_type(node);

    if (type == "element" || type == "self_closing_tag" || type == "start_tag") {
        string tag_name;
        const uint32_t count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_named_child(node, i);
            const string_view child_type = ts_node_type(child);
            if (child_type == "tag_name") {
                tag_name = get_node_text(child, source);
                break;
            }
            if (child_type == "start_tag") {
                TSNode inner_tag = ts_node_child_by_field_name(child, "name", 4);
                if (!ts_node_is_null(inner_tag)) {
                    tag_name = get_node_text(inner_tag, source);
                } else {
                    const uint32_t start_count = ts_node_named_child_count(child);
                    for (uint32_t j = 0; j < start_count; ++j) {
                        TSNode sc = ts_node_named_child(child, j);
                        if (string_view(ts_node_type(sc)) == "tag_name") {
                            tag_name = get_node_text(sc, source);
                            break;
                        }
                    }
                }
                break;
            }
        }

        if (!tag_name.empty() && type != "start_tag") {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::JSXElement,
                .name = tag_name,
                .location = get_node_range(node),
                .parent_context = current_parent,
                .detail = "",
            });
        }
    }

    if (type == "attribute") {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        TSNode val_node = ts_node_child_by_field_name(node, "value", 5);
        string attr_name = !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";
        string attr_val = !ts_node_is_null(val_node) ? get_node_text(val_node, source) : "";

        if (attr_name.empty()) {
            const uint32_t count = ts_node_named_child_count(node);
            for (uint32_t i = 0; i < count; ++i) {
                TSNode child = ts_node_named_child(node, i);
                const string_view child_type = ts_node_type(child);
                if (child_type == "attribute_name") {
                    attr_name = get_node_text(child, source);
                } else if (child_type == "attribute_value" ||
                           child_type == "quoted_attribute_value") {
                    attr_val = get_node_text(child, source);
                }
            }
        }

        if (!attr_name.empty()) {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::Attribute,
                .name = attr_name,
                .location = get_node_range(node),
                .parent_context = current_parent,
                .detail = attr_val,
            });

            if (attr_name == "class") {
                extract_tailwind_classes(attr_val, node, out_elements);
            }
        }
    }

    const uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        extract_html(ts_node_named_child(node, i), source, current_parent, out_elements);
    }
}

void extract_css(TSNode node, string_view source, const string& current_rule,
                 vector<CodeElement>& out_elements) {
    if (ts_node_is_null(node)) {
        return;
    }

    const string_view type = ts_node_type(node);

    if (type == "rule_set") {
        TSNode sel_node = ts_node_child_by_field_name(node, "selectors", 9);
        string selector_text;
        if (!ts_node_is_null(sel_node)) {
            selector_text = get_node_text(sel_node, source);
        } else {
            const uint32_t count = ts_node_named_child_count(node);
            for (uint32_t i = 0; i < count; ++i) {
                TSNode child = ts_node_named_child(node, i);
                const string_view ct = ts_node_type(child);
                if (ct == "selectors" || ct == "class_selector" || ct == "id_selector" ||
                    ct == "tag_name") {
                    selector_text = get_node_text(child, source);
                    break;
                }
            }
        }

        if (!selector_text.empty()) {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::Selector,
                .name = selector_text,
                .location = get_node_range(node),
                .parent_context = current_rule,
                .detail = "",
            });
        }

        TSNode block_node = ts_node_child_by_field_name(node, "body", 4);
        if (ts_node_is_null(block_node)) {
            block_node = ts_node_child_by_field_name(node, "block", 5);
        }
        if (!ts_node_is_null(block_node)) {
            const uint32_t count = ts_node_named_child_count(block_node);
            for (uint32_t i = 0; i < count; ++i) {
                extract_css(ts_node_named_child(block_node, i), source, selector_text,
                            out_elements);
            }
        } else {
            const uint32_t count = ts_node_named_child_count(node);
            for (uint32_t i = 0; i < count; ++i) {
                TSNode child = ts_node_named_child(node, i);
                if (string_view(ts_node_type(child)) == "block") {
                    extract_css(child, source, selector_text, out_elements);
                }
            }
        }
        return;
    }

    if (type == "declaration") {
        TSNode prop_node = ts_node_child_by_field_name(node, "name", 4);
        TSNode val_node = ts_node_child_by_field_name(node, "value", 5);
        string prop_name = !ts_node_is_null(prop_node) ? get_node_text(prop_node, source) : "";
        string prop_val = !ts_node_is_null(val_node) ? get_node_text(val_node, source) : "";

        if (prop_name.empty()) {
            const uint32_t count = ts_node_named_child_count(node);
            for (uint32_t i = 0; i < count; ++i) {
                TSNode child = ts_node_named_child(node, i);
                const string_view child_type = ts_node_type(child);
                if (child_type == "property_name") {
                    prop_name = get_node_text(child, source);
                } else if (child_type == "property_values" || child_type == "value") {
                    prop_val = get_node_text(child, source);
                }
            }
        }

        if (!prop_name.empty()) {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::Property,
                .name = prop_name,
                .location = get_node_range(node),
                .parent_context = current_rule,
                .detail = prop_val,
            });
        }
        return;
    }

    if (type == "import_statement" || type == "at_rule") {
        string at_text = get_node_text(node, source);
        if (!at_text.empty()) {
            out_elements.push_back(CodeElement{
                .kind = ElementKind::Include,
                .name = at_text,
                .location = get_node_range(node),
                .parent_context = current_rule,
                .detail = "",
            });
        }
        return;
    }

    const uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        extract_css(ts_node_named_child(node, i), source, current_rule, out_elements);
    }
}

}  // namespace amoeba::parser::internal
