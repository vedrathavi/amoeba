#pragma once

#include "amoeba/graph/relationship_evidence.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/retrieval/primary_search_result.hpp"
#include "amoeba/retrieval/retrieval_unit.hpp"

#include <cstddef>
#include <vector>

namespace amoeba::graph {

/**
 * @brief Configuration options for resolving direct graph relationships.
 */
struct RelationshipEvidenceOptions {
    std::size_t max_outgoing_per_kind{5};  ///< Maximum outgoing relations per relationship kind
    std::size_t max_incoming_per_kind{5};  ///< Maximum incoming relations per relationship kind
    bool include_incoming{true};  ///< Whether to resolve incoming relationships (e.g. callers)
    bool include_outgoing{true};  ///< Whether to resolve outgoing relationships (e.g. callees)
};

/**
 * @brief Resolves direct 1-hop relationship evidence for primary code symbols.
 *
 * Responsibilities:
 *  1. Look up adjacent incoming and outgoing graph edges for a focal primary symbol.
 *  2. Resolve target element metadata (name, kind, file path, source location) from InvertedIndex.
 *  3. Apply deterministic fanout limits per relationship kind to prevent high-degree bloat.
 *  4. Guarantee stable, deterministic ordering without deep/recursive traversal.
 */
class RelationshipEvidenceResolver {
public:
    /**
     * @brief Constructs resolver referencing an existing RelationshipGraph and InvertedIndex.
     *
     * @param graph The populated repository relationship graph (const reference, not owned).
     * @param index The populated repository inverted index (const reference, not owned).
     */
    RelationshipEvidenceResolver(const RelationshipGraph& graph, const index::InvertedIndex& index);

    ~RelationshipEvidenceResolver() = default;

    RelationshipEvidenceResolver(const RelationshipEvidenceResolver&) = default;
    RelationshipEvidenceResolver& operator=(const RelationshipEvidenceResolver&) = delete;
    RelationshipEvidenceResolver(RelationshipEvidenceResolver&&) noexcept = default;
    RelationshipEvidenceResolver& operator=(RelationshipEvidenceResolver&&) noexcept = delete;

    /**
     * @brief Resolves direct 1-hop relationship evidence for a specific primary element ID.
     *
     * @param primary_element_id ID of the primary symbol.
     * @param options Configuration options (fanout limits, direction filters).
     * @return Deterministically ordered list of RelationshipEvidence items.
     */
    [[nodiscard]] std::vector<RelationshipEvidence>
    resolve(ElementId primary_element_id, const RelationshipEvidenceOptions& options = {}) const;

    /**
     * @brief Resolves direct 1-hop relationship evidence for a RetrievalUnit.
     */
    [[nodiscard]] std::vector<RelationshipEvidence>
    resolve(const retrieval::RetrievalUnit& unit,
            const RelationshipEvidenceOptions& options = {}) const;

    /**
     * @brief Resolves direct 1-hop relationship evidence for a PrimarySearchResult.
     */
    [[nodiscard]] std::vector<RelationshipEvidence>
    resolve(const retrieval::PrimarySearchResult& result,
            const RelationshipEvidenceOptions& options = {}) const;

private:
    const RelationshipGraph& graph_;
    const index::InvertedIndex& index_;

    void populate_related_metadata(ElementId target_id, RelationshipEvidence& evidence) const;
};

}  // namespace amoeba::graph
