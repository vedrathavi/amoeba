#pragma once

// Phase 6.3 — Supporting Evidence Resolver
//
// Responsibility: Given a ParsedFile, construct the set of RetrievalUnits by
// associating supporting CodeElements with their nearest enclosing primary symbol.
//
// This component does NOT modify ParsedFile, InvertedIndex, SearchEngine, or
// any production retrieval path. It is a read-only view constructor.

#include "amoeba/parser/parsed_file.hpp"
#include "amoeba/retrieval/retrieval_unit.hpp"

#include <optional>
#include <span>
#include <vector>

namespace amoeba::retrieval {

/**
 * @brief Result of attempting to resolve a supporting element's owner.
 *
 * owner_unit_index is the index into the RetrievalUnit vector when resolution
 * succeeds. std::nullopt means the element could not be confidently attributed
 * to any primary symbol in the file.
 */
struct EvidenceResolution {
    uint32_t supporting_element_idx{0};        ///< Position in ParsedFile::elements
    std::optional<uint32_t> owner_unit_index;  ///< Index into units vector, or nullopt
};

/**
 * @brief Constructs hierarchical RetrievalUnits from parsed source files.
 *
 * Resolution rules (applied in priority order):
 *
 *   1. parent_context match:
 *      If child.parent_context is non-empty and equals a primary unit's name,
 *      the child is attributed to that unit. This is the authoritative
 *      structural attribution from the Tree-sitter parser.
 *
 *   2. Source range containment:
 *      If the child's source range is fully enclosed within a primary unit's
 *      source range, the child is attributed to the innermost such unit
 *      (smallest span).
 *
 *   3. Unresolved (no fallback to unrelated symbols):
 *      If neither rule above produces a confident match, the element is left
 *      unresolved. It is NOT silently assigned to an unrelated primary symbol
 *      in the same file. This is important for files with multiple independent
 *      top-level functions or components.
 *
 * File-level module unit:
 *      If a file contains no primary symbols at all (e.g., globals.css,
 *      config-only files), a synthetic module-level primary unit is created
 *      so the file is still searchable. All elements in such a file are
 *      attributed to that single unit.
 */
class SupportingEvidenceResolver {
public:
    SupportingEvidenceResolver() = default;

    /**
     * @brief Resolves and constructs RetrievalUnits from a single ParsedFile.
     * @param parsed_file Source file with structural CodeElements.
     * @return Ordered list of primary RetrievalUnits with attached evidence.
     */
    [[nodiscard]] static std::vector<RetrievalUnit>
    resolve_units(const parser::ParsedFile& parsed_file);

    /**
     * @brief Resolves RetrievalUnits across multiple parsed source files.
     * @param parsed_files Span of ParsedFiles (e.g., from a full repository scan).
     * @return All primary RetrievalUnits across all files, in file order.
     */
    [[nodiscard]] static std::vector<RetrievalUnit>
    resolve_units(std::span<const parser::ParsedFile> parsed_files);

    /**
     * @brief Returns resolution details for each supporting element.
     *
     * This overload is primarily for testing and diagnostics. It exposes which
     * elements were resolved and which remained unattributed.
     *
     * @param parsed_file Source file.
     * @param units [out] Constructed RetrievalUnits (same as resolve_units).
     * @param resolutions [out] Per-element resolution results.
     */
    static void resolve_with_diagnostics(const parser::ParsedFile& parsed_file,
                                         std::vector<RetrievalUnit>& units,
                                         std::vector<EvidenceResolution>& resolutions);

    /**
     * @brief Returns true if child is structurally enclosed within parent.
     *
     * Checks (in order):
     *   1. parent_context == parent.name (authoritative)
     *   2. source range containment
     */
    [[nodiscard]] static bool is_enclosed_by(const parser::CodeElement& child,
                                             const parser::CodeElement& parent) noexcept;
};

}  // namespace amoeba::retrieval
