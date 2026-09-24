#pragma once

#include "amoeba/parser/code_element.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace amoeba::parser {

using namespace std;
using namespace std::filesystem;

/**
 * @brief Structured result of parsing a source file.
 */
struct ParsedFile {
    path file_path;
    string language;
    bool success{false};
    bool has_syntax_errors{false};
    vector<CodeElement> elements;

    [[nodiscard]] vector<CodeElement> get_elements_by_kind(ElementKind kind) const {
        vector<CodeElement> filtered;
        for (const auto& elem : elements) {
            if (elem.kind == kind) {
                filtered.push_back(elem);
            }
        }
        return filtered;
    }
};

}  // namespace amoeba::parser
