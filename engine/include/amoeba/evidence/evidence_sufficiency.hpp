#pragma once

#include "amoeba/evidence/evidence_bundle.hpp"
#include "amoeba/retrieval/query_understanding.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::evidence {

/**
 * @brief Configuration thresholds for evidence sufficiency evaluation.
 */
struct EvidenceSufficiencyOptions {
    /// Minimum fraction of non-stopword query terms that must match at least one candidate.
    double min_term_coverage{0.34};

    /// Minimum hybrid score of top candidate.
    double min_top_score{0.20};

    /// Minimum raw/normalized semantic score required if match is purely semantic with zero lexical
    /// overlap.
    double min_semantic_only_score{0.50};

    /// If true, requires at least one content term to match symbol name, file path, supporting
    /// items, or excerpt.
    bool require_content_term_match{true};
};

/**
 * @brief Result of an evidence sufficiency check.
 */
struct EvidenceSufficiencyResult {
    /// Whether the evidence bundle contains sufficient grounded evidence to answer the question.
    bool is_sufficient{false};

    /// Evaluated confidence score in [0.0, 1.0].
    double confidence_score{0.0};

    /// Human-readable explanation of the sufficiency decision.
    std::string reason;

    /// Non-stopword query terms that were found in the retrieved evidence.
    std::vector<std::string> matched_terms;

    /// Non-stopword query terms that could not be found anywhere in the retrieved evidence.
    std::vector<std::string> missing_terms;

    /// Produces a clean, grounded refusal message when is_sufficient is false.
    [[nodiscard]] std::string format_grounded_refusal(std::string_view question) const;

    [[nodiscard]] bool operator==(const EvidenceSufficiencyResult&) const = default;
};

/**
 * @brief Deterministic gate that evaluates whether an EvidenceBundle contains sufficient
 * relevant evidence to justify LLM reasoning, preventing hallucinated answers on unsupported
 * queries.
 */
class EvidenceSufficiencyChecker {
public:
    /**
     * @brief Evaluates an EvidenceBundle against a user query using default or custom options.
     */
    [[nodiscard]] static EvidenceSufficiencyResult
    check(std::string_view query, const EvidenceBundle& bundle,
          const EvidenceSufficiencyOptions& options = EvidenceSufficiencyOptions{});

    /**
     * @brief Evaluates an EvidenceBundle against a pre-analyzed QueryRepresentation.
     */
    [[nodiscard]] static EvidenceSufficiencyResult
    check(const retrieval::QueryRepresentation& query_rep, const EvidenceBundle& bundle,
          const EvidenceSufficiencyOptions& options = EvidenceSufficiencyOptions{});
};

}  // namespace amoeba::evidence
