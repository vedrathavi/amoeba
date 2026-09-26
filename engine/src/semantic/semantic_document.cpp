#include "amoeba/semantic/semantic_document.hpp"

#include <sstream>
#include <unordered_set>

namespace amoeba::semantic {

std::string SemanticTextFormatter::format(std::string_view language,
                                          const std::filesystem::path& file_path,
                                          const parser::CodeElement& element) {
    return format(language, file_path, element, "", SemanticRepresentationMode::MetadataOnly);
}

std::string SemanticTextFormatter::format(std::string_view language,
                                          const std::filesystem::path& file_path,
                                          const parser::CodeElement& element,
                                          std::string_view source_snippet,
                                          SemanticRepresentationMode mode) {
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

    if (mode == SemanticRepresentationMode::MetadataWithSnippet && !source_snippet.empty()) {
        // Strip leading/trailing whitespace
        while (!source_snippet.empty() &&
               (source_snippet.front() == ' ' || source_snippet.front() == '\t' ||
                source_snippet.front() == '\r' || source_snippet.front() == '\n')) {
            source_snippet.remove_prefix(1);
        }
        while (!source_snippet.empty() &&
               (source_snippet.back() == ' ' || source_snippet.back() == '\t' ||
                source_snippet.back() == '\r' || source_snippet.back() == '\n')) {
            source_snippet.remove_suffix(1);
        }
        if (!source_snippet.empty()) {
            oss << "Code: " << source_snippet << "\n";
        }
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
                                                        const parser::CodeElement& element,
                                                        std::string_view source_snippet,
                                                        SemanticRepresentationMode mode) {
    return SemanticDocument{
        .element_id = element_id,
        .file_path = file_path,
        .language = std::string(language),
        .kind = element.kind,
        .name = element.name,
        .parent_context = element.parent_context,
        .detail = element.detail,
        .text_representation = format(language, file_path, element, source_snippet, mode),
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 6.4 — RetrievalUnit-level semantic representation
// ─────────────────────────────────────────────────────────────────────────────

std::string SemanticTextFormatter::format_unit(const retrieval::RetrievalUnit& unit) {
    std::ostringstream oss;
    const auto& el = unit.primary_element;

    if (!unit.language.empty()) {
        oss << "Language: " << unit.language << "\n";
    }
    if (!unit.file_path.empty()) {
        oss << "File: " << unit.file_path.generic_string() << "\n";
    }

    const std::string_view kind_str = parser::to_string(el.kind);
    if (kind_str != "Unknown") {
        oss << "Kind: " << kind_str << "\n";
    }
    if (!el.parent_context.empty()) {
        oss << "Context: " << el.parent_context << "\n";
    }
    if (!el.name.empty()) {
        oss << "Name: " << el.name << "\n";
    }
    if (!el.detail.empty()) {
        oss << "Detail: " << el.detail << "\n";
    }

    // Selective supporting evidence summary.
    // Only the most structurally informative evidence kinds are included.
    // Each category is capped to avoid drowning the primary signal.
    // Duplicate names are suppressed.
    static constexpr std::size_t kMaxCallees = 5;
    static constexpr std::size_t kMaxComponents = 4;
    static constexpr std::size_t kMaxAttributes = 4;

    std::vector<std::string_view> callees;
    std::vector<std::string_view> jsx_components;
    std::vector<std::string_view> attributes;
    std::unordered_set<std::string> seen;

    for (const auto& ev : unit.supporting_elements) {
        if (ev.name.empty() || seen.count(ev.name)) {
            continue;
        }
        switch (ev.kind) {
        case parser::ElementKind::Call:
            if (callees.size() < kMaxCallees) {
                callees.push_back(ev.name);
                seen.insert(ev.name);
            }
            break;
        case parser::ElementKind::JSXComponent:
            if (jsx_components.size() < kMaxComponents) {
                jsx_components.push_back(ev.name);
                seen.insert(ev.name);
            }
            break;
        case parser::ElementKind::Attribute:
            if (attributes.size() < kMaxAttributes) {
                attributes.push_back(ev.name);
                seen.insert(ev.name);
            }
            break;
        default:
            break;
        }
    }

    if (!callees.empty()) {
        oss << "Calls:";
        for (const auto& c : callees) {
            oss << " " << c;
        }
        oss << "\n";
    }
    if (!jsx_components.empty()) {
        oss << "Renders:";
        for (const auto& j : jsx_components) {
            oss << " " << j;
        }
        oss << "\n";
    }
    if (!attributes.empty()) {
        oss << "Attributes:";
        for (const auto& a : attributes) {
            oss << " " << a;
        }
        oss << "\n";
    }

    std::string text = oss.str();
    if (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    return text;
}

SemanticDocument SemanticTextFormatter::create_unit_document(const retrieval::RetrievalUnit& unit) {
    return SemanticDocument{
        .element_id = unit.primary_element_id,
        .file_path = unit.file_path,
        .language = unit.language,
        .kind = unit.primary_element.kind,
        .name = unit.primary_element.name,
        .parent_context = unit.primary_element.parent_context,
        .detail = unit.primary_element.detail,
        .text_representation = format_unit(unit),
    };
}

}  // namespace amoeba::semantic
