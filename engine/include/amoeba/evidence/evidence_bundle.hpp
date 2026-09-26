#pragma once

#include "amoeba/graph/relationship_evidence.hpp"
#include "amoeba/retrieval/primary_search_result.hpp"
#include "amoeba/source/source_excerpt.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace amoeba::evidence {

/**
 * @brief Represents all assembled evidence for a single retrieved primary symbol.
 *
 * An EvidenceItem encapsulates:
 *  - The PrimarySearchResult (identity, kind, location, scores, rank, provenance)
 *  - The extracted SourceExcerpt (if source file was available on disk)
 *  - Supporting AST elements attached to this primary unit
 *  - Resolved direct 1-hop RelationshipEvidence from the relationship graph
 */
struct EvidenceItem {
    /// The primary retrieval result (contains primary element, file path, scores, provenance).
    retrieval::PrimarySearchResult primary_result;

    /// Verbatim source code excerpt corresponding to the primary symbol (if available).
    std::optional<source::SourceExcerpt> source_excerpt{std::nullopt};

    /// Resolved direct 1-hop relationships (callers, callees, inheritance, imports, etc.).
    std::vector<graph::RelationshipEvidence> direct_relationships;

    /// Convenience accessors
    [[nodiscard]] const parser::CodeElement& primary_element() const noexcept {
        return primary_result.unit.primary_element;
    }

    [[nodiscard]] index::ElementId primary_element_id() const noexcept {
        return primary_result.unit.primary_element_id;
    }

    [[nodiscard]] const std::filesystem::path& file_path() const noexcept {
        return primary_result.unit.file_path;
    }

    [[nodiscard]] const std::vector<parser::CodeElement>& supporting_elements() const noexcept {
        return primary_result.unit.supporting_elements;
    }

    [[nodiscard]] bool has_source_excerpt() const noexcept { return source_excerpt.has_value(); }

    [[nodiscard]] bool operator==(const EvidenceItem&) const = default;
};

/**
 * @brief Represents the complete assembled evidence for a user query.
 *
 * An EvidenceBundle is an owned, self-contained, model-independent representation
 * of all retrieved primary units, their source excerpts, supporting AST context,
 * and direct graph relationships.
 *
 * It is completely decoupled from graph internals, inverted index internals,
 * AST pointers, and LLM-specific formatting.
 */
struct EvidenceBundle {
    /// The original user query (unaltered).
    std::string query;

    /// The ordered list of assembled evidence items, preserving retrieval ranking order.
    std::vector<EvidenceItem> items;

    [[nodiscard]] bool empty() const noexcept { return items.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return items.size(); }

    [[nodiscard]] bool operator==(const EvidenceBundle&) const = default;
};

}  // namespace amoeba::evidence
