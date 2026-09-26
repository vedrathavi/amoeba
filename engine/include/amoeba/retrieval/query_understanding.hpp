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

enum class QueryTermRole : uint8_t {
    Subject,  ///< Core entity, identifier, or domain concept being queried (e.g. "RBAC", "JWT",
              ///< "calendar", "state", "note", "month")
    Action,   ///< Inquiry action or verb describing the operation asked (e.g. "implemented",
              ///< "defined", "managed", "saved", "rendered", "navigate")
    Context  ///< Interrogative, preposition, stopword, or grammatical context (e.g. "where", "how",
             ///< "between", "in", "is")
};

[[nodiscard]] constexpr std::string_view to_string(QueryTermRole role) noexcept {
    switch (role) {
    case QueryTermRole::Subject:
        return "Subject";
    case QueryTermRole::Action:
        return "Action";
    case QueryTermRole::Context:
        return "Context";
    }
    return "Unknown";
}

/**
 * @brief Categorized query term with normalized stem and semantic role.
 */
struct CategorizedQueryTerm {
    std::string raw_term;
    std::string normalized_stem;
    QueryTermRole role{QueryTermRole::Subject};

    [[nodiscard]] bool operator==(const CategorizedQueryTerm&) const = default;
};

/**
 * @brief Enriched structured representation of a user search query.
 */
struct QueryRepresentation {
    std::string raw_query;
    std::string normalized_query;        ///< Lowercase, trimmed
    std::string collapsed_query;         ///< Lowercase with spaces/underscores/hyphens stripped
    std::vector<std::string> raw_terms;  ///< Extracted word tokens
    std::vector<CategorizedQueryTerm> categorized_terms;  ///< Role-tagged terms
    std::vector<std::string> subject_terms;               ///< Normalized stems of Subject concepts
    std::vector<std::string> action_terms;  ///< Normalized stems of Action/inquiry verbs
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
     * @brief Conservatively normalizes English plural nouns and verb suffixes to their base stem.
     * Preserves technical terms (e.g. "auth", "authority", "authentication", "rbac").
     */
    [[nodiscard]] static std::string conservative_stem(std::string_view term);

    /**
     * @brief Checks if a term is a generic inquiry/action verb (e.g. "implemented", "managed",
     * "saved", "navigate").
     */
    [[nodiscard]] static bool is_action_term(std::string_view term) noexcept;

    /**
     * @brief Checks if a term is an interrogative (e.g. "where", "how", "what", "which").
     */
    [[nodiscard]] static bool is_interrogative(std::string_view term) noexcept;

    /**
     * @brief Classifies a single term into its semantic QueryTermRole.
     */
    [[nodiscard]] static QueryTermRole classify_term_role(std::string_view term) noexcept;

    /**
     * @brief Checks if a term is a common natural language stopword / interrogative.
     */
    [[nodiscard]] static bool is_stopword(std::string_view term) noexcept;
};

}  // namespace amoeba::retrieval
