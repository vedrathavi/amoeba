#pragma once

// Phase 6.4 — Primary Retrieval Pipeline
//
// PrimaryRetrievalPipeline integrates the Phase 6.3 RetrievalUnit abstraction
// into the live C++ retrieval path.
//
// ┌──────────────────────────────────────────────────────────────────────────┐
// │  SCOPE BOUNDARY                                                           │
// │                                                                           │
// │  Phase 6.4 adds this component.                                           │
// │                                                                           │
// │  The following are intentionally NOT modified:                            │
// │    InvertedIndex, SearchEngine, BaselineRanker, BM25Ranker,              │
// │    CodeAwareRanker, SemanticRetriever, HybridRetriever,                  │
// │    FusionStrategy, ScoreNormalizer, RelationshipGraph.                   │
// │                                                                           │
// │  PrimaryRetrievalPipeline is an integration layer that orchestrates      │
// │  the existing components and adds primary-unit-level result surfacing.   │
// └──────────────────────────────────────────────────────────────────────────┘
//
// Architecture:
//
//   ParsedFile[]
//       │
//       ├──→ InvertedIndex (ALL elements, unchanged — full lexical index)
//       │
//       └──→ RetrievalUnit[] (via SupportingEvidenceResolver)
//                 │
//                 └──→ SemanticIndex (PRIMARY unit embeddings only)
//
//   Query
//       ↓
//   PrimaryRetrievalPipeline::search()
//       ├── SearchEngine::search()          (raw CodeElement results)
//       │       ↓ map ElementId → primary RetrievalUnit
//       ├── SemanticRetriever::retrieve()   (primary-unit-only semantic results)
//       │       ↓ (already primary — SemanticIndex built from units)
//       ├── ScoreNormalizer + FusionStrategy
//       └── PrimarySearchResult[]

#include "amoeba/hybrid/fusion_strategy.hpp"
#include "amoeba/hybrid/score_normalizer.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/index/search_engine.hpp"
#include "amoeba/parser/parsed_file.hpp"
#include "amoeba/retrieval/primary_search_result.hpp"
#include "amoeba/retrieval/query_understanding.hpp"
#include "amoeba/retrieval/retrieval_unit.hpp"
#include "amoeba/retrieval/supporting_evidence_resolver.hpp"
#include "amoeba/semantic/embedding_provider.hpp"
#include "amoeba/semantic/semantic_index.hpp"
#include "amoeba/semantic/semantic_retriever.hpp"

#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace amoeba::retrieval {

/**
 * @brief Configuration for a primary retrieval search query.
 */
struct PrimarySearchOptions {
    double alpha{0.5};  ///< Lexical weight [0=semantic only, 1=lexical only]
    hybrid::FusionMethod fusion_method{hybrid::FusionMethod::WeightedScore};
    double rrf_k{60.0};  ///< RRF smoothing constant
    index::RankerType lexical_ranker{index::RankerType::CodeAware};
    std::size_t lexical_top_k{50};   ///< Candidate depth for lexical retrieval
    std::size_t semantic_top_k{50};  ///< Candidate depth for semantic retrieval
    std::size_t max_results{10};     ///< Maximum number of primary results to return
    std::optional<parser::ElementKind> kind_filter{std::nullopt};  ///< Optional kind filter
    bool adaptive_fusion{true};  ///< Phase 6.6: adaptively tune fusion weights by query intent
};

/**
 * @brief Performance measurements from a single search execution.
 */
struct PipelineMetrics {
    std::size_t total_elements{0};       ///< Total CodeElements in InvertedIndex
    std::size_t primary_unit_count{0};   ///< Primary RetrievalUnits in semantic index
    std::size_t supporting_count{0};     ///< Total supporting elements (unindexed semantically)
    std::size_t lexical_candidates{0};   ///< Raw lexical results before mapping
    std::size_t semantic_candidates{0};  ///< Semantic results returned
    std::size_t fused_candidates{0};     ///< Candidates after union and fusion
    std::size_t final_results{0};        ///< Results returned to caller

    double lexical_ms{0.0};   ///< Lexical retrieval duration (ms)
    double semantic_ms{0.0};  ///< Semantic retrieval duration (ms)
    double fusion_ms{0.0};    ///< Fusion duration (ms)
    double total_ms{0.0};     ///< Total pipeline duration (ms)
};

/**
 * @brief Integrates the Phase 6.3 RetrievalUnit model into the live retrieval pipeline.
 *
 * Construction:
 *   - Resolves RetrievalUnits from ParsedFiles via SupportingEvidenceResolver.
 *   - Builds a PRIMARY-only SemanticIndex by embedding each primary unit's
 *     enriched text representation (via SemanticTextFormatter::format_unit).
 *   - Builds an element_id → RetrievalUnit lookup map for post-retrieval mapping.
 *   - Does NOT rebuild the InvertedIndex (passed by const reference).
 *
 * Query execution:
 *   - Runs lexical retrieval via SearchEngine → maps raw results to primary units.
 *   - Runs semantic retrieval via SemanticRetriever (primary-only index).
 *   - Fuses candidates via ScoreNormalizer + FusionStrategy.
 *   - Returns PrimarySearchResult[] where every result is a primary symbol.
 *
 * Ownership:
 *   - semantic_index_  is owned (value member).
 *   - units_           are owned (value member).
 *   - index_           is a const reference (not owned).
 *   - provider_        is a const reference (not owned).
 */
class PrimaryRetrievalPipeline {
public:
    /**
     * @brief Constructs the pipeline from a set of parsed files and an existing index.
     *
     * @param parsed_files  All parsed files (used to build RetrievalUnits and SemanticIndex).
     * @param index         The pre-built InvertedIndex (const reference, not owned).
     * @param provider      Embedding provider for semantic index construction and query embedding.
     */
    PrimaryRetrievalPipeline(std::span<const parser::ParsedFile> parsed_files,
                             const index::InvertedIndex& index,
                             const semantic::EmbeddingProvider& provider);

    /**
     * @brief Executes a primary retrieval search.
     *
     * Returns only primary architectural symbols. Supporting elements are
     * never surfaced as independent top-level results; they appear as evidence
     * attached to their owning primary unit.
     *
     * @param query   The developer query string (identifier or natural language).
     * @param options Search configuration.
     * @return Ordered list of PrimarySearchResult items.
     */
    [[nodiscard]] std::vector<PrimarySearchResult>
    search(std::string_view query, const PrimarySearchOptions& options = {}) const;

    /**
     * @brief Executes a search and also returns pipeline metrics for the query.
     *
     * @param query   The developer query string.
     * @param options Search configuration.
     * @param metrics [out] Performance and candidate-count measurements.
     * @return Ordered list of PrimarySearchResult items.
     */
    [[nodiscard]] std::vector<PrimarySearchResult>
    search_with_metrics(std::string_view query, const PrimarySearchOptions& options,
                        PipelineMetrics& metrics) const;

    // ─── Inspection ───────────────────────────────────────────────────────────

    /** @brief Total primary RetrievalUnits built at construction. */
    [[nodiscard]] std::size_t primary_unit_count() const noexcept { return units_.size(); }

    /** @brief Total supporting elements across all units. */
    [[nodiscard]] std::size_t supporting_element_count() const noexcept;

    /** @brief Whether the pipeline has any indexed primary units. */
    [[nodiscard]] bool empty() const noexcept { return units_.empty(); }

    /** @brief All primary RetrievalUnits (for inspection and testing). */
    [[nodiscard]] const std::vector<RetrievalUnit>& units() const noexcept { return units_; }

    /** @brief Semantic index accessor (for inspection and testing). */
    [[nodiscard]] const semantic::SemanticIndex& semantic_index() const noexcept {
        return semantic_index_;
    }

    /** @brief Total cached element match keys in the lookup map. */
    [[nodiscard]] std::size_t cached_element_match_count() const noexcept {
        return elem_key_to_id_.size();
    }


private:
    struct ElementMatchKey {
        std::string file_path;
        std::string name;
        uint32_t start_line{0};
        uint32_t start_col{0};

        bool operator==(const ElementMatchKey& o) const noexcept {
            return start_line == o.start_line && start_col == o.start_col && name == o.name &&
                   file_path == o.file_path;
        }
    };

    struct ElementMatchKeyHash {
        std::size_t operator()(const ElementMatchKey& k) const noexcept {
            const std::size_t h1 = std::hash<std::string>{}(k.file_path);
            const std::size_t h2 = std::hash<std::string>{}(k.name);
            const std::size_t h3 = std::hash<uint32_t>{}(k.start_line);
            const std::size_t h4 = std::hash<uint32_t>{}(k.start_col);
            return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
        }
    };

    using ElementToIdMap = std::unordered_map<ElementMatchKey, index::ElementId, ElementMatchKeyHash>;

    const index::InvertedIndex& index_;
    const semantic::EmbeddingProvider& provider_;

    std::vector<RetrievalUnit> units_;
    semantic::SemanticIndex semantic_index_;
    index::SearchEngine search_engine_;
    semantic::SemanticRetriever semantic_retriever_;

    /// Cached lookup from (file_path, name, start_line, start_col) → InvertedIndex ElementId.
    /// Built once at pipeline construction, eliminating per-query O(N) allocation overhead.
    ElementToIdMap elem_key_to_id_;

    /// Maps InvertedIndex ElementId → index into units_ vector.
    /// Populated at construction for O(1) post-retrieval mapping.
    std::unordered_map<index::ElementId, std::size_t> element_id_to_unit_;

    void build_units(std::span<const parser::ParsedFile> parsed_files);
    void reconcile_element_ids();  ///< Patches primary_element_id from InvertedIndex
    void build_semantic_index();
    void build_element_id_map();
};

}  // namespace amoeba::retrieval
