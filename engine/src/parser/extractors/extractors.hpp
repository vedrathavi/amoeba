#pragma once

#include "amoeba/parser/code_element.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <tree_sitter/api.h>
#include <vector>

extern "C" {
const TSLanguage* tree_sitter_c();
const TSLanguage* tree_sitter_cpp();
const TSLanguage* tree_sitter_python();
const TSLanguage* tree_sitter_java();
const TSLanguage* tree_sitter_go();
const TSLanguage* tree_sitter_rust();
const TSLanguage* tree_sitter_javascript();
const TSLanguage* tree_sitter_typescript();
const TSLanguage* tree_sitter_tsx();
const TSLanguage* tree_sitter_html();
const TSLanguage* tree_sitter_css();
}

namespace amoeba::parser::internal {

using namespace std;
using namespace std::filesystem;

void extract_cpp(TSNode node, string_view source, const string& current_class,
                 vector<CodeElement>& out_elements);

void extract_python(TSNode node, string_view source, const string& current_class,
                    vector<CodeElement>& out_elements);

void extract_java(TSNode node, string_view source, const string& current_class,
                  vector<CodeElement>& out_elements);

void extract_go(TSNode node, string_view source, const string& current_class,
                vector<CodeElement>& out_elements);

void extract_rust(TSNode node, string_view source, const string& current_class,
                  vector<CodeElement>& out_elements);

void extract_js_ts(TSNode node, string_view source, const string& current_class,
                   vector<CodeElement>& out_elements);

void extract_html(TSNode node, string_view source, const string& current_parent,
                  vector<CodeElement>& out_elements);

void extract_css(TSNode node, string_view source, const string& current_rule,
                 vector<CodeElement>& out_elements);

void check_nextjs_conventions(const path& file_path, vector<CodeElement>& out_elements);

}  // namespace amoeba::parser::internal
