#include "amoeba/evidence/evidence_sufficiency.hpp"

#include "amoeba/index/code_tokenizer.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace amoeba::evidence {

namespace {

[[nodiscard]] std::string to_lower_str(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (char c : text) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

[[nodiscard]] bool contains_case_insensitive(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    if (haystack.size() < needle.size()) {
        return false;
    }
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                          [](char ch1, char ch2) {
                              return std::tolower(static_cast<unsigned char>(ch1)) ==
                                     std::tolower(static_cast<unsigned char>(ch2));
                          });
    return it != haystack.end();
}

}  // namespace

std::string EvidenceSufficiencyResult::format_grounded_refusal(std::string_view question) const {
    std::ostringstream ss;
    ss << "I couldn't find sufficient evidence in the repository to answer: \"" << question
       << "\".\n\n";

    if (!missing_terms.empty()) {
        ss << "The repository does not contain matching symbols, files, or references for: ";
        for (std::size_t i = 0; i < missing_terms.size(); ++i) {
            if (i > 0) {
                ss << ", ";
            }
            ss << "\"" << missing_terms[i] << "\"";
        }
        ss << ".\n";
    } else if (!reason.empty()) {
        ss << reason << "\n";
    }
    return ss.str();
}

EvidenceSufficiencyResult
EvidenceSufficiencyChecker::check(std::string_view query, const EvidenceBundle& bundle,
                                  const EvidenceSufficiencyOptions& options) {
    const auto query_rep = retrieval::QueryUnderstanding::analyze(query);
    return check(query_rep, bundle, options);
}

EvidenceSufficiencyResult
EvidenceSufficiencyChecker::check(const retrieval::QueryRepresentation& query_rep,
                                  const EvidenceBundle& bundle,
                                  const EvidenceSufficiencyOptions& options) {
    EvidenceSufficiencyResult result;

    // 1. Check for empty evidence
    if (bundle.empty() || bundle.items.empty()) {
        result.is_sufficient = false;
        result.confidence_score = 0.0;
        result.reason = "No matching code elements were found in the repository.";
        return result;
    }

    // 2. Extract content terms (filtering out stopwords, punctuation & single-character tokens)
    std::vector<std::string> content_terms;
    std::unordered_set<std::string> seen_terms;

    for (const auto& term : query_rep.raw_terms) {
        std::string cleaned = to_lower_str(term);
        while (!cleaned.empty() &&
               (cleaned.back() == '?' || cleaned.back() == '!' || cleaned.back() == '.' ||
                cleaned.back() == ',' || cleaned.back() == ':' || cleaned.back() == ';')) {
            cleaned.pop_back();
        }
        if (cleaned.size() >= 2 && !retrieval::QueryUnderstanding::is_stopword(cleaned)) {
            if (seen_terms.insert(cleaned).second) {
                content_terms.push_back(cleaned);
            }
        }
    }

    // If query has no content terms (pure stopwords or punctuation)
    if (content_terms.empty()) {
        const auto& top_res = bundle.items.front().primary_result;
        if (top_res.hybrid_score >= options.min_top_score) {
            result.is_sufficient = true;
            result.confidence_score = top_res.hybrid_score;
            result.reason = "Retrieved candidate satisfies minimum score threshold.";
            return result;
        }
        result.is_sufficient = false;
        result.confidence_score = 0.0;
        result.reason =
            "Query contains no specific technical keywords and candidate scores are low.";
        return result;
    }

    // 3. Evaluate content term matches across all retrieved items
    for (const auto& term : content_terms) {
        bool term_matched = false;

        for (const auto& item : bundle.items) {
            // Check primary symbol name
            if (contains_case_insensitive(item.primary_element().name, term)) {
                term_matched = true;
                break;
            }

            // Check file path
            if (contains_case_insensitive(item.file_path().string(), term)) {
                term_matched = true;
                break;
            }

            // Check supporting elements
            for (const auto& supp : item.supporting_elements()) {
                if (contains_case_insensitive(supp.name, term)) {
                    term_matched = true;
                    break;
                }
            }
            if (term_matched) {
                break;
            }

            // Check source excerpt if present
            if (item.source_excerpt.has_value() &&
                contains_case_insensitive(item.source_excerpt->text, term)) {
                term_matched = true;
                break;
            }
        }

        if (term_matched) {
            result.matched_terms.push_back(term);
        } else {
            result.missing_terms.push_back(term);
        }
    }

    // Check for exact compound identifier match (e.g. "usecalendar" -> useCalendar)
    bool has_exact_compound_match = false;
    for (const auto& compound : query_rep.synthesized_identifiers) {
        if (compound.size() < 3) {
            continue;
        }
        for (const auto& item : bundle.items) {
            if (contains_case_insensitive(item.primary_element().name, compound) ||
                contains_case_insensitive(item.file_path().string(), compound)) {
                has_exact_compound_match = true;
                break;
            }
        }
        if (has_exact_compound_match) {
            break;
        }
    }

    double coverage_ratio = content_terms.empty()
                                ? 0.0
                                : static_cast<double>(result.matched_terms.size()) /
                                      static_cast<double>(content_terms.size());

    if (has_exact_compound_match) {
        coverage_ratio = std::max(coverage_ratio, 1.0);
    }

    const auto& top_item = bundle.items.front();
    const auto& top_result = top_item.primary_result;

    // 4. Decision logic
    // Case A: Zero content terms matched
    if (result.matched_terms.empty() && options.require_content_term_match) {
        result.is_sufficient = false;
        result.confidence_score = 0.0;
        result.reason = "None of the query keywords were found in the retrieved code candidates.";
        return result;
    }

    // Case B: Semantic-only match with low semantic similarity
    if (top_result.provenance == retrieval::RetrievalProvenance::SemanticOnly) {
        if (top_result.normalized_semantic_score < options.min_semantic_only_score &&
            coverage_ratio < 0.50) {
            result.is_sufficient = false;
            result.confidence_score = top_result.normalized_semantic_score;
            result.reason = "Top candidate is semantic-only with low confidence and insufficient "
                            "keyword coverage.";
            return result;
        }
    }

    // Case C: Insufficient keyword coverage ratio
    if (coverage_ratio < options.min_term_coverage) {
        result.is_sufficient = false;
        result.confidence_score = coverage_ratio;
        result.reason = "Query keyword coverage is below the required threshold.";
        return result;
    }

    // Case D: Top candidate score below threshold
    if (top_result.hybrid_score < options.min_top_score) {
        result.is_sufficient = false;
        result.confidence_score = top_result.hybrid_score;
        result.reason = "Top candidate retrieval score is below minimum threshold.";
        return result;
    }

    // 5. Sufficient evidence confirmed
    result.is_sufficient = true;
    result.confidence_score =
        std::clamp(0.5 * coverage_ratio + 0.5 * top_result.hybrid_score, 0.0, 1.0);
    result.reason = "Sufficient grounded evidence found in repository candidates.";
    return result;
}

}  // namespace amoeba::evidence
