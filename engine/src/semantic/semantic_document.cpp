#include "amoeba/semantic/semantic_document.hpp"

#include <sstream>

namespace amoeba::semantic {

std::string SemanticTextFormatter::format(std::string_view language,
                                          const std::filesystem::path& file_path,
                                          const parser::CodeElement& element) {
    std::ostringstream oss;
    if (!language.empty()) {
        oss << "Language: " << language << "\n";
    }
    if (!file_path.empty()) {
        oss << "File: " << file_path.generic_string() << "\n";
    }

    std::string_view kind_str = parser::to_string(element.kind);
    if (kind_str != "Unknown") {
        oss << "Kind: " << kind_str << "\n";
    }

    if (!element.parent_context.empty()) {
        oss << "Context: " << element.parent_context << "\n";
    }

    if (!element.name.empty()) {
        oss << "Name: " << element.name << "\n";
    }

    if (!element.detail.empty()) {
        oss << "Detail: " << element.detail << "\n";
    }

    std::string text = oss.str();
    if (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    return text;
}

SemanticDocument SemanticTextFormatter::create_document(ElementId element_id,
                                                        std::string_view language,
                                                        const std::filesystem::path& file_path,
                                                        const parser::CodeElement& element) {
    return SemanticDocument{
        .element_id = element_id,
        .file_path = file_path,
        .language = std::string(language),
        .kind = element.kind,
        .name = element.name,
        .parent_context = element.parent_context,
        .detail = element.detail,
        .text_representation = format(language, file_path, element),
    };
}

}  // namespace amoeba::semantic
