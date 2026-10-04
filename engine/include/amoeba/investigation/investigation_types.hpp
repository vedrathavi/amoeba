#pragma once

#include "amoeba/graph/relationship_kind.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/retrieval/primary_search_result.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace amoeba::investigation {

/**
 * @brief Role / provenance of a candidate in the investigation pool.
 */
enum class InvestigationCandidateRole : uint8_t {
    Hop0Primary,        ///< Initial primary retrieval candidate from hybrid search.
    Hop1Relationship,   ///< Discovered via 1-hop AST relationship (Calls, Imports, References, etc.).
    Hop1Vocabulary,     ///< Discovered via referenced technical symbol in seed candidate.
    Hop2Relationship,   ///< Discovered via 2-hop AST relationship.
    Hop2Vocabulary      ///< Discovered via 2-hop referenced technical symbol.
};

[[nodiscard]] constexpr std::string_view to_string(InvestigationCandidateRole role) noexcept {
    switch (role) {
    case InvestigationCandidateRole::Hop0Primary:
        return "Hop0Primary";
    case InvestigationCandidateRole::Hop1Relationship:
        return "Hop1Relationship";
    case InvestigationCandidateRole::Hop1Vocabulary:
        return "Hop1Vocabulary";
    case InvestigationCandidateRole::Hop2Relationship:
        return "Hop2Relationship";
    case InvestigationCandidateRole::Hop2Vocabulary:
        return "Hop2Vocabulary";
    }
    return "Hop0Primary";
}

/**
 * @brief Candidate code unit with full investigation role and expansion provenance.
 */
struct InvestigatedCandidate {
    retrieval::PrimarySearchResult result;
    InvestigationCandidateRole role{InvestigationCandidateRole::Hop0Primary};
    index::ElementId seed_element_id{0};
    std::string seed_symbol_name;
    graph::RelationshipKind relationship_kind{graph::RelationshipKind::Calls};
    std::string direction{"Outgoing"};
    std::size_t original_rank{0};
    bool is_top10{true};
    bool is_selected_seed{false};
    bool is_hop1_discovered{false};
    bool is_hop2_discovered{false};
    uint32_t hop_depth{0};
};

/**
 * @brief Configuration options for bounded repository investigation.
 */
struct InvestigationOptions {
    /// Whether bounded investigation is active.
    bool enabled{true};

    /// Maximum relationship hops (Phase 9.3C supports bounded 2-hop).
    uint32_t max_hops{2};

    /// Maximum primary seed candidates to investigate from Hop-0.
    uint32_t max_seeds{5};

    /// Maximum related candidates to discover per seed at Hop-1.
    uint32_t max_discovered_per_seed{3};

    /// Maximum related candidates to discover per Hop-1 candidate at Hop-2.
    uint32_t max_discovered_per_hop1{2};

    /// Maximum total candidates in the final returned pool.
    uint32_t max_total_candidates{15};

    /// Minimum hybrid score for a Hop-0 candidate to qualify as an investigation seed.
    double min_seed_relevance{0.05};

    /// Whether to enable vocabulary discovery from AST calls/references.
    bool enable_vocabulary_discovery{true};

    /// Discount factor applied to discovered 1-hop candidates relative to their seed.
    double hop1_score_discount{0.85};

    /// Discount factor applied to discovered 2-hop candidates relative to their Hop-1 parent.
    double hop2_score_discount{0.70};
};

/**
 * @brief Metadata recording a discovery step during bounded investigation.
 */
struct InvestigationStep {
    uint32_t step_number{1};
    index::ElementId seed_element_id{0};
    std::string seed_symbol_name;
    index::ElementId target_element_id{0};
    std::string target_symbol_name;
    graph::RelationshipKind relationship_kind{graph::RelationshipKind::Calls};
    InvestigationCandidateRole role{InvestigationCandidateRole::Hop1Relationship};
};

/**
 * @brief Performance and discovery metrics for an investigation execution.
 */
struct InvestigationMetrics {
    bool investigation_triggered{false};
    std::size_t hop0_candidate_count{0};
    std::size_t seeds_investigated{0};
    std::size_t hop1_candidates_discovered{0};
    std::size_t hop2_candidates_discovered{0};
    std::size_t final_candidate_count{0};
    double seed_selection_latency_ms{0.0};
    double investigation_latency_ms{0.0};
    std::vector<InvestigationStep> steps;
    std::vector<std::string> selected_seed_names;
};

}  // namespace amoeba::investigation
