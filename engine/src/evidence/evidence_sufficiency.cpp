#include "amoeba/evidence/evidence_sufficiency.hpp"

#include "amoeba/evidence/semantic_evidence_support.hpp"
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
    if (needle.size() <= 2) {
        // For short terms (<= 2 chars like "oa"), require word/identifier token matching
        // to prevent false positives inside words like "floating" or "toolbar"
        const auto tokens = index::CodeTokenizer::tokenize_query(haystack);
        for (const auto& tok : tokens) {
            if (tok == needle) {
                return true;
            }
        }
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
                                  const EvidenceSufficiencyOptions& options,
                                  const SemanticEvidenceResult* semantic_evidence) {
    const auto query_rep = retrieval::QueryUnderstanding::analyze(query);
    return check(query_rep, bundle, options, semantic_evidence);
}

EvidenceSufficiencyResult EvidenceSufficiencyChecker::check(
    const retrieval::QueryRepresentation& query_rep, const EvidenceBundle& bundle,
    const EvidenceSufficiencyOptions& options, const SemanticEvidenceResult* semantic_evidence) {
    EvidenceSufficiencyResult result;

    // 1. Check for empty evidence
    if (bundle.empty() || bundle.items.empty()) {
        result.is_sufficient = false;
        result.confidence_score = 0.0;
        result.reason = "No matching code elements were found in the repository.";
        return result;
    }

    // 2. Extract categorized terms
    std::vector<std::string> subject_terms;
    if (!query_rep.distinguishing_subject_terms.empty()) {
        subject_terms = query_rep.distinguishing_subject_terms;
        for (const auto& gen : query_rep.generic_component_terms) {
            if (std::find(subject_terms.begin(), subject_terms.end(), gen) == subject_terms.end()) {
                subject_terms.push_back(gen);
            }
        }
    } else if (!query_rep.subject_terms.empty()) {
        subject_terms = query_rep.subject_terms;
    } else {
        for (const auto& cat : query_rep.categorized_terms) {
            if (cat.role != retrieval::QueryTermRole::Context &&
                cat.role != retrieval::QueryTermRole::ConversationalFraming) {
                subject_terms.push_back(cat.normalized_stem);
            }
        }
    }

    std::vector<std::string> action_terms = query_rep.action_terms;

    // 3. Evaluate Subject concept matches across retrieved evidence (tracking primary vs expanded)
    std::vector<std::string> primary_matched_subjects;
    std::vector<std::string> expanded_only_matched_subjects;

    for (const auto& subj : subject_terms) {
        bool matched_in_primary = false;
        bool matched_in_expanded = false;

        for (const auto& item : bundle.items) {
            bool item_has_subj = false;

            // Check primary symbol name
            if (contains_case_insensitive(item.primary_element().name, subj)) {
                item_has_subj = true;
            }

            // Check file path
            if (!item_has_subj && contains_case_insensitive(item.file_path().string(), subj)) {
                item_has_subj = true;
            }

            // Check element detail
            if (!item_has_subj && contains_case_insensitive(item.primary_element().detail, subj)) {
                item_has_subj = true;
            }

            // Check supporting elements
            if (!item_has_subj) {
                for (const auto& supp : item.supporting_elements()) {
                    if (contains_case_insensitive(supp.name, subj)) {
                        item_has_subj = true;
                        break;
                    }
                }
            }

            // Check source excerpt if present
            if (!item_has_subj && item.source_excerpt.has_value() &&
                contains_case_insensitive(item.source_excerpt->text, subj)) {
                item_has_subj = true;
            }

            if (item_has_subj) {
                if (!item.is_expanded_relationship) {
                    matched_in_primary = true;
                    break;
                } else {
                    matched_in_expanded = true;
                }
            }
        }

        // Check for exact compound identifier match for this subject (e.g. "usecalendar" -> useCalendar)
        if (!matched_in_primary) {
            for (const auto& compound : query_rep.synthesized_identifiers) {
                if (compound.size() < 3 || !contains_case_insensitive(compound, subj)) {
                    continue;
                }
                for (const auto& item : bundle.items) {
                    if (contains_case_insensitive(item.primary_element().name, compound) ||
                        contains_case_insensitive(item.file_path().string(), compound)) {
                        if (!item.is_expanded_relationship) {
                            matched_in_primary = true;
                        } else {
                            matched_in_expanded = true;
                        }
                        break;
                    }
                }
                if (matched_in_primary) {
                    break;
                }
            }
        }

        // If lexical match didn't find the subject concept, check semantic evidence support
        if (!matched_in_primary && !matched_in_expanded && semantic_evidence != nullptr) {
            const auto* concept_sup = semantic_evidence->find_concept(subj);
            if (concept_sup != nullptr && concept_sup->is_supported()) {
                matched_in_primary = true;
            }
        }

        if (matched_in_primary) {
            primary_matched_subjects.push_back(subj);
            result.matched_subjects.push_back(subj);
            result.matched_terms.push_back(subj);
        } else if (matched_in_expanded) {
            expanded_only_matched_subjects.push_back(subj);
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

    // Rule A.1 (Phase 8.2.9.1 Safety Invariant: Primary Subject Grounding):
    // Relationship expansion is strictly SUPPORTING evidence. It may enrich an already grounded
    // primary subject (e.g. providing helper methods, subclasses, or annotations), but it MUST NOT
    // independently manufacture subject grounding when ZERO primary candidates matched any query subject terms.
    if (!subject_terms.empty() && primary_matched_subjects.empty() && options.require_subject_match) {
        result.is_sufficient = false;
        result.confidence_score = 0.0;
        result.reason =
            "None of the query subject concepts were grounded in primary retrieval candidates; "
            "supporting relationship expansion cannot independently manufacture subject sufficiency.";
        return result;
    }

    // Rule B: Semantic-only candidates cannot establish sufficiency without Subject evidence
    if (top_result.provenance == retrieval::RetrievalProvenance::SemanticOnly) {
        if (result.matched_subjects.empty()) {
            result.is_sufficient = false;
            result.confidence_score = 0.0;
            result.reason = "Top candidate was retrieved purely through semantic similarity with "
                            "no subject keyword evidence.";
            return result;
        }
        if (top_result.normalized_semantic_score < options.min_semantic_only_score &&
            subject_coverage <= 0.50) {
            result.is_sufficient = false;
            result.confidence_score = 0.0;
            result.reason = "Top candidate is semantic-only with low confidence and insufficient "
                            "subject concept coverage.";
            return result;
        }
    }

    // Rule C: Primary Subject concept coverage ratio threshold
    bool has_strong_primary_match = false;
    for (const auto& item : bundle.items) {
        if (item.is_expanded_relationship) continue; // Strong primary anchors must originate from primary retrieval
        for (const auto& te : query_rep.technical_entities) {
            if (contains_case_insensitive(item.primary_element().name, te) ||
                contains_case_insensitive(item.file_path().string(), te)) {
                has_strong_primary_match = true;
                break;
            }
        }
        if (has_strong_primary_match) {
            break;
        }
    }

    double primary_subject_coverage =
        subject_terms.empty()
            ? 1.0
            : static_cast<double>(primary_matched_subjects.size()) /
                  static_cast<double>(subject_terms.size());

    const double effective_min_coverage = (has_strong_primary_match && !primary_matched_subjects.empty())
                                              ? std::min(0.50, options.min_term_coverage)
                                              : options.min_term_coverage;

    if (!subject_terms.empty() && primary_subject_coverage < effective_min_coverage) {
        result.is_sufficient = false;
        result.confidence_score = 0.0;
        result.reason = "Query subject concept coverage is below the required threshold.";
        return result;
    }

    // Rule D: Top candidate retrieval score below minimum floor
    if (top_result.hybrid_score < options.min_top_score && result.matched_subjects.empty()) {
        result.is_sufficient = false;
        result.confidence_score = 0.0;
        result.reason = "Top candidate retrieval score is below minimum threshold.";
        return result;
    }

    // Rule E: Evidence Identity Rule (Phase 7.6.1 + Phase 8.2.9.1 Safety Hardening)
    // If the query contains at least one distinguishing domain subject term
    // (e.g. "GraphQL" in "GraphQL resolver", "JWT" in "JWT authentication", "Qdrant" in "Qdrant indexing"),
    // at least one distinguishing domain subject term MUST be grounded in PRIMARY retrieval candidates.
    if (!query_rep.distinguishing_subject_terms.empty()) {
        bool has_primary_distinguishing_match = false;
        for (const auto& dist_subj : query_rep.distinguishing_subject_terms) {
            if (std::find(primary_matched_subjects.begin(), primary_matched_subjects.end(), dist_subj) !=
                primary_matched_subjects.end()) {
                has_primary_distinguishing_match = true;
                break;
            }
        }

        if (!has_primary_distinguishing_match) {
            result.is_sufficient = false;
            result.confidence_score = 0.0;
            result.reason =
                "Only generic component terms were matched in primary evidence; required distinguishing domain concept(s) "
                "were not found in primary repository candidates.";
            return result;
        }
    }

    // 6. Sufficient grounded evidence confirmed
    result.is_sufficient = true;
    result.confidence_score =
        std::clamp(0.6 * subject_coverage + 0.4 * top_result.hybrid_score, 0.0, 1.0);
    result.reason = "Sufficient grounded evidence found in repository candidates.";
    return result;
}

}  // namespace amoeba::evidence
