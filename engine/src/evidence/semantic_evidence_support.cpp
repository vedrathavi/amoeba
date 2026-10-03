#include "amoeba/evidence/semantic_evidence_support.hpp"

#include "amoeba/index/code_tokenizer.hpp"
#include "amoeba/semantic/semantic_document.hpp"
#include "amoeba/semantic/similarity.hpp"

#include <algorithm>
#include <cctype>

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

SemanticEvidenceSupport::SemanticEvidenceSupport(const semantic::EmbeddingProvider& provider,
                                                 const semantic::ISemanticVectorIndex* semantic_index)
    : provider_(provider), semantic_index_(semantic_index) {}

ConceptEvidenceSupport
SemanticEvidenceSupport::evaluate_concept(std::string_view target_concept,
                                          const EvidenceBundle& bundle,
                                          const SemanticEvidenceOptions& options) const {
    ConceptEvidenceSupport result;
    result.concept_name = std::string(target_concept);

    if (target_concept.empty() || bundle.empty()) {
        return result;
    }

    // 1. Embed query concept vector
    const auto concept_embedding = provider_.embed(target_concept);
    if (concept_embedding.empty()) {
        return result;
    }

    // 2. Evaluate all evidence candidates
    std::vector<SemanticEvidenceMatch> matches;
    matches.reserve(bundle.items.size());

    bool has_lexical = false;
    bool has_structural = false;
    std::size_t strong_candidate_count = 0;

    for (const auto& item : bundle.items) {
        SemanticEvidenceMatch match;
        match.query_concept = std::string(target_concept);
        match.element_id = item.primary_element_id();
        match.element_name = item.primary_element().name;
        match.file_path = item.file_path().generic_string();

        // Check lexical presence
        if (contains_case_insensitive(item.primary_element().name, target_concept) ||
            contains_case_insensitive(item.primary_element().detail, target_concept) ||
            contains_case_insensitive(item.file_path().generic_string(), target_concept) ||
            (item.source_excerpt.has_value() &&
             contains_case_insensitive(item.source_excerpt->text, target_concept))) {
            match.is_lexical_match = true;
            has_lexical = true;
        }

        for (const auto& supp : item.supporting_elements()) {
            if (contains_case_insensitive(supp.name, target_concept)) {
                match.is_lexical_match = true;
                has_lexical = true;
                break;
            }
        }

        // Check direct structural relationship presence
        for (const auto& rel : item.direct_relationships) {
            if (contains_case_insensitive(rel.related_name, target_concept) ||
                contains_case_insensitive(rel.related_file_path.generic_string(), target_concept) ||
                contains_case_insensitive(rel.related_detail, target_concept)) {
                match.is_structural_match = true;
                has_structural = true;
                break;
            }
        }

        // Retrieve or compute candidate embedding
        const semantic::Embedding* candidate_emb = nullptr;
        std::vector<float> computed_emb;

        if (semantic_index_ != nullptr) {
            candidate_emb = semantic_index_->lookup(item.primary_element_id());
        }

        if (candidate_emb != nullptr) {
            match.similarity =
                semantic::cosine_similarity(concept_embedding, candidate_emb->values);
        } else {
            const std::string unit_text =
                semantic::SemanticTextFormatter::format_unit(item.primary_result.unit);
            computed_emb = provider_.embed(unit_text);
            match.similarity = semantic::cosine_similarity(concept_embedding, computed_emb);
        }

        if (match.similarity >= options.min_semantic_support_similarity || match.is_lexical_match) {
            strong_candidate_count++;
        }

        matches.push_back(std::move(match));
    }

    // Sort matches descending by similarity
    std::sort(matches.begin(), matches.end(),
              [](const auto& a, const auto& b) { return a.similarity > b.similarity; });

    // Check for structural corroboration across the bundle
    bool has_structural_corroboration = false;
    std::string top_name;
    std::string top_file;

    if (!matches.empty()) {
        top_name = matches.front().element_name;
        top_file = matches.front().file_path;
        const auto top_id = matches.front().element_id;

        // Check if top candidate is connected in the call/reference graph to another relevant candidate
        for (const auto& item : bundle.items) {
            if (item.primary_element_id() == top_id) {
                continue;
            }
            for (const auto& rel : item.direct_relationships) {
                if (rel.related_element_id == top_id || rel.primary_element_id == top_id) {
                    // Check if the related node is non-trivial (has similarity >= noise floor or lexical match)
                    for (const auto& m : matches) {
                        if (m.element_id == item.primary_element_id() &&
                            (m.similarity >= options.min_candidate_similarity || m.is_lexical_match)) {
                            has_structural_corroboration = true;
                            break;
                        }
                    }
                }
                if (has_structural_corroboration) {
                    break;
                }
            }
            if (has_structural_corroboration) {
                break;
            }
        }
    }

    result.matches = matches;
    result.has_lexical_support = has_lexical;
    result.has_structural_support = has_structural;
    result.has_structural_corroboration = has_structural_corroboration;
    result.supporting_candidate_count = strong_candidate_count;
    result.corroborating_symbol = top_name;
    result.corroborating_file_path = top_file;

    if (matches.empty()) {
        result.top_similarity = 0.0f;
        result.background_similarity = 0.0f;
        result.separation_margin = 0.0f;
        result.coherent_support_score = 0.0f;
        result.is_semantically_supported = false;
        result.explanation = "No candidate items met minimum similarity threshold.";
    } else {
        const float s_top = matches.front().similarity;
        result.top_similarity = s_top;

        // Compute representative background similarity from remaining candidates
        float bg_sum = 0.0f;
        std::size_t bg_count = 0;
        for (std::size_t i = 1; i < matches.size(); ++i) {
            bg_sum += matches[i].similarity;
            bg_count++;
        }

        const float s_bg = (bg_count > 0)
                               ? (bg_sum / static_cast<float>(bg_count))
                               : options.min_candidate_similarity;
        const float margin = s_top - s_bg;

        result.background_similarity = s_bg;
        result.separation_margin = margin;

        if (has_lexical) {
            result.coherent_support_score = 1.0f;
            result.is_semantically_supported = true;
            result.explanation = "Lexically grounded in repository code/symbols.";
        } else {
            // Semantic Evidence Decision:
            // Top candidate must exceed minimum similarity floor AND exhibit distinct relative separation.
            // Structural corroboration provides verified graph edges but cannot bypass separation.
            const bool meets_similarity = (s_top >= options.min_semantic_support_similarity);
            const bool meets_separation = (margin >= options.min_separation_margin);

            if (meets_similarity && meets_separation) {
                result.is_semantically_supported = true;
                result.coherent_support_score = s_top;
                result.explanation = "Semantically supported by candidate '" + top_name +
                                     "' (similarity: " + std::to_string(s_top) +
                                     ", margin: " + std::to_string(margin) +
                                     (has_structural_corroboration ? ", graph-corroborated" : "") +
                                     ")";
            } else {
                result.is_semantically_supported = false;
                result.coherent_support_score = 0.0f;
                result.explanation = "Insufficient semantic separation (similarity: " +
                                     std::to_string(s_top) +
                                     ", margin: " + std::to_string(margin) + ")";
            }
        }
    }

    return result;
}

SemanticEvidenceResult
SemanticEvidenceSupport::evaluate(const retrieval::QueryRepresentation& query_rep,
                                  const EvidenceBundle& bundle,
                                  const SemanticEvidenceOptions& options) const {
    SemanticEvidenceResult result;

    std::vector<std::string> concepts_to_check = query_rep.subject_terms;
    if (concepts_to_check.empty()) {
        for (const auto& cat : query_rep.categorized_terms) {
            if (cat.role != retrieval::QueryTermRole::Context) {
                concepts_to_check.push_back(cat.normalized_stem);
            }
        }
    }

    result.total_concept_count = concepts_to_check.size();
    result.supported_concept_count = 0;

    for (const auto& concept_str : concepts_to_check) {
        auto support = evaluate_concept(concept_str, bundle, options);
        if (support.is_supported()) {
            result.supported_concept_count++;
        }
        result.concepts.push_back(std::move(support));
    }

    result.all_concepts_supported = (result.total_concept_count > 0 &&
                                     result.supported_concept_count == result.total_concept_count);

    return result;
}

}  // namespace amoeba::evidence
