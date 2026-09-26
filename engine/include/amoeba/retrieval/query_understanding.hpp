#pragma once

// Phase 6.6 — Query Understanding & Retrieval Refinement
//
// This header defines query understanding, normalization, compound identifier
// synthesis, and deterministic intent classification for Amoeba retrieval.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::retrieval {

/**
 * @brief High-level query intent category determined by deterministic lexical inspection.
 */
enum class QueryIntent : uint8_t {
    IdentifierOrTechnical,  ///< Query contains code identifiers, camelCase/PascalCase tokens, code
                            ///< symbols, or file terms
    NaturalLanguage,        ///< Conversational question or conceptual description with multiple
                            ///< stopwords/interrogatives
    GeneralSearch           ///< Short generic or multi-term query without clear interrogatives or
                            ///< identifiers
};

[[nodiscard]] constexpr std::string_view to_string(QueryIntent intent) noexcept {
    switch (intent) {
    case QueryIntent::IdentifierOrTechnical:
        return "IdentifierOrTechnical";
    case QueryIntent::NaturalLanguage:
        return "NaturalLanguage";
    case QueryIntent::GeneralSearch:
        return "GeneralSearch";
    }
    return "Unknown";
}

/**
 * @brief Enriched structured representation of a user search query.
 */
struct QueryRepresentation {
    std::string raw_query;
    std::string normalized_query;        ///< Lowercase, trimmed
    std::string collapsed_query;         ///< Lowercase with spaces/underscores/hyphens stripped
    std::vector<std::string> raw_terms;  ///< Extracted word tokens
    std::vector<std::string>
        synthesized_identifiers;  ///< Reconstructed compound identifier tokens (e.g. "usecalendar")
    std::vector<std::string>
        all_search_terms;  ///< Combined deduplicated search tokens for index lookup
    QueryIntent intent{QueryIntent::GeneralSearch};
    bool has_code_syntax{false};
    bool has_interrogative{false};
    double recommended_alpha{0.5};  ///< Intent-guided lexical/semantic weighting
};

/**
 * @brief Utility for query normalization, compound identifier reconstruction, and deterministic
 * intent analysis.
 */
class QueryUnderstanding {
public:
    /**
     * @brief Normalizes raw query text (collapses consecutive whitespace, trims punctuation).
     */
    [[nodiscard]] static std::string normalize_text(std::string_view text);

    /**
     * @brief Collapses a query by stripping all whitespace and word delimiters.
     * Example: "use calendar" -> "usecalendar", "Calendar Grid" -> "calendargrid".
     */
    [[nodiscard]] static std::string collapse_identifier(std::string_view text);

    /**
     * @brief Reconstructs synthesized compound identifier forms from adjacent terms.
     * Example: "use calendar" -> {"usecalendar", "useCalendar", "UseCalendar", "use_calendar"}
     */
    [[nodiscard]] static std::vector<std::string>
    synthesize_compounds(const std::vector<std::string>& terms);

    /**
     * @brief Performs complete query analysis and returns a QueryRepresentation.
     */
    [[nodiscard]] static QueryRepresentation analyze(std::string_view raw_query);

    /**
     * @brief Checks if a term is a common natural language stopword / interrogative.
     */
    [[nodiscard]] static bool is_stopword(std::string_view term) noexcept;
};

}  // namespace amoeba::retrieval
