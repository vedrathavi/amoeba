#include "amoeba/evidence/evidence_sufficiency.hpp"

#include "amoeba/index/code_tokenizer.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace amoeba::evidence {

namespace {

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

    const auto& terms_to_report = !missing_subjects.empty() ? missing_subjects : missing_terms;

    if (!terms_to_report.empty()) {
        ss << "The repository does not contain matching symbols, files, or references for: ";
        for (std::size_t i = 0; i < terms_to_report.size(); ++i) {
            if (i > 0) {
                ss << ", ";
            }
            ss << "\"" << terms_to_report[i] << "\"";
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

    // 2. Extract categorized terms
    std::vector<std::string> subject_terms = query_rep.subject_terms;
    std::vector<std::string> action_terms = query_rep.action_terms;

    // If query has no subject terms (pure inquiry/stopwords like "How is this implemented?")
    if (subject_terms.empty()) {
        // Fall back to all non-stopword content terms
        for (const auto& cat : query_rep.categorized_terms) {
            if (cat.role != retrieval::QueryTermRole::Context) {
                subject_terms.push_back(cat.normalized_stem);
            }
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

    // 3. Evaluate Subject concept matches across retrieved evidence
    for (const auto& subj : subject_terms) {
        bool subj_matched = false;

        for (const auto& item : bundle.items) {
            // Check primary symbol name
            if (contains_case_insensitive(item.primary_element().name, subj)) {
                subj_matched = true;
                break;
            }

            // Check file path
            if (contains_case_insensitive(item.file_path().string(), subj)) {
                subj_matched = true;
                break;
            }

            // Check supporting elements
            for (const auto& supp : item.supporting_elements()) {
                if (contains_case_insensitive(supp.name, subj)) {
                    subj_matched = true;
                    break;
                }
            }
            if (subj_matched) {
                break;
            }

            // Check source excerpt if present
            if (item.source_excerpt.has_value() &&
                contains_case_insensitive(item.source_excerpt->text, subj)) {
                subj_matched = true;
                break;
            }
        }

        if (subj_matched || has_exact_compound_match) {
            result.matched_subjects.push_back(subj);
            result.matched_terms.push_back(subj);
        } else {
            result.missing_subjects.push_back(subj);
            result.missing_terms.push_back(subj);
        }
    }

    // 4. Evaluate Action/inquiry verb matches
    for (const auto& act : action_terms) {
        bool act_matched = false;

        for (const auto& item : bundle.items) {
            if (contains_case_insensitive(item.primary_element().name, act) ||
                contains_case_insensitive(item.file_path().string(), act)) {
                act_matched = true;
                break;
            }
            for (const auto& supp : item.supporting_elements()) {
                if (contains_case_insensitive(supp.name, act)) {
                    act_matched = true;
                    break;
                }
            }
            if (act_matched) {
                break;
            }
            if (item.source_excerpt.has_value() &&
                contains_case_insensitive(item.source_excerpt->text, act)) {
                act_matched = true;
                break;
            }
        }

        if (act_matched) {
            result.matched_actions.push_back(act);
            result.matched_terms.push_back(act);
        } else {
            result.missing_actions.push_back(act);
        }
    }

    double subject_coverage = subject_terms.empty()
                                  ? 1.0
                                  : static_cast<double>(result.matched_subjects.size()) /
                                        static_cast<double>(subject_terms.size());

    if (has_exact_compound_match) {
        subject_coverage = 1.0;
    }

    const auto& top_item = bundle.items.front();
    const auto& top_result = top_item.primary_result;

    // 5. Decision logic

    // Rule A: For queries with identifiable Subject concepts, at least one Subject concept
    // MUST have grounded evidence in the code. Action verbs alone NEVER establish sufficiency.
    if (!subject_terms.empty() && result.matched_subjects.empty() &&
        options.require_subject_match) {
        result.is_sufficient = false;
        result.confidence_score = 0.0;
        result.reason =
            "None of the query subject concepts were found in the retrieved code candidates.";
        return result;
    }

    // Rule B: Semantic-only candidates cannot establish sufficiency without Subject evidence
    if (top_result.provenance == retrieval::RetrievalProvenance::SemanticOnly) {
        if (result.matched_subjects.empty()) {
            result.is_sufficient = false;
            result.confidence_score = top_result.normalized_semantic_score;
            result.reason = "Top candidate was retrieved purely through semantic similarity with "
                            "no subject keyword evidence.";
            return result;
        }
        if (top_result.normalized_semantic_score < options.min_semantic_only_score &&
            subject_coverage < 0.50) {
            result.is_sufficient = false;
            result.confidence_score = top_result.normalized_semantic_score;
            result.reason = "Top candidate is semantic-only with low confidence and insufficient "
                            "subject concept coverage.";
            return result;
        }
    }

    // Rule C: Subject concept coverage ratio threshold
    if (!subject_terms.empty() && subject_coverage < options.min_term_coverage) {
        result.is_sufficient = false;
        result.confidence_score = subject_coverage;
        result.reason = "Query subject concept coverage is below the required threshold.";
        return result;
    }

    // Rule D: Top candidate retrieval score below minimum floor
    if (top_result.hybrid_score < options.min_top_score && result.matched_subjects.empty()) {
        result.is_sufficient = false;
        result.confidence_score = top_result.hybrid_score;
        result.reason = "Top candidate retrieval score is below minimum threshold.";
        return result;
    }

    // 6. Sufficient grounded evidence confirmed
    result.is_sufficient = true;
    result.confidence_score =
        std::clamp(0.6 * subject_coverage + 0.4 * top_result.hybrid_score, 0.0, 1.0);
    result.reason = "Sufficient grounded evidence found in repository candidates.";
    return result;
}

}  // namespace amoeba::evidence
