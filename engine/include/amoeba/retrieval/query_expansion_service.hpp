#pragma once

#include "amoeba/reasoning/llm_runtime.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/retrieval/primary_search_result.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::retrieval {

/**
 * @brief Configuration options for context-aware query expansion.
 */
struct QueryExpansionOptions {
    bool enabled{false};                  ///< Whether query expansion is active (default: false)
    double rrf_k{60.0};                   ///< Reciprocal Rank Fusion smoothing constant k
    double original_weight{1.0};         ///< RRF weight for original query rank
    double expanded_weight{0.5};         ///< RRF weight for expanded query rank
    std::size_t context_max_symbols{5};  ///< Maximum top original candidates used as context
    std::size_t max_query_length{300};   ///< Maximum allowed character length for expanded query
    std::size_t max_tokens{128};         ///< Max generation tokens for LLM expansion
    double temperature{0.0};             ///< Greedy deterministic generation
};

/**
 * @brief Structured representation of query expansion outcome.
 */
struct QueryExpansionResult {
    std::string original_query;
    std::string expanded_query;
    std::vector<std::string> search_terms;
    std::vector<std::string> assumptions;
    bool expansion_applied{false};
    std::string status_message;
};

/**
 * @brief Coordinates context-aware LLM query expansion and dual-path retrieval merging.
 *
 * Guarantees:
 *  - The original query is strictly immutable and searched independently.
 *  - Original retrieval executes exactly once.
 *  - Repository code and context are marked as untrusted data to guard against prompt injection.
 *  - Merges dual retrieval paths using Reciprocal Rank Fusion with original-query priority.
 *  - Fails safe to original-query results if LLM is unavailable, times out, or produces invalid output.
 */
class QueryExpansionService {
public:
    explicit QueryExpansionService(reasoning::LLMRuntime* runtime = nullptr);
    ~QueryExpansionService() = default;

    /**
     * @brief Constructs the prompt dispatched to the LLM for query expansion.
     * Treats repository context as untrusted data.
     */
    [[nodiscard]] static std::string build_expansion_prompt(
        std::string_view original_query,
        std::span<const PrimarySearchResult> initial_candidates,
        std::size_t max_context_symbols);

    /**
     * @brief Parses and validates the raw LLM output into structured expansion fields.
     */
    [[nodiscard]] static bool parse_and_validate_expansion(
        std::string_view original_query,
        std::string_view raw_response,
        const QueryExpansionOptions& options,
        QueryExpansionResult& out_result);

    /**
     * @brief Merges original (Search A) and expanded (Search B) results using Reciprocal Rank Fusion.
     */
    [[nodiscard]] static std::vector<PrimarySearchResult> merge_with_rrf(
        std::span<const PrimarySearchResult> original_results,
        std::span<const PrimarySearchResult> expanded_results,
        const QueryExpansionOptions& options);

    /**
     * @brief Executes dual-path retrieval with query expansion over the primary pipeline.
     */
    [[nodiscard]] std::vector<PrimarySearchResult> execute_dual_retrieval(
        PrimaryRetrievalPipeline& pipeline,
        std::string_view original_query,
        const PrimarySearchOptions& search_options,
        const QueryExpansionOptions& expansion_options,
        QueryExpansionResult* out_expansion_result = nullptr);

    /**
     * @brief Updates the active LLM runtime backend.
     */
    void set_runtime(reasoning::LLMRuntime* runtime) noexcept { runtime_ = runtime; }

private:
    reasoning::LLMRuntime* runtime_{nullptr};
};

}  // namespace amoeba::retrieval

