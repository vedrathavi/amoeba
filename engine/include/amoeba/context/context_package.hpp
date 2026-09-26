#pragma once

#include "amoeba/evidence/evidence_bundle.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace amoeba::context {

/**
 * @brief Structured, bounded context presentation generated from an EvidenceBundle.
 *
 * A ContextPackage contains:
 *  - The original query.
 *  - The selected structured evidence items (retaining provenance and metadata).
 *  - The model-independent rendered Markdown representation.
 *  - Budget, usage, and truncation accounting.
 *
 * It is completely self-contained, owning all its data, and free of raw pointers,
 * graph structures, AST nodes, or model-specific prompt templates.
 */
struct ContextPackage {
    /// The original user query.
    std::string query;

    /// The structured evidence items selected for this context package.
    std::vector<evidence::EvidenceItem> selected_items;

    /// Rendered Markdown representation of the selected context.
    std::string rendered_markdown;

    /// Total number of primary evidence items available in the input bundle.
    std::size_t total_available_items{0};

    /// Number of primary evidence items selected into this package.
    std::size_t selected_item_count{0};

    /// Configured maximum character budget (0 for unlimited).
    std::size_t max_character_budget{0};

    /// Total characters in the rendered Markdown representation.
    std::size_t used_characters{0};

    /// Whether any content was truncated or omitted due to item or character limits.
    bool truncated{false};

    /// Optional description of truncation reason, if truncated is true.
    std::string truncation_reason;

    [[nodiscard]] bool empty() const noexcept { return selected_items.empty(); }

    [[nodiscard]] std::size_t size() const noexcept { return selected_items.size(); }

    [[nodiscard]] bool operator==(const ContextPackage&) const = default;
};

}  // namespace amoeba::context
