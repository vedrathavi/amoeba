#include "amoeba/context/context_builder.hpp"
#include "amoeba/evidence/evidence_assembler.hpp"
#include "amoeba/evidence/evidence_bundle.hpp"
#include "amoeba/evidence/evidence_sufficiency.hpp"
#include "amoeba/graph/relationship_evidence_resolver.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/graph/repository_graph_builder.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/investigation/bounded_investigation_service.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/reasoning/fake_llm_runtime.hpp"
#include "amoeba/reasoning/reasoning_service.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/scanner/repository_scanner.hpp"
#include "amoeba/semantic/deterministic_embedding_provider.hpp"
#include "amoeba/source/source_snippet_reader.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

struct QueryItem {
    std::string question_id;
    std::string repo_name;
    std::string source;
    std::string taxonomy;
    std::string difficulty;
    std::string question_text;
    std::vector<std::string> expected_files;
    std::vector<std::string> expected_symbols;
    std::vector<std::string> expected_relationships;
    std::vector<std::string> expected_concepts;
};

[[nodiscard]] std::string escape_json(std::string_view s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) <= 0x1f) {
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                      << static_cast<int>(c);
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

[[nodiscard]] bool path_matches(std::string_view retrieved, std::string_view expected) {
    if (retrieved.empty() || expected.empty()) return false;
    std::string r(retrieved);
    std::string e(expected);
    std::replace(r.begin(), r.end(), '\\', '/');
    std::replace(e.begin(), e.end(), '\\', '/');

    if (r == e) return true;
    if (r.ends_with(e) || e.ends_with(r)) return true;
    if (r.find(e) != std::string::npos || e.find(r) != std::string::npos) return true;

    auto r_fname = fs::path(r).filename().string();
    auto e_fname = fs::path(e).filename().string();
    return (!r_fname.empty() && r_fname == e_fname);
}

[[nodiscard]] bool symbol_matches(std::string_view retrieved, std::string_view expected) {
    if (retrieved.empty() || expected.empty()) return false;
    if (retrieved == expected) return true;
    if (retrieved.find(expected) != std::string::npos || expected.find(retrieved) != std::string::npos) return true;
    return false;
}

[[nodiscard]] std::vector<QueryItem> load_queries_tsv(const fs::path& tsv_path) {
    std::vector<QueryItem> queries;
    std::ifstream ifs(tsv_path);
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::stringstream ss(line);
        std::string qid, repo, src, tax, diff, qtext, files_str, syms_str, rels_str, concepts_str;
        if (!std::getline(ss, qid, '\t')) continue;
        if (!std::getline(ss, repo, '\t')) continue;
        if (!std::getline(ss, src, '\t')) continue;
        if (!std::getline(ss, tax, '\t')) continue;
        if (!std::getline(ss, diff, '\t')) continue;
        if (!std::getline(ss, qtext, '\t')) continue;
        std::getline(ss, files_str, '\t');
        std::getline(ss, syms_str, '\t');
        std::getline(ss, rels_str, '\t');
        std::getline(ss, concepts_str, '\t');

        QueryItem item;
        item.question_id = qid;
        item.repo_name = repo;
        item.source = src;
        item.taxonomy = tax;
        item.difficulty = diff;
        item.question_text = qtext;

        auto split_pipe = [](const std::string& str) {
            std::vector<std::string> out;
            std::stringstream pss(str);
            std::string tok;
            while (std::getline(pss, tok, '|')) {
                if (!tok.empty()) out.push_back(tok);
            }
            return out;
        };

        item.expected_files = split_pipe(files_str);
        item.expected_symbols = split_pipe(syms_str);
        item.expected_relationships = split_pipe(rels_str);
        item.expected_concepts = split_pipe(concepts_str);
        queries.push_back(std::move(item));
    }
    return queries;
}

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

struct CustomSeedCandidateScore {
    std::size_t index{0};
    const amoeba::retrieval::PrimarySearchResult* candidate{nullptr};
    double suitability_score{0.0};
};

// Custom candidate selection simulation
std::vector<amoeba::investigation::InvestigatedCandidate> run_custom_candidate_selection(
    const std::string& policy_name,
    const amoeba::retrieval::QueryRepresentation& query_rep,
    std::span<const amoeba::retrieval::PrimarySearchResult> hop0_candidates,
    const amoeba::graph::RelationshipGraph& graph,
    const std::unordered_map<amoeba::index::ElementId, const amoeba::retrieval::RetrievalUnit*>& element_to_unit_map) {

    using namespace amoeba::investigation;
    std::vector<CustomSeedCandidateScore> seed_scores;
    seed_scores.reserve(hop0_candidates.size());

    uint32_t max_seeds = 3;
    uint32_t max_discovered_per_seed = 3;
    uint32_t max_total_candidates = 10;
    std::size_t max_cands_per_file = (policy_name == "policy_c" || policy_name == "policy_d") ? 2 : 100;
    bool prioritize_primary = (policy_name == "policy_d");

    if (policy_name == "policy_b") {
        max_seeds = 5;
        max_total_candidates = 15;
    }

    for (std::size_t i = 0; i < hop0_candidates.size(); ++i) {
        const auto& cand = hop0_candidates[i];
        if (cand.hybrid_score < 0.05) continue;

        double score = cand.hybrid_score;

        // Signal 1: Technical entity match (+0.30)
        bool has_te_match = false;
        for (const auto& te : query_rep.technical_entities) {
            if (contains_ci(cand.unit.primary_element.name, te) ||
                contains_ci(cand.unit.file_path.generic_string(), te)) {
                has_te_match = true;
                break;
            }
        }
        if (has_te_match) score += 0.30;

        // Signal 2: Subject grounding overlap
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

        // Signal 3: AST connectivity
        const auto cand_id = cand.unit.primary_element_id;
        std::size_t edge_count = 0;
        if (cand_id != 0) {
            edge_count += graph.outgoing_relationships(cand_id).size();
            edge_count += graph.incoming_relationships(cand_id).size();
        }
        edge_count += cand.unit.supporting_elements.size();
        if (edge_count > 0) score += 0.10;

        // Policy D: Prioritize primary symbols over anonymous/sub-elements
        if (prioritize_primary) {
            if (!cand.unit.primary_element.name.empty() && 
                cand.unit.primary_element.name.find("<anon") == std::string::npos &&
                cand.unit.primary_element.name.find("__init__") == std::string::npos) {
                score += 0.15;
            }
        }

        // Signal 4: Rank decay
        score *= (1.0 / (1.0 + 0.02 * static_cast<double>(i)));

        seed_scores.push_back(CustomSeedCandidateScore{
            .index = i,
            .candidate = &cand,
            .suitability_score = score,
        });
    }

    std::sort(seed_scores.begin(), seed_scores.end(),
              [](const CustomSeedCandidateScore& a, const CustomSeedCandidateScore& b) {
                  if (std::abs(a.suitability_score - b.suitability_score) > 1e-6) {
                      return a.suitability_score > b.suitability_score;
                  }
                  return a.candidate->hybrid_score > b.candidate->hybrid_score;
              });

    std::vector<const amoeba::retrieval::PrimarySearchResult*> seeds;
    std::unordered_map<std::string, std::size_t> file_cand_counts;
    std::unordered_set<std::size_t> selected_seed_indices;

    for (const auto& item : seed_scores) {
        if (seeds.size() >= max_seeds) break;
        std::string fpath = item.candidate->unit.file_path.generic_string();
        if (file_cand_counts[fpath] >= 1 && (policy_name == "policy_c" || policy_name == "policy_d")) {
            // Diversity in seeds: max 1 seed per file
            continue;
        }
        seeds.push_back(item.candidate);
        selected_seed_indices.insert(item.index);
        file_cand_counts[fpath]++;
    }

    // Set of element IDs and (file, symbol) keys
    std::unordered_set<amoeba::index::ElementId> seen_element_ids;
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

    for (const auto* seed : seeds) {
        std::size_t discovered_for_this_seed = 0;
        const auto seed_id = seed->unit.primary_element_id;
        const auto& seed_name = seed->unit.primary_element.name;

        if (seed_id != 0) {
            const auto outgoing = graph.outgoing_relationships(seed_id);
            for (const auto& rel : outgoing) {
                if (discovered_for_this_seed >= max_discovered_per_seed) break;
                const auto target_id = rel.target;
                if (seen_element_ids.count(target_id)) continue;

                auto it = element_to_unit_map.find(target_id);
                if (it != element_to_unit_map.end() && it->second != nullptr) {
                    const auto* target_unit = it->second;
                    const auto key = make_key(target_unit->file_path, target_unit->primary_element.name);
                    if (seen_keys.count(key)) continue;

                    std::string fpath = target_unit->file_path.generic_string();
                    if (file_cand_counts[fpath] >= max_cands_per_file) continue;

                    seen_element_ids.insert(target_id);
                    seen_keys.insert(key);
                    file_cand_counts[fpath]++;

                    amoeba::retrieval::PrimarySearchResult discovered;
                    discovered.unit = *target_unit;
                    discovered.hybrid_score = seed->hybrid_score * 0.85;
                    discovered.provenance = amoeba::retrieval::RetrievalProvenance::HybridBoth;

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
                    });
                    discovered_for_this_seed++;
                }
            }
        }
    }

    std::vector<InvestigatedCandidate> results;
    results.reserve(max_total_candidates);

    // 1. Add selected seeds
    for (const auto* seed : seeds) {
        if (results.size() >= max_total_candidates) break;
        results.push_back(InvestigatedCandidate{
            .result = *seed,
            .role = InvestigationCandidateRole::Hop0Primary,
            .seed_element_id = seed->unit.primary_element_id,
            .seed_symbol_name = seed->unit.primary_element.name,
            .relationship_kind = amoeba::graph::RelationshipKind::Calls,
            .direction = "Primary",
            .original_rank = 1,
            .is_top10 = true,
            .is_selected_seed = true,
            .is_hop1_discovered = false,
        });
    }

    // 2. Add discovered Hop-1 candidates
    for (auto& disc : hop1_discovered) {
        if (results.size() >= max_total_candidates) break;
        results.push_back(std::move(disc));
    }

    // 3. Fill remaining slots from top Hop-0 candidates in rank order, respecting max_cands_per_file
    for (std::size_t i = 0; i < hop0_candidates.size() && results.size() < max_total_candidates; ++i) {
        const auto& cand = hop0_candidates[i];
        const auto key = make_key(cand.unit.file_path, cand.unit.primary_element.name);
        if (seen_keys.count(key)) continue;
        if (cand.unit.primary_element_id != 0 && seen_element_ids.count(cand.unit.primary_element_id)) continue;

        std::string fpath = cand.unit.file_path.generic_string();
        if (file_cand_counts[fpath] >= max_cands_per_file) continue;

        seen_keys.insert(key);
        if (cand.unit.primary_element_id != 0) seen_element_ids.insert(cand.unit.primary_element_id);
        file_cand_counts[fpath]++;

        results.push_back(InvestigatedCandidate{
            .result = cand,
            .role = InvestigationCandidateRole::Hop0Primary,
            .seed_element_id = cand.unit.primary_element_id,
            .seed_symbol_name = cand.unit.primary_element.name,
            .relationship_kind = amoeba::graph::RelationshipKind::Calls,
            .direction = "Primary",
            .original_rank = i + 1,
            .is_top10 = (i < 10),
            .is_selected_seed = false,
            .is_hop1_discovered = false,
        });
    }

    return results;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: test_candidate_selection_experiments <repo_dir> <queries_tsv_file> <output_base_dir>\n";
        return 1;
    }

    fs::path repo_dir = argv[1];
    fs::path tsv_file = argv[2];
    fs::path out_base_dir = argv[3];
    std::string repo_alias = repo_dir.filename().string();

    auto queries = load_queries_tsv(tsv_file);
    std::cout << "Indexing " << repo_alias << " (" << queries.size() << " queries)...\n";

    // 1. Scan Repository
    amoeba::scanner::RepositoryScanner scanner;
    auto scan_res = scanner.scan(repo_dir);

    // 2. Parse Source Files & Build Inverted Index
    amoeba::parser::SourceParser parser;
    amoeba::index::InvertedIndex inverted_index;
    amoeba::graph::RelationshipGraph graph;
    std::vector<amoeba::parser::ParsedFile> parsed_files;

    for (const auto& file_meta : scan_res.files) {
        auto pf = parser.parse_file(file_meta.path);
        if (pf.success) {
            inverted_index.add_parsed_file(pf);
            parsed_files.push_back(std::move(pf));
        }
    }

    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);

    // 3. Initialize Services
    amoeba::source::SourceSnippetReader snippet_reader;
    amoeba::semantic::DeterministicEmbeddingProvider embedding_provider(32);
    auto rel_resolver = std::make_unique<amoeba::graph::RelationshipEvidenceResolver>(graph, inverted_index);
    auto pipeline = std::make_unique<amoeba::retrieval::PrimaryRetrievalPipeline>(
        parsed_files, inverted_index, embedding_provider);

    amoeba::investigation::BoundedInvestigationService investigator(inverted_index, graph, *pipeline, snippet_reader);
    amoeba::evidence::EvidenceAssembler assembler(snippet_reader, *rel_resolver);
    amoeba::context::ContextBuilder context_builder;
    amoeba::reasoning::FakeLLMRuntime llm_runtime;
    amoeba::reasoning::ReasoningService reasoning_service(llm_runtime);

    std::unordered_map<amoeba::index::ElementId, const amoeba::retrieval::RetrievalUnit*> element_to_unit_map;
    element_to_unit_map.reserve(pipeline->units().size() * 2);
    for (const auto& u : pipeline->units()) {
        if (u.primary_element_id != 0) {
            element_to_unit_map[u.primary_element_id] = &u;
        }
    }

    std::vector<std::string> policies = {"policy_a", "policy_b", "policy_c", "policy_d"};

    for (const auto& policy_name : policies) {
        fs::path policy_dir = out_base_dir / policy_name;
        fs::create_directories(policy_dir);
        fs::path out_file = policy_dir / (repo_alias + ".json");
        std::ofstream ofs(out_file);
        ofs << "[\n";

        for (std::size_t q_idx = 0; q_idx < queries.size(); ++q_idx) {
            const auto& q = queries[q_idx];
            const bool is_negative_case = q.expected_files.empty() && q.expected_symbols.empty();

            // 1. Hop-0 Hybrid Search (Top-50 Reservoir)
            amoeba::retrieval::PrimarySearchOptions search_opts;
            search_opts.max_results = 50;

            auto search_results = pipeline->search(q.question_text, search_opts);

            std::size_t target_rank_in_hop0 = 0;
            bool hop0_hit_at_1 = false;
            bool hop0_hit_at_5 = false;
            bool hop0_hit_at_10 = false;
            bool hop0_hit_at_20 = false;
            bool hop0_hit_at_50 = false;

            for (std::size_t i = 0; i < search_results.size(); ++i) {
                const auto& sr = search_results[i];
                const auto f_str = sr.unit.file_path.generic_string();
                bool hit = false;
                for (const auto& exp_f : q.expected_files) {
                    if (path_matches(f_str, exp_f)) { hit = true; break; }
                }
                if (!hit) {
                    for (const auto& exp_s : q.expected_symbols) {
                        if (symbol_matches(sr.unit.primary_element.name, exp_s)) { hit = true; break; }
                    }
                }
                if (hit) {
                    if (target_rank_in_hop0 == 0) target_rank_in_hop0 = i + 1;
                    if (i == 0) hop0_hit_at_1 = true;
                    if (i < 5) hop0_hit_at_5 = true;
                    if (i < 10) hop0_hit_at_10 = true;
                    if (i < 20) hop0_hit_at_20 = true;
                    if (i < 50) hop0_hit_at_50 = true;
                }
            }

            // 2. Candidate Selection via Policy
            const auto query_rep = amoeba::retrieval::QueryUnderstanding::analyze(q.question_text);
            std::vector<amoeba::investigation::InvestigatedCandidate> investigated_candidates;

            if (policy_name == "policy_a") {
                amoeba::investigation::InvestigationOptions invest_opts;
                invest_opts.max_total_candidates = 10;
                invest_opts.max_seeds = 3;
                invest_opts.max_discovered_per_seed = 3;
                investigated_candidates = investigator.investigate_candidates(q.question_text, search_results, invest_opts);
            } else {
                investigated_candidates = run_custom_candidate_selection(policy_name, query_rep, search_results, graph, element_to_unit_map);
            }

            // 3. Evidence Assembly & Sufficiency
            auto bundle = investigator.assemble_bundle(q.question_text, investigated_candidates, assembler);
            auto sufficiency = amoeba::evidence::EvidenceSufficiencyChecker::check(q.question_text, bundle);
            auto context_package = context_builder.build(bundle);
            auto grounded_answer = reasoning_service.answer_grounded(q.question_text, context_package, sufficiency);

            bool hit_at_10 = false;
            bool hit_at_5 = false;
            bool hit_at_1 = false;
            std::vector<std::string> retrieved_files;
            std::vector<std::string> retrieved_symbols;

            for (std::size_t i = 0; i < investigated_candidates.size() && i < 10; ++i) {
                const auto& cand = investigated_candidates[i];
                const auto f_str = cand.result.unit.file_path.generic_string();
                const auto& s_name = cand.result.unit.primary_element.name;
                retrieved_files.push_back(f_str);
                retrieved_symbols.push_back(s_name);

                bool hit = false;
                for (const auto& exp_f : q.expected_files) {
                    if (path_matches(f_str, exp_f)) { hit = true; break; }
                }
                if (!hit) {
                    for (const auto& exp_s : q.expected_symbols) {
                        if (symbol_matches(s_name, exp_s)) { hit = true; break; }
                    }
                }
                if (hit) {
                    if (i == 0) hit_at_1 = true;
                    if (i < 5) hit_at_5 = true;
                    if (i < 10) hit_at_10 = true;
                }
            }

            std::string correctness = "incorrect";
            std::string failure_category = "none";

            if (is_negative_case) {
                if (!sufficiency.is_sufficient && grounded_answer.is_insufficient()) {
                    correctness = "correct";
                    failure_category = "none";
                } else {
                    correctness = "incorrect";
                    failure_category = "groundedness_failure";
                }
            } else {
                if (!hit_at_10) {
                    correctness = "incorrect";
                    failure_category = "retrieval_failure";
                } else if (!sufficiency.is_sufficient) {
                    correctness = "incorrect";
                    failure_category = "sufficiency_failure";
                } else if (!grounded_answer.is_grounded()) {
                    correctness = "incorrect";
                    failure_category = "reasoning_failure";
                } else {
                    correctness = "correct";
                    failure_category = "none";
                }
            }

            ofs << "  {\n"
                << "    \"question_id\": \"" << escape_json(q.question_id) << "\",\n"
                << "    \"repository\": \"" << escape_json(q.repo_name) << "\",\n"
                << "    \"source\": \"" << escape_json(q.source) << "\",\n"
                << "    \"target_rank_in_hop0\": " << target_rank_in_hop0 << ",\n"
                << "    \"hop0_hit_at_1\": " << (hop0_hit_at_1 ? "true" : "false") << ",\n"
                << "    \"hop0_hit_at_5\": " << (hop0_hit_at_5 ? "true" : "false") << ",\n"
                << "    \"hop0_hit_at_10\": " << (hop0_hit_at_10 ? "true" : "false") << ",\n"
                << "    \"hop0_hit_at_20\": " << (hop0_hit_at_20 ? "true" : "false") << ",\n"
                << "    \"hop0_hit_at_50\": " << (hop0_hit_at_50 ? "true" : "false") << ",\n"
                << "    \"retrieval_hit_at_1\": " << (hit_at_1 ? "true" : "false") << ",\n"
                << "    \"retrieval_hit_at_5\": " << (hit_at_5 ? "true" : "false") << ",\n"
                << "    \"retrieval_hit_at_10\": " << (hit_at_10 ? "true" : "false") << ",\n"
                << "    \"sufficiency_decision\": " << (sufficiency.is_sufficient ? "true" : "false") << ",\n"
                << "    \"sufficiency_reason\": \"" << escape_json(sufficiency.reason) << "\",\n"
                << "    \"correctness\": \"" << correctness << "\",\n"
                << "    \"failure_category\": \"" << failure_category << "\"\n"
                << "  }" << (q_idx + 1 < queries.size() ? ",\n" : "\n");
        }

        ofs << "]\n";
    }

    std::cout << "Completed all 4 policies for " << repo_alias << "\n";
    return 0;
}
