#include "amoeba/parser/source_parser.hpp"

#include "parser/extractors/extractors.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tree_sitter/api.h>

namespace amoeba::parser {

using namespace std;
using namespace std::filesystem;
using namespace amoeba::parser::internal;

namespace {

using GrammarGetter = const TSLanguage* (*)();
using ExtractorFn = void (*)(TSNode node, string_view source, const string& parent,
                             vector<CodeElement>& out_elements);

struct LanguageRegistryEntry {
    string_view name;
    GrammarGetter get_grammar;
    ExtractorFn extractor;
};

constexpr array<LanguageRegistryEntry, 12> LANGUAGE_REGISTRY = {{
    {"C", tree_sitter_c, extract_cpp},
    {"C++", tree_sitter_cpp, extract_cpp},
    {"Python", tree_sitter_python, extract_python},
    {"Java", tree_sitter_java, extract_java},
    {"Go", tree_sitter_go, extract_go},
    {"Rust", tree_sitter_rust, extract_rust},
    {"JavaScript", tree_sitter_javascript, extract_js_ts},
    {"JSX", tree_sitter_javascript, extract_js_ts},
    {"TypeScript", tree_sitter_typescript, extract_js_ts},
    {"TSX", tree_sitter_tsx, extract_js_ts},
    {"HTML", tree_sitter_html, extract_html},
    {"CSS", tree_sitter_css, extract_css},
}};

const LanguageRegistryEntry* find_language_entry(string_view language) noexcept {
    for (const auto& entry : LANGUAGE_REGISTRY) {
        if (entry.name == language) {
            return &entry;
        }
    }
    return nullptr;
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
    if (ext == ".py") {
        return "Python";
    }
    if (ext == ".java") {
        return "Java";
    }
    if (ext == ".go") {
        return "Go";
    }
    if (ext == ".rs") {
        return "Rust";
    }
    if (ext == ".js") {
        return "JavaScript";
    }
    if (ext == ".jsx") {
        return "JSX";
    }
    if (ext == ".ts") {
        return "TypeScript";
    }
    if (ext == ".tsx") {
        return "TSX";
    }
    if (ext == ".html" || ext == ".htm") {
        return "HTML";
    }
    if (ext == ".css") {
        return "CSS";
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

    const auto* entry = find_language_entry(language);
    if (entry == nullptr || entry->get_grammar == nullptr) {
        result.success = false;
        return result;
    }

    const TSLanguage* ts_lang = entry->get_grammar();
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

    if (entry->extractor != nullptr) {
        entry->extractor(root_node, source, "", result.elements);
    }

    // Check for file-level Next.js route conventions if a path is given
    check_nextjs_conventions(file_path, result.elements);

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
