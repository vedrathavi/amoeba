#pragma once

#include "amoeba/evidence/evidence_bundle.hpp"
#include "amoeba/graph/relationship_evidence_resolver.hpp"
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

    /// Whether to resolve direct graph relationships.
    bool include_relationships{true};
};

/**
 * @brief Assembles primary search results, source excerpts, supporting AST evidence,
 *        and direct graph relationships into a unified EvidenceBundle.
 *
 * Orchestrates:
 *  - SourceSnippetReader (verbatim source code extraction)
 *  - SupportingEvidenceResolver / PrimarySearchResult (supporting AST elements)
 *  - RelationshipEvidenceResolver (bounded 1-hop graph relationships)
 *
 * Guarantees:
 *  - Deterministic ordering matching the input retrieval ranking.
 *  - Complete data ownership in the returned EvidenceBundle.
 *  - Safe handling of missing source files, invalid ranges, or empty search results.
 *  - Zero mutation of input search results.
 *  - No recursive graph traversal, deep context budgeting, or prompt formatting.
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
};

}  // namespace amoeba::evidence
