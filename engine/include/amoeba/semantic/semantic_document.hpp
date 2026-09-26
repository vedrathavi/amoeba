#pragma once

#include "amoeba/parser/code_element.hpp"
#include "amoeba/retrieval/retrieval_unit.hpp"
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

    // -------------------------------------------------------------------------
    // Phase 6.4 — RetrievalUnit-level representation
    // -------------------------------------------------------------------------

    /**
     * @brief Generates enriched semantic text for a primary RetrievalUnit.
     *
     * The representation includes:
     *   - Language, file, kind, context, name, detail of the primary symbol
     *   - A concise summary of key supporting evidence (call targets,
     *     JSX component references, key attributes)
     *
     * Supporting evidence is included selectively, not exhaustively, to
     * preserve embedding quality without burying the primary symbol signal.
     *
     * @param unit A primary RetrievalUnit with supporting evidence attached.
     * @return Structured text suitable for embedding.
     */
    [[nodiscard]] static std::string format_unit(const retrieval::RetrievalUnit& unit);

    /**
     * @brief Creates a SemanticDocument from a RetrievalUnit (Phase 6.4).
     *
     * Uses format_unit() for the text representation.
     * The ElementId is the unit's primary_element_id.
     */
    [[nodiscard]] static SemanticDocument
    create_unit_document(const retrieval::RetrievalUnit& unit);
};

}  // namespace amoeba::semantic
