#pragma once

#include "amoeba/context/context_package.hpp"
#include "amoeba/evidence/evidence_bundle.hpp"

#include <cstddef>

namespace amoeba::context {

/**
 * @brief Configuration options for ContextBuilder.
 */
struct ContextBuilderOptions {
    /// Maximum number of primary retrieved items to select (default: 3).
    std::size_t max_primary_items{3};

    /// Maximum lines of source code excerpt to render per item (default: 50).
    std::size_t max_source_lines_per_item{50};

    /// Maximum direct relationships to render per item (default: 5).
    std::size_t max_relationships_per_item{5};

    /// Maximum supporting AST elements to render per item (default: 5).
    std::size_t max_supporting_elements_per_item{5};

    /// Maximum total characters for the rendered context (default: 16000, 0 = unlimited).
    std::size_t max_character_budget{16000};

    /// Whether to render source code excerpts.
    bool include_source{true};

    /// Whether to render supporting AST elements.
    bool include_supporting_evidence{true};

    /// Whether to render direct graph relationships.
    bool include_relationships{true};

    /// Whether to render symbol metadata (kind, file path, range, etc.).
    bool include_metadata{true};
};

/**
 * @brief Selects, prioritizes, bounds, and formats evidence from an EvidenceBundle
 *        into a ContextPackage.
 *
 * Responsibilities:
 *  1. Select top-ranked primary evidence items up to max_primary_items.
 *  2. Apply bounded line limits on source excerpts.
 *  3. Apply bounded limits on supporting AST elements and direct relationships.
 *  4. Enforce total character budget with deterministic, UTF-8-safe truncation.
 *  5. Generate structured and model-independent Markdown context.
 *
 * Guarantees:
 *  - 100% deterministic: identical inputs produce identical ContextPackages.
 *  - Consumes ONLY EvidenceBundle (zero filesystem, graph, or retrieval calls).
 *  - UTF-8 safe truncation (never splits multibyte code points).
 *  - Preserves retrieval rank order (higher-ranked items prioritized over lower-ranked).
 *  - Safe handling of empty, partial, or missing evidence without fabricating facts.
 */
class ContextBuilder {
public:
    explicit ContextBuilder(const ContextBuilderOptions& options = {});

    ~ContextBuilder() = default;

    ContextBuilder(const ContextBuilder&) = default;
    ContextBuilder& operator=(const ContextBuilder&) = default;
    ContextBuilder(ContextBuilder&&) noexcept = default;
    ContextBuilder& operator=(ContextBuilder&&) noexcept = default;

    /**
     * @brief Constructs a ContextPackage from an EvidenceBundle.
     *
     * @param bundle The input EvidenceBundle (const reference, not mutated).
     * @return Bounded, structured, and rendered ContextPackage.
     */
    [[nodiscard]] ContextPackage build(const evidence::EvidenceBundle& bundle) const;

    /**
     * @brief Returns the active configuration options.
     */
    [[nodiscard]] const ContextBuilderOptions& options() const noexcept { return options_; }

private:
    ContextBuilderOptions options_;
};

}  // namespace amoeba::context
