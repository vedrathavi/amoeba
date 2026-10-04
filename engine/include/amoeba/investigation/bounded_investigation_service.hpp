#pragma once

#include "amoeba/evidence/evidence_bundle.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/investigation/investigation_types.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/retrieval/primary_search_result.hpp"
#include "amoeba/retrieval/query_understanding.hpp"
#include "amoeba/source/source_snippet_reader.hpp"

#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace amoeba::evidence {
class EvidenceAssembler;
struct EvidenceAssemblerOptions;
}

namespace amoeba::investigation {

/**
 * @brief Orchestrates deterministic, bounded 1-hop repository investigation.
 *
 * Responsibilities:
 * - Accepts initial Hop-0 hybrid retrieval candidates.
 * - Evaluates whether investigation is justified (insufficient initial evidence, relevant seed).
 * - Queries the RelationshipGraph for 1-hop outgoing and incoming edges.
 * - Performs AST vocabulary discovery on call and reference targets.
 * - Assembles, deduplicates, and bounds the candidate pool to at most max_total_candidates.
 * - Preserves complete provenance and deterministic ordering.
 * - Enforces zero-hallucination invariants (maximum 1 hop, no unbounded expansion).
 */
class BoundedInvestigationService {
public:
    /**
     * @brief Constructs BoundedInvestigationService with references to core engine capabilities.
     *
     * @param index           Inverted index for element and file metadata lookup.
     * @param graph           Repository relationship graph for AST edge traversal.
     * @param pipeline        Primary retrieval pipeline for primary unit resolution.
     * @param snippet_reader  Source snippet reader for bounded context extraction.
     */
    BoundedInvestigationService(
        const index::InvertedIndex& index,
        const graph::RelationshipGraph& graph,
        const retrieval::PrimaryRetrievalPipeline& pipeline,
        const source::SourceSnippetReader& snippet_reader);

    ~BoundedInvestigationService() = default;

    BoundedInvestigationService(const BoundedInvestigationService&) = delete;
    BoundedInvestigationService& operator=(const BoundedInvestigationService&) = delete;
    BoundedInvestigationService(BoundedInvestigationService&&) noexcept = delete;
    BoundedInvestigationService& operator=(BoundedInvestigationService&&) noexcept = delete;

    /**
     * @brief Executes bounded investigation and returns candidates with their investigation roles.
     */
    [[nodiscard]] std::vector<InvestigatedCandidate>
    investigate_candidates(std::string_view query,
                           std::span<const retrieval::PrimarySearchResult> hop0_candidates,
                           const InvestigationOptions& options = {},
                           InvestigationMetrics* metrics = nullptr) const;

    [[nodiscard]] std::vector<InvestigatedCandidate>
    investigate_candidates_with_metrics(const retrieval::QueryRepresentation& query_rep,
                                        std::span<const retrieval::PrimarySearchResult> hop0_candidates,
                                        const InvestigationOptions& options,
                                        InvestigationMetrics& metrics) const;

    /**
     * @brief Executes bounded investigation from Hop-0 candidates (convenience returning PrimarySearchResults).
     */
    [[nodiscard]] std::vector<retrieval::PrimarySearchResult>
    investigate(std::string_view query,
                std::span<const retrieval::PrimarySearchResult> hop0_candidates,
                const InvestigationOptions& options = {},
                InvestigationMetrics* metrics = nullptr) const;

    /**
     * @brief Overload that accepts a pre-analyzed QueryRepresentation and outputs detailed metrics.
     */
    [[nodiscard]] std::vector<retrieval::PrimarySearchResult>
    investigate_with_metrics(const retrieval::QueryRepresentation& query_rep,
                             std::span<const retrieval::PrimarySearchResult> hop0_candidates,
                             const InvestigationOptions& options,
                             InvestigationMetrics& metrics) const;

    /**
     * @brief Conditionally expands an existing candidate pool to Hop-2 depth.
     *
     * Used when Hop-0/Hop-1 evidence was deemed insufficient by EvidenceSufficiencyChecker.
     * Preserves existing seeds and Hop-1 candidates while discovering 2-hop AST and vocabulary targets.
     */
    [[nodiscard]] std::vector<InvestigatedCandidate>
    expand_to_hop2(std::span<const InvestigatedCandidate> existing_candidates,
                   const InvestigationOptions& options = {},
                   InvestigationMetrics* metrics = nullptr) const;

    [[nodiscard]] std::vector<InvestigatedCandidate>
    expand_to_hop2_with_metrics(std::span<const InvestigatedCandidate> existing_candidates,
                                const InvestigationOptions& options,
                                InvestigationMetrics& metrics) const;

    /**
     * @brief Assembles a unified EvidenceBundle from investigated candidates, preserving Hop-0 vs Hop-1 vs Hop-2 roles.
     */
    [[nodiscard]] evidence::EvidenceBundle
    assemble_bundle(std::string_view query,
                    std::span<const InvestigatedCandidate> candidates,
                    const evidence::EvidenceAssembler& assembler) const;

private:
    const index::InvertedIndex& index_;
    const graph::RelationshipGraph& graph_;
    const retrieval::PrimaryRetrievalPipeline& pipeline_;
    const source::SourceSnippetReader& snippet_reader_;

    // Lookup table from ElementId -> primary unit index in pipeline_.units()
    std::unordered_map<index::ElementId, std::size_t> element_to_unit_map_;

    void build_unit_lookup();
    [[nodiscard]] const retrieval::RetrievalUnit* find_unit_for_element(index::ElementId id) const;
    [[nodiscard]] const retrieval::RetrievalUnit* find_unit_by_symbol_name(std::string_view name) const;
};

}  // namespace amoeba::investigation
