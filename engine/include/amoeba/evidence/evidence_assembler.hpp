#pragma once

#include "amoeba/evidence/evidence_bundle.hpp"
#include "amoeba/graph/relationship_evidence_resolver.hpp"
#include "amoeba/graph/relationship_expander.hpp"
#include "amoeba/retrieval/primary_search_result.hpp"
#include "amoeba/source/source_snippet_reader.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace amoeba::evidence {

/**
 * @brief Configuration options for assembling evidence.
 */
struct EvidenceAssemblerOptions {
    /// Number of context lines before and after the primary source range.
    uint32_t source_context_lines{0};

    /// Configuration for direct graph relationship resolution (e.g. fanout limits).
    graph::RelationshipEvidenceOptions relationship_options{};

    /// Whether to extract source code snippets.
    bool include_source_snippets{true};

    /// Whether to resolve direct graph relationships metadata.
    bool include_relationships{true};

    /// Phase 8.2.9: Bounded typed relationship expansion configuration.
    graph::ExpansionConfig expansion_config{};

    /// Whether to perform bounded relationship expansion.
    bool enable_expansion{false};
};

/**
 * @brief Assembles primary search results, source excerpts, supporting AST evidence,
 *        and direct graph relationships into a unified EvidenceBundle.
 *
 * Orchestrates:
 *  - SourceSnippetReader (verbatim source code extraction)
 *  - SupportingEvidenceResolver / PrimarySearchResult (supporting AST elements)
 *  - RelationshipEvidenceResolver (direct 1-hop relationship metadata)
 *  - RelationshipExpander (bounded typed 1-hop and 2-hop relationship expansion)
 *
 * Guarantees:
 *  - Deterministic ordering matching retrieval ranking + ranked expansions.
 *  - Complete data ownership in the returned EvidenceBundle.
 *  - Full provenance tracing on every expanded evidence item.
 *  - Strict unit and source character budgeting.
 */
class EvidenceAssembler {
public:
    /**
     * @brief Constructs an EvidenceAssembler referencing a RelationshipEvidenceResolver
     *        and an optional custom SourceSnippetReader.
     */
    explicit EvidenceAssembler(const graph::RelationshipEvidenceResolver& relationship_resolver);

    EvidenceAssembler(const source::SourceSnippetReader& snippet_reader,
                      const graph::RelationshipEvidenceResolver& relationship_resolver);

    EvidenceAssembler(const source::SourceSnippetReader& snippet_reader,
                      const graph::RelationshipEvidenceResolver& relationship_resolver,
                      const graph::RelationshipExpander& relationship_expander);

    ~EvidenceAssembler() = default;

    EvidenceAssembler(const EvidenceAssembler&) = delete;
    EvidenceAssembler& operator=(const EvidenceAssembler&) = delete;
    EvidenceAssembler(EvidenceAssembler&&) noexcept = delete;
    EvidenceAssembler& operator=(EvidenceAssembler&&) noexcept = delete;

    /**
     * @brief Assembles an EvidenceBundle for a query from a span of search results.
     */
    [[nodiscard]] EvidenceBundle
    assemble(std::string_view query, std::span<const retrieval::PrimarySearchResult> search_results,
             const EvidenceAssemblerOptions& options = {}) const;

    /**
     * @brief Overload for vector of PrimarySearchResult.
     */
    [[nodiscard]] EvidenceBundle
    assemble(std::string_view query,
             const std::vector<retrieval::PrimarySearchResult>& search_results,
             const EvidenceAssemblerOptions& options = {}) const {
        return assemble(query, std::span<const retrieval::PrimarySearchResult>(search_results),
                        options);
    }

    /**
     * @brief Assembles a single EvidenceItem from a PrimarySearchResult.
     */
    [[nodiscard]] EvidenceItem assemble_item(const retrieval::PrimarySearchResult& result,
                                             const EvidenceAssemblerOptions& options = {}) const;

private:
    source::SourceSnippetReader default_snippet_reader_{};
    const source::SourceSnippetReader& snippet_reader_;
    const graph::RelationshipEvidenceResolver& relationship_resolver_;
    const graph::RelationshipExpander* relationship_expander_{nullptr};
};

}  // namespace amoeba::evidence
