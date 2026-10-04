#include "amoeba/investigation/bounded_investigation_service.hpp"

#include "amoeba/evidence/evidence_assembler.hpp"
#include "amoeba/index/code_tokenizer.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_set>

namespace amoeba::investigation {

using Clock = std::chrono::steady_clock;

BoundedInvestigationService::BoundedInvestigationService(
    const index::InvertedIndex& index,
    const graph::RelationshipGraph& graph,
    const retrieval::PrimaryRetrievalPipeline& pipeline,
    const source::SourceSnippetReader& snippet_reader)
    : index_(index), graph_(graph), pipeline_(pipeline), snippet_reader_(snippet_reader) {
    build_unit_lookup();
}

void BoundedInvestigationService::build_unit_lookup() {
    const auto& units = pipeline_.units();
    element_to_unit_map_.reserve(units.size() * 4);

    for (std::size_t i = 0; i < units.size(); ++i) {
        const auto& unit = units[i];
        if (unit.primary_element_id != 0) {
            element_to_unit_map_[unit.primary_element_id] = i;
        }
    }
}

const retrieval::RetrievalUnit*
BoundedInvestigationService::find_unit_for_element(index::ElementId id) const {
    const auto it = element_to_unit_map_.find(id);
    if (it != element_to_unit_map_.end()) {
        const auto& units = pipeline_.units();
        if (it->second < units.size()) {
            return &units[it->second];
        }
    }
    return nullptr;
}

const retrieval::RetrievalUnit*
BoundedInvestigationService::find_unit_by_symbol_name(std::string_view name) const {
    if (name.empty()) {
        return nullptr;
    }

    const auto& units = pipeline_.units();
    for (const auto& unit : units) {
        if (unit.primary_element.name == name) {
            return &unit;
        }
    }
    return nullptr;
}

std::vector<InvestigatedCandidate>
BoundedInvestigationService::investigate_candidates(
    std::string_view query,
    std::span<const retrieval::PrimarySearchResult> hop0_candidates,
    const InvestigationOptions& options,
    InvestigationMetrics* metrics) const {
    const auto query_rep = retrieval::QueryUnderstanding::analyze(query);
    if (metrics) {
        return investigate_candidates_with_metrics(query_rep, hop0_candidates, options, *metrics);
    }
    InvestigationMetrics dummy_metrics;
    return investigate_candidates_with_metrics(query_rep, hop0_candidates, options, dummy_metrics);
}

namespace {

[[nodiscard]] bool contains_ci(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                          [](char ch1, char ch2) {
                              return std::tolower(static_cast<unsigned char>(ch1)) ==
                                     std::tolower(static_cast<unsigned char>(ch2));
                          });
    return it != haystack.end();
}

struct SeedCandidateScore {
    std::size_t index{0};
    const retrieval::PrimarySearchResult* candidate{nullptr};
    double suitability_score{0.0};
};

} // namespace

std::vector<InvestigatedCandidate>
BoundedInvestigationService::investigate_candidates_with_metrics(
    const retrieval::QueryRepresentation& query_rep,
    std::span<const retrieval::PrimarySearchResult> hop0_candidates,
    const InvestigationOptions& options,
    InvestigationMetrics& metrics) const {

    const auto start_time = Clock::now();
    metrics.hop0_candidate_count = hop0_candidates.size();

    // 1. Perform deterministic seed scoring across the candidate reservoir (Top-50)
    const auto seed_sel_start = Clock::now();
    std::vector<SeedCandidateScore> seed_scores;
    seed_scores.reserve(hop0_candidates.size());

    for (std::size_t i = 0; i < hop0_candidates.size(); ++i) {
        const auto& cand = hop0_candidates[i];
        if (cand.hybrid_score < options.min_seed_relevance) {
            continue;
        }

        double score = cand.hybrid_score;

        // Signal 1: Technical entity overlap boost (+0.30)
        bool has_te_match = false;
        for (const auto& te : query_rep.technical_entities) {
            if (contains_ci(cand.unit.primary_element.name, te) ||
                contains_ci(cand.unit.file_path.generic_string(), te)) {
                has_te_match = true;
                break;
            }
        }
        if (has_te_match) {
            score += 0.30;
        }

        // Signal 2: Distinguishing and core subject term grounding overlap (+0.15 per term, capped at +0.45)
        std::size_t matched_subjects = 0;
        for (const auto& subj : query_rep.distinguishing_subject_terms) {
            if (contains_ci(cand.unit.primary_element.name, subj) ||
                contains_ci(cand.unit.file_path.filename().string(), subj)) {
                matched_subjects++;
            }
        }
        for (const auto& subj : query_rep.core_subject_terms) {
            if (contains_ci(cand.unit.primary_element.name, subj) ||
                contains_ci(cand.unit.file_path.filename().string(), subj)) {
                matched_subjects++;
            }
        }
        score += std::min(0.45, 0.15 * static_cast<double>(matched_subjects));

        // Signal 3: AST Connectivity boost (+0.10 if node has graph edges or vocabulary calls)
        const auto cand_id = cand.unit.primary_element_id;
        std::size_t edge_count = 0;
        if (cand_id != 0) {
            edge_count += graph_.outgoing_relationships(cand_id).size();
            edge_count += graph_.incoming_relationships(cand_id).size();
        }
        edge_count += cand.unit.supporting_elements.size();
        if (edge_count > 0) {
            score += 0.10;
        }

        // Signal 4: Rank decay factor: modest discount for deeper candidates to break ties cleanly
        score *= (1.0 / (1.0 + 0.02 * static_cast<double>(i)));

        seed_scores.push_back(SeedCandidateScore{
            .index = i,
            .candidate = &cand,
            .suitability_score = score,
        });
    }

    // Sort seed candidates by suitability score descending
    std::sort(seed_scores.begin(), seed_scores.end(),
              [](const SeedCandidateScore& a, const SeedCandidateScore& b) {
                  if (std::abs(a.suitability_score - b.suitability_score) > 1e-6) {
                      return a.suitability_score > b.suitability_score;
                  }
                  if (std::abs(a.candidate->hybrid_score - b.candidate->hybrid_score) > 1e-6) {
                      return a.candidate->hybrid_score > b.candidate->hybrid_score;
                  }
                  return a.candidate->unit.primary_element_id < b.candidate->unit.primary_element_id;
              });

    std::vector<const retrieval::PrimarySearchResult*> seeds;
    const std::size_t seed_limit = std::min<std::size_t>(options.max_seeds, seed_scores.size());
    std::unordered_set<std::size_t> selected_seed_indices;

    for (std::size_t i = 0; i < seed_limit; ++i) {
        seeds.push_back(seed_scores[i].candidate);
        selected_seed_indices.insert(seed_scores[i].index);
        metrics.selected_seed_names.push_back(seed_scores[i].candidate->unit.primary_element.name);
    }
    metrics.seeds_investigated = seeds.size();
    metrics.seed_selection_latency_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - seed_sel_start).count();

    if (!options.enabled || hop0_candidates.empty() || options.max_hops == 0 || seeds.empty()) {
        std::vector<InvestigatedCandidate> uninvestigated;
        const std::size_t pool_limit =
            std::min<std::size_t>(options.max_total_candidates, hop0_candidates.size());
        uninvestigated.reserve(pool_limit);

        for (std::size_t i = 0; i < pool_limit; ++i) {
            const auto& cand = hop0_candidates[i];
            uninvestigated.push_back(InvestigatedCandidate{
                .result = cand,
                .role = InvestigationCandidateRole::Hop0Primary,
                .seed_element_id = cand.unit.primary_element_id,
                .seed_symbol_name = cand.unit.primary_element.name,
                .relationship_kind = graph::RelationshipKind::Calls,
                .direction = "Primary",
                .original_rank = i + 1,
                .is_top10 = (i < 10),
                .is_selected_seed = selected_seed_indices.contains(i),
                .is_hop1_discovered = false,
                .is_hop2_discovered = false,
                .hop_depth = 0,
            });
        }

        metrics.investigation_triggered = false;
        metrics.final_candidate_count = uninvestigated.size();
        metrics.investigation_latency_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - start_time).count();
        return uninvestigated;
    }

    metrics.investigation_triggered = true;

    // Set of element IDs and (file, symbol) keys already present in candidate pool
    std::unordered_set<index::ElementId> seen_element_ids;
    std::unordered_set<std::string> seen_keys;

    auto make_key = [](const std::filesystem::path& file_path, std::string_view name) {
        return file_path.generic_string() + "::" + std::string(name);
    };

    for (const auto* seed : seeds) {
        if (seed->unit.primary_element_id != 0) {
            seen_element_ids.insert(seed->unit.primary_element_id);
        }
        seen_keys.insert(make_key(seed->unit.file_path, seed->unit.primary_element.name));
    }

    std::vector<InvestigatedCandidate> hop1_discovered;

    // 3. Execute bounded 1-hop investigation from selected seeds
    for (const auto* seed : seeds) {
        std::size_t discovered_for_this_seed = 0;
        const auto seed_id = seed->unit.primary_element_id;
        const auto& seed_name = seed->unit.primary_element.name;

        // 3.1 Traverse 1-hop outgoing relationships (Calls, Imports, References, Contains, etc.)
        if (seed_id != 0) {
            const auto outgoing = graph_.outgoing_relationships(seed_id);
            for (const auto& rel : outgoing) {
                if (discovered_for_this_seed >= options.max_discovered_per_seed) {
                    break;
                }
                const auto target_id = rel.target;
                if (seen_element_ids.count(target_id)) {
                    continue;
                }

                const auto* target_unit = find_unit_for_element(target_id);
                if (target_unit != nullptr) {
                    const auto key = make_key(target_unit->file_path, target_unit->primary_element.name);
                    if (seen_keys.count(key)) {
                        continue;
                    }

                    seen_element_ids.insert(target_id);
                    seen_keys.insert(key);

                    retrieval::PrimarySearchResult discovered;
                    discovered.unit = *target_unit;
                    discovered.hybrid_score = seed->hybrid_score * options.hop1_score_discount;
                    discovered.provenance = retrieval::RetrievalProvenance::HybridBoth;

                    hop1_discovered.push_back(InvestigatedCandidate{
                        .result = std::move(discovered),
                        .role = InvestigationCandidateRole::Hop1Relationship,
                        .seed_element_id = seed_id,
                        .seed_symbol_name = seed_name,
                        .relationship_kind = rel.kind,
                        .direction = "Outgoing",
                        .original_rank = 0,
                        .is_top10 = false,
                        .is_selected_seed = false,
                        .is_hop1_discovered = true,
                        .is_hop2_discovered = false,
                        .hop_depth = 1,
                    });
                    discovered_for_this_seed++;

                    metrics.steps.push_back(InvestigationStep{
                        .step_number = 1,
                        .seed_element_id = seed_id,
                        .seed_symbol_name = seed_name,
                        .target_element_id = target_id,
                        .target_symbol_name = target_unit->primary_element.name,
                        .relationship_kind = rel.kind,
                        .role = InvestigationCandidateRole::Hop1Relationship,
                    });
                }
            }

            // 3.2 Traverse 1-hop incoming relationships (Callers, Importers, Containers)
            if (discovered_for_this_seed < options.max_discovered_per_seed) {
                const auto incoming = graph_.incoming_relationships(seed_id);
                for (const auto& rel : incoming) {
                    if (discovered_for_this_seed >= options.max_discovered_per_seed) {
                        break;
                    }
                    const auto source_id = rel.source;
                    if (seen_element_ids.count(source_id)) {
                        continue;
                    }

                    const auto* source_unit = find_unit_for_element(source_id);
                    if (source_unit != nullptr) {
                        const auto key = make_key(source_unit->file_path, source_unit->primary_element.name);
                        if (seen_keys.count(key)) {
                            continue;
                        }

                        seen_element_ids.insert(source_id);
                        seen_keys.insert(key);

                        retrieval::PrimarySearchResult discovered;
                        discovered.unit = *source_unit;
                        discovered.hybrid_score = seed->hybrid_score * options.hop1_score_discount * 0.95;
                        discovered.provenance = retrieval::RetrievalProvenance::HybridBoth;

                        hop1_discovered.push_back(InvestigatedCandidate{
                            .result = std::move(discovered),
                            .role = InvestigationCandidateRole::Hop1Relationship,
                            .seed_element_id = seed_id,
                            .seed_symbol_name = seed_name,
                            .relationship_kind = rel.kind,
                            .direction = "Incoming",
                            .original_rank = 0,
                            .is_top10 = false,
                            .is_selected_seed = false,
                            .is_hop1_discovered = true,
                            .is_hop2_discovered = false,
                            .hop_depth = 1,
                        });
                        discovered_for_this_seed++;

                        metrics.steps.push_back(InvestigationStep{
                            .step_number = 1,
                            .seed_element_id = seed_id,
                            .seed_symbol_name = seed_name,
                            .target_element_id = source_id,
                            .target_symbol_name = source_unit->primary_element.name,
                            .relationship_kind = rel.kind,
                            .role = InvestigationCandidateRole::Hop1Relationship,
                        });
                    }
                }
            }
        }

        // 3.3 AST Vocabulary Discovery (inspect supporting calls, references, and type names in seed)
        if (options.enable_vocabulary_discovery &&
            discovered_for_this_seed < options.max_discovered_per_seed) {
            for (const auto& supp : seed->unit.supporting_elements) {
                if (discovered_for_this_seed >= options.max_discovered_per_seed) {
                    break;
                }
                if (supp.name.empty() || supp.name.size() < 3) {
                    continue;
                }

                const auto* related_unit = find_unit_by_symbol_name(supp.name);
                if (related_unit != nullptr) {
                    const auto key = make_key(related_unit->file_path, related_unit->primary_element.name);
                    if (seen_keys.count(key)) {
                        continue;
                    }

                    if (related_unit->primary_element_id != 0) {
                        seen_element_ids.insert(related_unit->primary_element_id);
                    }
                    seen_keys.insert(key);

                    retrieval::PrimarySearchResult discovered;
                    discovered.unit = *related_unit;
                    discovered.hybrid_score = seed->hybrid_score * options.hop1_score_discount * 0.9;
                    discovered.provenance = retrieval::RetrievalProvenance::HybridBoth;

                    hop1_discovered.push_back(InvestigatedCandidate{
                        .result = std::move(discovered),
                        .role = InvestigationCandidateRole::Hop1Vocabulary,
                        .seed_element_id = seed_id,
                        .seed_symbol_name = seed_name,
                        .relationship_kind = graph::RelationshipKind::Calls,
                        .direction = "Vocabulary",
                        .original_rank = 0,
                        .is_top10 = false,
                        .is_selected_seed = false,
                        .is_hop1_discovered = true,
                        .is_hop2_discovered = false,
                        .hop_depth = 1,
                    });
                    discovered_for_this_seed++;

                    metrics.steps.push_back(InvestigationStep{
                        .step_number = 1,
                        .seed_element_id = seed_id,
                        .seed_symbol_name = seed_name,
                        .target_element_id = related_unit->primary_element_id,
                        .target_symbol_name = related_unit->primary_element.name,
                        .relationship_kind = graph::RelationshipKind::Calls,
                        .role = InvestigationCandidateRole::Hop1Vocabulary,
                    });
                }
            }
        }
    }

    metrics.hop1_candidates_discovered = hop1_discovered.size();

    // Sort discovered Hop-1 candidates by hybrid score descending
    std::sort(hop1_discovered.begin(), hop1_discovered.end(),
              [](const auto& a, const auto& b) {
                  return a.result.hybrid_score > b.result.hybrid_score;
              });

    // 4. Execute bounded 2-hop investigation from discovered Hop-1 candidates (if max_hops >= 2)
    std::vector<InvestigatedCandidate> hop2_discovered;
    if (options.max_hops >= 2 && !hop1_discovered.empty()) {
        for (const auto& hop1_cand : hop1_discovered) {
            std::size_t discovered_for_this_hop1 = 0;
            const auto hop1_id = hop1_cand.result.unit.primary_element_id;
            const auto& hop1_name = hop1_cand.result.unit.primary_element.name;

            // 4.1 Traverse 2-hop outgoing relationships
            if (hop1_id != 0) {
                const auto outgoing = graph_.outgoing_relationships(hop1_id);
                for (const auto& rel : outgoing) {
                    if (discovered_for_this_hop1 >= options.max_discovered_per_hop1) {
                        break;
                    }
                    const auto target_id = rel.target;
                    if (seen_element_ids.count(target_id)) {
                        continue;
                    }

                    const auto* target_unit = find_unit_for_element(target_id);
                    if (target_unit != nullptr) {
                        const auto key = make_key(target_unit->file_path, target_unit->primary_element.name);
                        if (seen_keys.count(key)) {
                            continue;
                        }

                        seen_element_ids.insert(target_id);
                        seen_keys.insert(key);

                        retrieval::PrimarySearchResult discovered;
                        discovered.unit = *target_unit;
                        discovered.hybrid_score = hop1_cand.result.hybrid_score * options.hop2_score_discount;
                        discovered.provenance = retrieval::RetrievalProvenance::HybridBoth;

                        hop2_discovered.push_back(InvestigatedCandidate{
                            .result = std::move(discovered),
                            .role = InvestigationCandidateRole::Hop2Relationship,
                            .seed_element_id = hop1_cand.seed_element_id,
                            .seed_symbol_name = hop1_cand.seed_symbol_name,
                            .relationship_kind = rel.kind,
                            .direction = "Outgoing",
                            .original_rank = 0,
                            .is_top10 = false,
                            .is_selected_seed = false,
                            .is_hop1_discovered = false,
                            .is_hop2_discovered = true,
                            .hop_depth = 2,
                        });
                        discovered_for_this_hop1++;

                        metrics.steps.push_back(InvestigationStep{
                            .step_number = 2,
                            .seed_element_id = hop1_id,
                            .seed_symbol_name = hop1_name,
                            .target_element_id = target_id,
                            .target_symbol_name = target_unit->primary_element.name,
                            .relationship_kind = rel.kind,
                            .role = InvestigationCandidateRole::Hop2Relationship,
                        });
                    }
                }

                // 4.2 Traverse 2-hop incoming relationships
                if (discovered_for_this_hop1 < options.max_discovered_per_hop1) {
                    const auto incoming = graph_.incoming_relationships(hop1_id);
                    for (const auto& rel : incoming) {
                        if (discovered_for_this_hop1 >= options.max_discovered_per_hop1) {
                            break;
                        }
                        const auto source_id = rel.source;
                        if (seen_element_ids.count(source_id)) {
                            continue;
                        }

                        const auto* source_unit = find_unit_for_element(source_id);
                        if (source_unit != nullptr) {
                            const auto key = make_key(source_unit->file_path, source_unit->primary_element.name);
                            if (seen_keys.count(key)) {
                                continue;
                            }

                            seen_element_ids.insert(source_id);
                            seen_keys.insert(key);

                            retrieval::PrimarySearchResult discovered;
                            discovered.unit = *source_unit;
                            discovered.hybrid_score = hop1_cand.result.hybrid_score * options.hop2_score_discount * 0.95;
                            discovered.provenance = retrieval::RetrievalProvenance::HybridBoth;

                            hop2_discovered.push_back(InvestigatedCandidate{
                                .result = std::move(discovered),
                                .role = InvestigationCandidateRole::Hop2Relationship,
                                .seed_element_id = hop1_cand.seed_element_id,
                                .seed_symbol_name = hop1_cand.seed_symbol_name,
                                .relationship_kind = rel.kind,
                                .direction = "Incoming",
                                .original_rank = 0,
                                .is_top10 = false,
                                .is_selected_seed = false,
                                .is_hop1_discovered = false,
                                .is_hop2_discovered = true,
                                .hop_depth = 2,
                            });
                            discovered_for_this_hop1++;

                            metrics.steps.push_back(InvestigationStep{
                                .step_number = 2,
                                .seed_element_id = hop1_id,
                                .seed_symbol_name = hop1_name,
                                .target_element_id = source_id,
                                .target_symbol_name = source_unit->primary_element.name,
                                .relationship_kind = rel.kind,
                                .role = InvestigationCandidateRole::Hop2Relationship,
                            });
                        }
                    }
                }
            }

            // 4.3 2-Hop AST Vocabulary Discovery
            if (options.enable_vocabulary_discovery &&
                discovered_for_this_hop1 < options.max_discovered_per_hop1) {
                for (const auto& supp : hop1_cand.result.unit.supporting_elements) {
                    if (discovered_for_this_hop1 >= options.max_discovered_per_hop1) {
                        break;
                    }
                    if (supp.name.empty() || supp.name.size() < 3) {
                        continue;
                    }

                    const auto* related_unit = find_unit_by_symbol_name(supp.name);
                    if (related_unit != nullptr) {
                        const auto key = make_key(related_unit->file_path, related_unit->primary_element.name);
                        if (seen_keys.count(key)) {
                            continue;
                        }

                        if (related_unit->primary_element_id != 0) {
                            seen_element_ids.insert(related_unit->primary_element_id);
                        }
                        seen_keys.insert(key);

                        retrieval::PrimarySearchResult discovered;
                        discovered.unit = *related_unit;
                        discovered.hybrid_score = hop1_cand.result.hybrid_score * options.hop2_score_discount * 0.9;
                        discovered.provenance = retrieval::RetrievalProvenance::HybridBoth;

                        hop2_discovered.push_back(InvestigatedCandidate{
                            .result = std::move(discovered),
                            .role = InvestigationCandidateRole::Hop2Vocabulary,
                            .seed_element_id = hop1_cand.seed_element_id,
                            .seed_symbol_name = hop1_cand.seed_symbol_name,
                            .relationship_kind = graph::RelationshipKind::Calls,
                            .direction = "Vocabulary",
                            .original_rank = 0,
                            .is_top10 = false,
                            .is_selected_seed = false,
                            .is_hop1_discovered = false,
                            .is_hop2_discovered = true,
                            .hop_depth = 2,
                        });
                        discovered_for_this_hop1++;

                        metrics.steps.push_back(InvestigationStep{
                            .step_number = 2,
                            .seed_element_id = hop1_id,
                            .seed_symbol_name = hop1_name,
                            .target_element_id = related_unit->primary_element_id,
                            .target_symbol_name = related_unit->primary_element.name,
                            .relationship_kind = graph::RelationshipKind::Calls,
                            .role = InvestigationCandidateRole::Hop2Vocabulary,
                        });
                    }
                }
            }
        }
    }

    metrics.hop2_candidates_discovered = hop2_discovered.size();

    // Sort discovered Hop-2 candidates by hybrid score descending
    std::sort(hop2_discovered.begin(), hop2_discovered.end(),
              [](const auto& a, const auto& b) {
                  return a.result.hybrid_score > b.result.hybrid_score;
              });

    // Merge candidates into final pool bounded by max_total_candidates
    std::vector<InvestigatedCandidate> results;
    results.reserve(options.max_total_candidates);

    // 1. Add selected seeds
    for (std::size_t i = 0; i < seed_limit && results.size() < options.max_total_candidates; ++i) {
        const auto& score_item = seed_scores[i];
        results.push_back(InvestigatedCandidate{
            .result = *score_item.candidate,
            .role = InvestigationCandidateRole::Hop0Primary,
            .seed_element_id = score_item.candidate->unit.primary_element_id,
            .seed_symbol_name = score_item.candidate->unit.primary_element.name,
            .relationship_kind = graph::RelationshipKind::Calls,
            .direction = "Primary",
            .original_rank = score_item.index + 1,
            .is_top10 = (score_item.index < 10),
            .is_selected_seed = true,
            .is_hop1_discovered = false,
            .is_hop2_discovered = false,
            .hop_depth = 0,
        });
    }

    // 2. Add discovered Hop-1 candidates
    for (auto& disc : hop1_discovered) {
        if (results.size() >= options.max_total_candidates) {
            break;
        }
        results.push_back(std::move(disc));
    }

    // 3. Add discovered Hop-2 candidates
    for (auto& disc : hop2_discovered) {
        if (results.size() >= options.max_total_candidates) {
            break;
        }
        results.push_back(std::move(disc));
    }

    // 4. Fill remaining slots from top Hop-0 candidates in rank order
    for (std::size_t i = 0; i < hop0_candidates.size() && results.size() < options.max_total_candidates; ++i) {
        const auto& cand = hop0_candidates[i];
        const auto key = make_key(cand.unit.file_path, cand.unit.primary_element.name);
        if (seen_keys.count(key)) {
            continue;
        }
        if (cand.unit.primary_element_id != 0 && seen_element_ids.count(cand.unit.primary_element_id)) {
            continue;
        }

        seen_keys.insert(key);
        if (cand.unit.primary_element_id != 0) {
            seen_element_ids.insert(cand.unit.primary_element_id);
        }

        results.push_back(InvestigatedCandidate{
            .result = cand,
            .role = InvestigationCandidateRole::Hop0Primary,
            .seed_element_id = cand.unit.primary_element_id,
            .seed_symbol_name = cand.unit.primary_element.name,
            .relationship_kind = graph::RelationshipKind::Calls,
            .direction = "Primary",
            .original_rank = i + 1,
            .is_top10 = (i < 10),
            .is_selected_seed = false,
            .is_hop1_discovered = false,
            .is_hop2_discovered = false,
            .hop_depth = 0,
        });
    }

    if (results.size() > options.max_total_candidates) {
        results.resize(options.max_total_candidates);
    }

    metrics.final_candidate_count = results.size();
    metrics.investigation_latency_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - start_time).count();

    return results;
}

std::vector<retrieval::PrimarySearchResult>
BoundedInvestigationService::investigate(
    std::string_view query,
    std::span<const retrieval::PrimarySearchResult> hop0_candidates,
    const InvestigationOptions& options,
    InvestigationMetrics* metrics) const {
    const auto candidates = investigate_candidates(query, hop0_candidates, options, metrics);
    std::vector<retrieval::PrimarySearchResult> out;
    out.reserve(candidates.size());
    for (const auto& c : candidates) {
        out.push_back(c.result);
    }
    return out;
}

std::vector<retrieval::PrimarySearchResult>
BoundedInvestigationService::investigate_with_metrics(
    const retrieval::QueryRepresentation& query_rep,
    std::span<const retrieval::PrimarySearchResult> hop0_candidates,
    const InvestigationOptions& options,
    InvestigationMetrics& metrics) const {
    const auto candidates =
        investigate_candidates_with_metrics(query_rep, hop0_candidates, options, metrics);
    std::vector<retrieval::PrimarySearchResult> out;
    out.reserve(candidates.size());
    for (const auto& c : candidates) {
        out.push_back(c.result);
    }
    return out;
}

std::vector<InvestigatedCandidate>
BoundedInvestigationService::expand_to_hop2(
    std::span<const InvestigatedCandidate> existing_candidates,
    const InvestigationOptions& options,
    InvestigationMetrics* metrics) const {
    if (metrics) {
        return expand_to_hop2_with_metrics(existing_candidates, options, *metrics);
    }
    InvestigationMetrics dummy;
    return expand_to_hop2_with_metrics(existing_candidates, options, dummy);
}

std::vector<InvestigatedCandidate>
BoundedInvestigationService::expand_to_hop2_with_metrics(
    std::span<const InvestigatedCandidate> existing_candidates,
    const InvestigationOptions& options,
    InvestigationMetrics& metrics) const {

    const auto start_time = Clock::now();
    std::vector<InvestigatedCandidate> results;
    results.reserve(options.max_total_candidates);

    std::unordered_set<index::ElementId> seen_element_ids;
    std::unordered_set<std::string> seen_keys;

    auto make_key = [](const std::filesystem::path& file_path, std::string_view name) {
        return file_path.generic_string() + "::" + std::string(name);
    };

    std::vector<const InvestigatedCandidate*> hop1_candidates;

    for (const auto& cand : existing_candidates) {
        if (cand.result.unit.primary_element_id != 0) {
            seen_element_ids.insert(cand.result.unit.primary_element_id);
        }
        seen_keys.insert(make_key(cand.result.unit.file_path, cand.result.unit.primary_element.name));
        results.push_back(cand);

        if (cand.role == InvestigationCandidateRole::Hop1Relationship ||
            cand.role == InvestigationCandidateRole::Hop1Vocabulary ||
            cand.is_hop1_discovered) {
            hop1_candidates.push_back(&cand);
        }
    }

    if (!options.enabled || options.max_hops < 2 || hop1_candidates.empty() || results.size() >= options.max_total_candidates) {
        metrics.investigation_latency_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - start_time).count();
        metrics.final_candidate_count = results.size();
        return results;
    }

    std::vector<InvestigatedCandidate> hop2_discovered;

    for (const auto* hop1_cand : hop1_candidates) {
        std::size_t discovered_for_this_hop1 = 0;
        const auto hop1_id = hop1_cand->result.unit.primary_element_id;
        const auto& hop1_name = hop1_cand->result.unit.primary_element.name;

        // 1. Traverse 2-hop outgoing relationships
        if (hop1_id != 0) {
            const auto outgoing = graph_.outgoing_relationships(hop1_id);
            for (const auto& rel : outgoing) {
                if (discovered_for_this_hop1 >= options.max_discovered_per_hop1) {
                    break;
                }
                const auto target_id = rel.target;
                if (seen_element_ids.count(target_id)) {
                    continue;
                }

                const auto* target_unit = find_unit_for_element(target_id);
                if (target_unit != nullptr) {
                    const auto key = make_key(target_unit->file_path, target_unit->primary_element.name);
                    if (seen_keys.count(key)) {
                        continue;
                    }

                    seen_element_ids.insert(target_id);
                    seen_keys.insert(key);

                    retrieval::PrimarySearchResult discovered;
                    discovered.unit = *target_unit;
                    discovered.hybrid_score = hop1_cand->result.hybrid_score * options.hop2_score_discount;
                    discovered.provenance = retrieval::RetrievalProvenance::HybridBoth;

                    hop2_discovered.push_back(InvestigatedCandidate{
                        .result = std::move(discovered),
                        .role = InvestigationCandidateRole::Hop2Relationship,
                        .seed_element_id = hop1_cand->seed_element_id,
                        .seed_symbol_name = hop1_cand->seed_symbol_name,
                        .relationship_kind = rel.kind,
                        .direction = "Outgoing",
                        .original_rank = 0,
                        .is_top10 = false,
                        .is_selected_seed = false,
                        .is_hop1_discovered = false,
                        .is_hop2_discovered = true,
                        .hop_depth = 2,
                    });
                    discovered_for_this_hop1++;

                    metrics.steps.push_back(InvestigationStep{
                        .step_number = 2,
                        .seed_element_id = hop1_id,
                        .seed_symbol_name = hop1_name,
                        .target_element_id = target_id,
                        .target_symbol_name = target_unit->primary_element.name,
                        .relationship_kind = rel.kind,
                        .role = InvestigationCandidateRole::Hop2Relationship,
                    });
                }
            }

            // 2. Traverse 2-hop incoming relationships
            if (discovered_for_this_hop1 < options.max_discovered_per_hop1) {
                const auto incoming = graph_.incoming_relationships(hop1_id);
                for (const auto& rel : incoming) {
                    if (discovered_for_this_hop1 >= options.max_discovered_per_hop1) {
                        break;
                    }
                    const auto source_id = rel.source;
                    if (seen_element_ids.count(source_id)) {
                        continue;
                    }

                    const auto* source_unit = find_unit_for_element(source_id);
                    if (source_unit != nullptr) {
                        const auto key = make_key(source_unit->file_path, source_unit->primary_element.name);
                        if (seen_keys.count(key)) {
                            continue;
                        }

                        seen_element_ids.insert(source_id);
                        seen_keys.insert(key);

                        retrieval::PrimarySearchResult discovered;
                        discovered.unit = *source_unit;
                        discovered.hybrid_score = hop1_cand->result.hybrid_score * options.hop2_score_discount * 0.95;
                        discovered.provenance = retrieval::RetrievalProvenance::HybridBoth;

                        hop2_discovered.push_back(InvestigatedCandidate{
                            .result = std::move(discovered),
                            .role = InvestigationCandidateRole::Hop2Relationship,
                            .seed_element_id = hop1_cand->seed_element_id,
                            .seed_symbol_name = hop1_cand->seed_symbol_name,
                            .relationship_kind = rel.kind,
                            .direction = "Incoming",
                            .original_rank = 0,
                            .is_top10 = false,
                            .is_selected_seed = false,
                            .is_hop1_discovered = false,
                            .is_hop2_discovered = true,
                            .hop_depth = 2,
                        });
                        discovered_for_this_hop1++;

                        metrics.steps.push_back(InvestigationStep{
                            .step_number = 2,
                            .seed_element_id = hop1_id,
                            .seed_symbol_name = hop1_name,
                            .target_element_id = source_id,
                            .target_symbol_name = source_unit->primary_element.name,
                            .relationship_kind = rel.kind,
                            .role = InvestigationCandidateRole::Hop2Relationship,
                        });
                    }
                }
            }
        }

        // 3. 2-Hop AST Vocabulary Discovery
        if (options.enable_vocabulary_discovery &&
            discovered_for_this_hop1 < options.max_discovered_per_hop1) {
            for (const auto& supp : hop1_cand->result.unit.supporting_elements) {
                if (discovered_for_this_hop1 >= options.max_discovered_per_hop1) {
                    break;
                }
                if (supp.name.empty() || supp.name.size() < 3) {
                    continue;
                }

                const auto* related_unit = find_unit_by_symbol_name(supp.name);
                if (related_unit != nullptr) {
                    const auto key = make_key(related_unit->file_path, related_unit->primary_element.name);
                    if (seen_keys.count(key)) {
                        continue;
                    }

                    if (related_unit->primary_element_id != 0) {
                        seen_element_ids.insert(related_unit->primary_element_id);
                    }
                    seen_keys.insert(key);

                    retrieval::PrimarySearchResult discovered;
                    discovered.unit = *related_unit;
                    discovered.hybrid_score = hop1_cand->result.hybrid_score * options.hop2_score_discount * 0.9;
                    discovered.provenance = retrieval::RetrievalProvenance::HybridBoth;

                    hop2_discovered.push_back(InvestigatedCandidate{
                        .result = std::move(discovered),
                        .role = InvestigationCandidateRole::Hop2Vocabulary,
                        .seed_element_id = hop1_cand->seed_element_id,
                        .seed_symbol_name = hop1_cand->seed_symbol_name,
                        .relationship_kind = graph::RelationshipKind::Calls,
                        .direction = "Vocabulary",
                        .original_rank = 0,
                        .is_top10 = false,
                        .is_selected_seed = false,
                        .is_hop1_discovered = false,
                        .is_hop2_discovered = true,
                        .hop_depth = 2,
                    });
                    discovered_for_this_hop1++;

                    metrics.steps.push_back(InvestigationStep{
                        .step_number = 2,
                        .seed_element_id = hop1_id,
                        .seed_symbol_name = hop1_name,
                        .target_element_id = related_unit->primary_element_id,
                        .target_symbol_name = related_unit->primary_element.name,
                        .relationship_kind = graph::RelationshipKind::Calls,
                        .role = InvestigationCandidateRole::Hop2Vocabulary,
                    });
                }
            }
        }
    }

    metrics.hop2_candidates_discovered = hop2_discovered.size();

    // Sort discovered Hop-2 candidates by hybrid score descending
    std::sort(hop2_discovered.begin(), hop2_discovered.end(),
              [](const auto& a, const auto& b) {
                  return a.result.hybrid_score > b.result.hybrid_score;
              });

    for (auto& disc : hop2_discovered) {
        if (results.size() >= options.max_total_candidates) {
            break;
        }
        results.push_back(std::move(disc));
    }

    metrics.final_candidate_count = results.size();
    metrics.investigation_latency_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - start_time).count();

    return results;
}

evidence::EvidenceBundle
BoundedInvestigationService::assemble_bundle(
    std::string_view query,
    std::span<const InvestigatedCandidate> candidates,
    const evidence::EvidenceAssembler& assembler) const {

    evidence::EvidenceBundle bundle;
    bundle.query = std::string(query);
    bundle.items.reserve(candidates.size());

    for (const auto& cand : candidates) {
        evidence::EvidenceItem item = assembler.assemble_item(cand.result);
        if (cand.role != InvestigationCandidateRole::Hop0Primary) {
            item.is_expanded_relationship = true;
            item.expansion_depth = (cand.role == InvestigationCandidateRole::Hop2Relationship ||
                                    cand.role == InvestigationCandidateRole::Hop2Vocabulary ||
                                    cand.hop_depth == 2)
                                       ? 2
                                       : 1;
            item.expansion_relationship_type = std::string(graph::to_string(cand.relationship_kind));
            item.expansion_direction = cand.direction;
            item.expansion_seed_id = cand.seed_element_id;
            item.expansion_seed_name = cand.seed_symbol_name;
        } else {
            item.is_expanded_relationship = false;
            item.expansion_depth = 0;
        }
        bundle.items.push_back(std::move(item));
    }

    return bundle;
}

}  // namespace amoeba::investigation
