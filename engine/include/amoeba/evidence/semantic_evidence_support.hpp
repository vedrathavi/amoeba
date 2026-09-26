#pragma once

#include "amoeba/evidence/evidence_bundle.hpp"
#include "amoeba/retrieval/query_understanding.hpp"
#include "amoeba/semantic/embedding_provider.hpp"
#include "amoeba/semantic/semantic_index.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::evidence {

/**
 * @brief Semantic match record between a single query Subject concept and a candidate evidence
 * item.
 */
struct SemanticEvidenceMatch {
    std::string query_concept;
    index::ElementId element_id{0};
    std::string element_name;
    std::string file_path;
    float similarity{0.0f};
    bool is_lexical_match{false};
    bool is_structural_match{false};

    [[nodiscard]] bool operator==(const SemanticEvidenceMatch&) const = default;
};

/**
 * @brief Configuration parameters for semantic evidence support evaluation.
 */
struct SemanticEvidenceOptions {
    /// Noise floor threshold used for candidate search extraction (principled engineering bound).
    float min_candidate_similarity{0.50f};

    /// Minimum similarity required for a top semantic candidate to be considered a viable match.
    float min_semantic_support_similarity{0.60f};

    /// Minimum relative separation margin (s_top - s_background) required for semantic grounding.
    float min_separation_margin{0.20f};
};

/**
 * @brief Aggregated semantic and structural support signals for a single query concept.
 */
struct ConceptEvidenceSupport {
    std::string concept_name;
    std::vector<SemanticEvidenceMatch> matches;
    float top_similarity{0.0f};
    float background_similarity{0.0f};
    float separation_margin{0.0f};
    float coherent_support_score{0.0f};
    bool has_lexical_support{false};
    bool has_structural_support{false};
    bool has_structural_corroboration{false};
    bool is_semantically_supported{false};
    std::size_t supporting_candidate_count{0};
    std::string corroborating_symbol;
    std::string corroborating_file_path;
    std::string explanation;

    [[nodiscard]] bool is_supported() const noexcept {
        return has_lexical_support || is_semantically_supported;
    }

    [[nodiscard]] bool operator==(const ConceptEvidenceSupport&) const = default;
};

/**
 * @brief Complete evidence support evaluation result across all query concepts.
 */
struct SemanticEvidenceResult {
    std::vector<ConceptEvidenceSupport> concepts;
    bool all_concepts_supported{false};
    std::size_t supported_concept_count{0};
    std::size_t total_concept_count{0};

    [[nodiscard]] const ConceptEvidenceSupport* find_concept(std::string_view name) const noexcept {
        for (const auto& c : concepts) {
            if (c.concept_name == name) {
                return &c;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool operator==(const SemanticEvidenceResult&) const = default;
};

/**
 * @brief Evaluates whether retrieved evidence semantically supports specific query Subject
 * concepts.
 *
 * Distinct from SemanticRetriever (which performs whole-query candidate discovery).
 * SemanticEvidenceSupport validates per-concept semantic grounding against candidate embeddings.
 */
class SemanticEvidenceSupport {
public:
    explicit SemanticEvidenceSupport(const semantic::EmbeddingProvider& provider,
                                     const semantic::SemanticIndex* semantic_index = nullptr);

    /**
     * @brief Evaluates semantic support for all Subject concepts in a query against an
     * EvidenceBundle.
     */
    [[nodiscard]] SemanticEvidenceResult
    evaluate(const retrieval::QueryRepresentation& query_rep, const EvidenceBundle& bundle,
             const SemanticEvidenceOptions& options = SemanticEvidenceOptions{}) const;

    /**
     * @brief Evaluates semantic support for a single query concept string against an
     * EvidenceBundle.
     */
    [[nodiscard]] ConceptEvidenceSupport
    evaluate_concept(std::string_view target_concept, const EvidenceBundle& bundle,
                     const SemanticEvidenceOptions& options = SemanticEvidenceOptions{}) const;

private:
    const semantic::EmbeddingProvider& provider_;
    const semantic::SemanticIndex* semantic_index_{nullptr};
};

}  // namespace amoeba::evidence
