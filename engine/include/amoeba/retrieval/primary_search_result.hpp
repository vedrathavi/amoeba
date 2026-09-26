#pragma once

// Phase 6.4 — Primary Search Result
//
// PrimarySearchResult is the retrieval unit-aware search result type.
//
// It replaces the mix of primary + supporting CodeElements in the raw
// SearchResult / HybridSearchResult types with a clear hierarchy:
//
//   primary_unit   — the architectural symbol that should be shown to the user
//   evidence       — supporting elements that contextually support the match
//   scores         — per-modality and fused scores
//   provenance     — which retrieval modalities contributed

#include "amoeba/retrieval/retrieval_unit.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace amoeba::retrieval {

/**
 * @brief Indicates which retrieval modalities contributed to this result.
 */
enum class RetrievalProvenance : uint8_t {
    LexicalOnly = 0x01,   ///< Matched by lexical retrieval only
    SemanticOnly = 0x02,  ///< Matched by semantic retrieval only
    HybridBoth = 0x03,    ///< Matched by both lexical and semantic retrieval
};

[[nodiscard]] constexpr std::string_view to_string(RetrievalProvenance p) noexcept {
    switch (p) {
    case RetrievalProvenance::LexicalOnly:
        return "lexical";
    case RetrievalProvenance::SemanticOnly:
        return "semantic";
    case RetrievalProvenance::HybridBoth:
        return "hybrid";
    }
    return "unknown";
}

/**
 * @brief The top-level retrieval result from the Phase 6.4 pipeline.
 *
 * A PrimarySearchResult always represents a primary architectural symbol —
 * a Class, Function, Component, Hook, Route, etc. — with its supporting
 * evidence elements attached for context.
 *
 * Supporting elements are NEVER returned as independent PrimarySearchResults.
 */
struct PrimarySearchResult {
    /// The primary architectural symbol and its supporting evidence.
    RetrievalUnit unit;

    /// Raw lexical score (0.0 if not retrieved lexically).
    double lexical_score{0.0};

    /// Normalized lexical score in [0, 1] (0.0 if not retrieved lexically).
    double normalized_lexical_score{0.0};

    /// 1-indexed rank in lexical results (0 if not retrieved lexically).
    uint32_t lexical_rank{0};

    /// Raw semantic similarity score (0.0 if not retrieved semantically).
    double semantic_score{0.0};

    /// Normalized semantic score in [0, 1] (0.0 if not retrieved semantically).
    double normalized_semantic_score{0.0};

    /// 1-indexed rank in semantic results (0 if not retrieved semantically).
    uint32_t semantic_rank{0};

    /// Final fused score (weighted or RRF).
    double hybrid_score{0.0};

    /// Which retrieval modalities contributed to this result.
    RetrievalProvenance provenance{RetrievalProvenance::LexicalOnly};

    [[nodiscard]] bool operator==(const PrimarySearchResult&) const = default;
};

}  // namespace amoeba::retrieval
