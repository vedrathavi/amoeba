#pragma once

#include "amoeba/parser/code_element.hpp"
#include "amoeba/semantic/embedding.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace amoeba::semantic {

/**
 * @brief Strategy variant for semantic code representation formatting.
 */
enum class SemanticRepresentationMode {
    MetadataOnly,        ///< Representation A: Language, File, Kind, Context, Name, Detail
    MetadataWithSnippet  ///< Representation B: Metadata + associated code source snippet
};

/**
 * @brief Document connecting a CodeElement with its semantic representation and embedding.
 */
struct SemanticDocument {
    ElementId element_id{0};
    std::filesystem::path file_path;
    std::string language;
    parser::ElementKind kind{parser::ElementKind::Unknown};
    std::string name;
    std::string parent_context;
    std::string detail;
    std::string text_representation;
};

/**
 * @brief Strategy for converting a CodeElement into structured text suitable for embedding.
 */
class SemanticTextFormatter {
public:
    /**
     * @brief Generates structured representation text for a CodeElement (Representation A).
     */
    [[nodiscard]] static std::string format(std::string_view language,
                                            const std::filesystem::path& file_path,
                                            const parser::CodeElement& element);

    /**
     * @brief Generates structured representation text with optional code snippet (Representation A
     * or B).
     */
    [[nodiscard]] static std::string
    format(std::string_view language, const std::filesystem::path& file_path,
           const parser::CodeElement& element, std::string_view source_snippet,
           SemanticRepresentationMode mode = SemanticRepresentationMode::MetadataOnly);

    /**
     * @brief Creates a SemanticDocument from element details without embedding.
     */
    [[nodiscard]] static SemanticDocument
    create_document(ElementId element_id, std::string_view language,
                    const std::filesystem::path& file_path, const parser::CodeElement& element,
                    std::string_view source_snippet = "",
                    SemanticRepresentationMode mode = SemanticRepresentationMode::MetadataOnly);
};

}  // namespace amoeba::semantic
