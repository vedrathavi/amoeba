#pragma once

// Phase 6.3 — Retrieval Unit Model
//
// This header defines the search-oriented retrieval representation for Amoeba.
//
// ┌─────────────────────────────────────────────────────────┐
// │  IMPORTANT CONCEPTUAL BOUNDARY                          │
// │                                                         │
// │  CodeElement  = structural/AST representation           │
// │                 (produced by SourceParser)              │
// │                                                         │
// │  RetrievalUnit = retrieval-oriented representation      │
// │                  (produced by SupportingEvidenceResolver)│
// │                                                         │
// │  These are related but intentionally NOT the same.      │
// │  A CodeElement is never deleted or modified.            │
// │  A RetrievalUnit is a view over existing elements.      │
// └─────────────────────────────────────────────────────────┘
//
// Phase 6.3 scope:
//   - Defines RetrievalUnitRole, RetrievalUnitClassifier, RetrievalUnit.
//   - Does NOT integrate with SearchEngine, InvertedIndex, SemanticRetriever,
//     or HybridRetriever.
//   - Phase 6.4 is responsible for live query path integration.

#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/code_element.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace amoeba::retrieval {

// ─────────────────────────────────────────────────────────────────────────────
// RetrievalUnitRole
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Search-oriented role of a code element in the retrieval candidate set.
 *
 * Primary elements are standalone architectural symbols that a developer is
 * directly searching for (e.g. "find the AuthService class", "find useCalendar").
 *
 * Supporting elements are fine-grained AST nodes that provide context and
 * evidence for their enclosing primary symbol, but should not independently
 * compete at the top level of retrieval results.
 */
enum class RetrievalUnitRole : uint8_t {
    Primary,    ///< Standalone architectural symbol: Class, Struct, Interface,
                ///< Function, Method, Component, Hook, Route
    Supporting  ///< Fine-grained implementation detail: Call, Attribute,
                ///< JSXElement, JSXComponent, UtilityClass, Include,
                ///< Property, Selector, Unknown
};

[[nodiscard]] constexpr std::string_view to_string(RetrievalUnitRole role) noexcept {
    switch (role) {
    case RetrievalUnitRole::Primary:
        return "Primary";
    case RetrievalUnitRole::Supporting:
        return "Supporting";
    }
    return "Supporting";
}

// ─────────────────────────────────────────────────────────────────────────────
// RetrievalUnitClassifier
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Deterministic, stateless policy classifier mapping ElementKind to
 *        RetrievalUnitRole.
 *
 * Design:
 *  - All methods are constexpr — classification happens at compile time when
 *    ElementKind is a compile-time constant.
 *  - The default branch maps any unknown/future ElementKind to Supporting,
 *    preventing unclassified nodes from competing as primary results.
 *  - Classification policy is centralized here; adding a new ElementKind
 *    requires only a localized change to this switch.
 */
class RetrievalUnitClassifier {
public:
    /**
     * @brief Classifies an ElementKind into its RetrievalUnitRole.
     * @param kind The element kind from parser::ElementKind.
     * @return Primary or Supporting.
     */
    [[nodiscard]] static constexpr RetrievalUnitRole classify(parser::ElementKind kind) noexcept {
        switch (kind) {
        case parser::ElementKind::Class:
        case parser::ElementKind::Struct:
        case parser::ElementKind::Interface:
        case parser::ElementKind::Function:
        case parser::ElementKind::Method:
        case parser::ElementKind::Component:
        case parser::ElementKind::Hook:
        case parser::ElementKind::Route:
            return RetrievalUnitRole::Primary;

        case parser::ElementKind::Call:
        case parser::ElementKind::Attribute:
        case parser::ElementKind::JSXElement:
        case parser::ElementKind::JSXComponent:
        case parser::ElementKind::UtilityClass:
        case parser::ElementKind::Include:
        case parser::ElementKind::Property:
        case parser::ElementKind::Selector:
        case parser::ElementKind::Unknown:
        default:
            return RetrievalUnitRole::Supporting;
        }
    }

    /** @brief Returns true if kind classifies as Primary. */
    [[nodiscard]] static constexpr bool is_primary(parser::ElementKind kind) noexcept {
        return classify(kind) == RetrievalUnitRole::Primary;
    }

    /** @brief Returns true if kind classifies as Supporting. */
    [[nodiscard]] static constexpr bool is_supporting(parser::ElementKind kind) noexcept {
        return classify(kind) == RetrievalUnitRole::Supporting;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// RetrievalUnit
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Search-oriented view over a primary architectural symbol and its
 *        associated supporting evidence.
 *
 * A RetrievalUnit does not duplicate source code. It holds:
 *  - A reference to the primary CodeElement by value (copied from ParsedFile).
 *  - The ElementId of the primary element in the InvertedIndex.
 *  - References to supporting elements by their index-relative positions
 *    within the same ParsedFile.
 *
 * The full CodeElement representation in ParsedFile and InvertedIndex remains
 * intact and is not removed by Phase 6.3.
 */
struct RetrievalUnit {
    /// ElementId of the primary element in the InvertedIndex (0 for
    /// file-module units synthesized for primary-less files).
    index::ElementId primary_element_id{0};

    /// The primary architectural symbol.
    parser::CodeElement primary_element;

    /// Source file path (matches the ParsedFile that produced this unit).
    std::filesystem::path file_path;

    /// Language identifier (e.g. "TypeScript", "TSX", "CSS").
    std::string language;

    /// Always RetrievalUnitRole::Primary for a well-formed RetrievalUnit.
    RetrievalUnitRole role{RetrievalUnitRole::Primary};

    /// Positions (indices into ParsedFile::elements) of supporting elements.
    std::vector<uint32_t> supporting_element_ids;

    /// Supporting element values (copies for convenience; authoritative source
    /// is ParsedFile::elements).
    std::vector<parser::CodeElement> supporting_elements;

    [[nodiscard]] bool operator==(const RetrievalUnit&) const = default;
};

}  // namespace amoeba::retrieval
