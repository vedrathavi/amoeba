#include "amoeba/parser/source_parser.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tree_sitter/api.h>

extern "C" {
const TSLanguage* tree_sitter_c();
const TSLanguage* tree_sitter_cpp();
}

namespace amoeba::parser {

using namespace std;
using namespace std::filesystem;

namespace {

string get_node_text(TSNode node, string_view source) {
    const uint32_t start = ts_node_start_byte(node);
    const uint32_t end = ts_node_end_byte(node);
    if (start >= source.size() || end > source.size() || start >= end) {
        return "";
    }
    return string(source.substr(start, end - start));
}

SourceRange get_node_range(TSNode node) {
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

void extract_elements_recursive(TSNode node, string_view source, const string& current_class,
                                vector<CodeElement>& out_elements) {
    if (ts_node_is_null(node)) {
        return;
    }

    const string_view type = ts_node_type(node);

    if (type == "class_specifier" || type == "struct_specifier") {
        const bool is_class = (type == "class_specifier");
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        string type_name = !ts_node_is_null(name_node) ? get_node_text(name_node, source) : "";

        if (!type_name.empty()) {
            out_elements.push_back(CodeElement{
                .kind = is_class ? ElementKind::Class : ElementKind::Struct,
                .name = type_name,
                .location = get_node_range(node),
                .parent_context = current_class,
            });
        }

        TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body_node)) {
            const uint32_t child_count = ts_node_named_child_count(body_node);
            for (uint32_t i = 0; i < child_count; ++i) {
                extract_elements_recursive(ts_node_named_child(body_node, i), source, type_name,
                                           out_elements);
            }
        }
        return;
    }

    if (type == "function_definition") {
        TSNode declarator = ts_node_child_by_field_name(node, "declarator", 10);
        string func_name =
            !ts_node_is_null(declarator) ? extract_function_name(declarator, source) : "";

        if (!func_name.empty()) {
            const bool is_method = !current_class.empty() || func_name.find("::") != string::npos;
            out_elements.push_back(CodeElement{
                .kind = is_method ? ElementKind::Method : ElementKind::Function,
                .name = func_name,
                .location = get_node_range(node),
                .parent_context = current_class,
            });
        }

        TSNode body = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body)) {
            extract_elements_recursive(body, source, current_class, out_elements);
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
                if (!method_name.empty()) {
                    out_elements.push_back(CodeElement{
                        .kind =
                            !current_class.empty() ? ElementKind::Method : ElementKind::Function,
                        .name = method_name,
                        .location = get_node_range(node),
                        .parent_context = current_class,
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
            });
        }

        TSNode args = ts_node_child_by_field_name(node, "arguments", 9);
        if (!ts_node_is_null(args)) {
            extract_elements_recursive(args, source, current_class, out_elements);
        }
        return;
    }

    const uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        extract_elements_recursive(ts_node_named_child(node, i), source, current_class,
                                   out_elements);
    }
}

}  // namespace

struct SourceParser::Impl {
    TSParser* parser{nullptr};

    Impl() : parser(ts_parser_new()) {}

    ~Impl() {
        if (parser != nullptr) {
            ts_parser_delete(parser);
            parser = nullptr;
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&& other) noexcept : parser(other.parser) { other.parser = nullptr; }
    Impl& operator=(Impl&& other) noexcept {
        if (this != &other) {
            if (parser != nullptr) {
                ts_parser_delete(parser);
            }
            parser = other.parser;
            other.parser = nullptr;
        }
        return *this;
    }
};

SourceParser::SourceParser() : impl_(make_unique<Impl>()) {}

SourceParser::~SourceParser() = default;

SourceParser::SourceParser(SourceParser&&) noexcept = default;

SourceParser& SourceParser::operator=(SourceParser&&) noexcept = default;

string_view SourceParser::detect_language(const path& file_path) {
    const string ext = file_path.extension().string();
    if (ext == ".c" || ext == ".h") {
        return "C";
    }
    if (ext == ".cpp" || ext == ".hpp" || ext == ".cc" || ext == ".hh" || ext == ".cxx" ||
        ext == ".hxx") {
        return "C++";
    }
    return "Unknown";
}

ParsedFile SourceParser::parse_source(string_view source, string_view language,
                                      const path& file_path) const {
    ParsedFile result;
    result.file_path = file_path;
    result.language = string(language);

    if (!impl_ || impl_->parser == nullptr) {
        result.success = false;
        return result;
    }

    const TSLanguage* ts_lang = nullptr;
    if (language == "C") {
        ts_lang = tree_sitter_c();
    } else if (language == "C++") {
        ts_lang = tree_sitter_cpp();
    } else {
        // Unsupported language in current phase
        result.success = false;
        return result;
    }

    if (ts_lang == nullptr || !ts_parser_set_language(impl_->parser, ts_lang)) {
        result.success = false;
        return result;
    }

    TSTree* tree = ts_parser_parse_string(impl_->parser, nullptr, source.data(),
                                          static_cast<uint32_t>(source.size()));

    if (tree == nullptr) {
        result.success = false;
        return result;
    }

    TSNode root_node = ts_tree_root_node(tree);
    result.success = true;
    result.has_syntax_errors = ts_node_has_error(root_node);

    extract_elements_recursive(root_node, source, "", result.elements);

    ts_tree_delete(tree);
    return result;
}

ParsedFile SourceParser::parse_file(const path& file_path) const {
    error_code ec;
    if (!exists(file_path, ec)) {
        throw invalid_argument("File does not exist: " + file_path.string());
    }
    if (!is_regular_file(file_path, ec)) {
        throw invalid_argument("Path is not a regular file: " + file_path.string());
    }

    ifstream file_stream(file_path, ios::binary);
    if (!file_stream.is_open()) {
        throw invalid_argument("Unable to open file for reading: " + file_path.string());
    }

    stringstream buffer;
    buffer << file_stream.rdbuf();
    const string content = buffer.str();

    const string_view language = detect_language(file_path);
    return parse_source(content, language, file_path);
}

}  // namespace amoeba::parser
