// Amoeba — RepoProbe v1 Native Repository Evaluator
//
// Evaluates the CURRENT Amoeba C++ engine for a single repository against its benchmark questions.
// Performs full scanning, Tree-sitter parsing, inverted indexing, relationship resolving,
// primary retrieval, evidence assembly, sufficiency checking, and grounded reasoning.

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
#include "amoeba/reasoning/grounded_answer.hpp"
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
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

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

// Parse line-delimited TSV queries
// Format per line: question_id \t repo_name \t source \t taxonomy \t difficulty \t question_text \t expected_files (pipe-separated) \t expected_symbols (pipe-separated) \t expected_relationships (pipe-separated) \t expected_concepts (pipe-separated)
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

        // Split pipe-separated fields
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

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: amoeba_repoprobe_benchmark <repo_dir> <queries_tsv_file> <output_json_file> [config_mode]\n"
                  << "  config_mode: baseline_top10 | top50_no_investigation | top50_bounded_investigation (default)\n";
        return 1;
    }

    fs::path repo_dir = argv[1];
    fs::path tsv_file = argv[2];
    fs::path out_file = argv[3];
    std::string config_mode = (argc >= 5) ? argv[4] : "top50_bounded_investigation";

    if (!fs::exists(repo_dir)) {
        std::cerr << "ERROR: Repo directory does not exist: " << repo_dir << "\n";
        return 1;
    }

    if (!fs::exists(tsv_file)) {
        std::cerr << "ERROR: Queries file does not exist: " << tsv_file << "\n";
        return 1;
    }

    auto queries = load_queries_tsv(tsv_file);
    std::cout << "Loaded " << queries.size() << " queries for " << repo_dir.filename().string()
              << " [Config: " << config_mode << "]\n";

    // 1. Scan Repository
    amoeba::scanner::RepositoryScanner scanner;
    auto scan_start = Clock::now();
    auto scan_res = scanner.scan(repo_dir);
    auto scan_ms = std::chrono::duration<double, std::milli>(Clock::now() - scan_start).count();
    std::cout << "  Discovered " << scan_res.total_files_included() << " files in " << scan_ms << " ms.\n";

    // 2. Parse Source Files & Build Inverted Index
    amoeba::parser::SourceParser parser;
    amoeba::index::InvertedIndex index;
    amoeba::graph::RelationshipGraph graph;
    std::vector<amoeba::parser::ParsedFile> parsed_files;

    auto parse_start = Clock::now();
    for (const auto& file_meta : scan_res.files) {
        auto pf = parser.parse_file(file_meta.path);
        if (pf.success) {
            index.add_parsed_file(pf);
            parsed_files.push_back(std::move(pf));
        }
    }
    auto parse_ms = std::chrono::duration<double, std::milli>(Clock::now() - parse_start).count();
    std::cout << "  Parsed " << parsed_files.size() << " files (" << index.element_count() << " elements) in "
              << parse_ms << " ms.\n";

    // 3. Build Relationship Resolver & Retrieval Pipeline
    amoeba::source::SourceSnippetReader snippet_reader;
    amoeba::semantic::DeterministicEmbeddingProvider embedding_provider{32};

    auto graph_start = Clock::now();
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    auto graph_ms = std::chrono::duration<double, std::milli>(Clock::now() - graph_start).count();
    std::cout << "  Built relationship graph with " << graph.relationship_count() << " edges in "
              << graph_ms << " ms.\n";

    auto rel_resolver = std::make_unique<amoeba::graph::RelationshipEvidenceResolver>(graph, index);
    auto pipeline = std::make_unique<amoeba::retrieval::PrimaryRetrievalPipeline>(
        parsed_files, index, embedding_provider);
    std::cout << "  Built primary retrieval pipeline with " << pipeline->primary_unit_count() << " primary units.\n";

    amoeba::investigation::BoundedInvestigationService investigator(
        index, graph, *pipeline, snippet_reader);

    amoeba::evidence::EvidenceAssembler assembler(snippet_reader, *rel_resolver);
    amoeba::context::ContextBuilder context_builder;
    amoeba::reasoning::FakeLLMRuntime llm_runtime;
    amoeba::reasoning::ReasoningService reasoning_service(llm_runtime);

    // 4. Run Queries
    std::ofstream ofs(out_file);
    ofs << "[\n";

    for (std::size_t q_idx = 0; q_idx < queries.size(); ++q_idx) {
        const auto& q = queries[q_idx];
        auto q_start = Clock::now();

        // Hop-0 Retrieval Stage (Top-10 for baseline_top10, Top-50 for broad reservoir)
        auto retr_start = Clock::now();
        amoeba::retrieval::PrimarySearchOptions search_opts;
        const uint32_t reservoir_size = (config_mode == "baseline_top10") ? 10 : 50;
        search_opts.max_results = reservoir_size;
        auto search_results = pipeline->search(q.question_text, search_opts);
        double retr_ms = std::chrono::duration<double, std::milli>(Clock::now() - retr_start).count();

        std::size_t target_rank_in_hop0 = 0;
        bool hop0_hit_at_1 = false;
        bool hop0_hit_at_5 = false;
        bool hop0_hit_at_10 = false;
        bool hop0_hit_at_20 = false;
        bool hop0_hit_at_50 = false;

        for (std::size_t i = 0; i < search_results.size(); ++i) {
            const auto& sr = search_results[i];
            auto f_str = sr.unit.file_path.generic_string();
            bool hit = false;
            for (const auto& exp_f : q.expected_files) {
                if (path_matches(f_str, exp_f)) {
                    hit = true;
                    break;
                }
            }
            if (!hit) {
                for (const auto& exp_s : q.expected_symbols) {
                    if (symbol_matches(sr.unit.primary_element.name, exp_s)) {
                        hit = true;
                        break;
                    }
                }
            }
            if (hit) {
                if (target_rank_in_hop0 == 0) {
                    target_rank_in_hop0 = i + 1;
                }
                if (i == 0) hop0_hit_at_1 = true;
                if (i < 5) hop0_hit_at_5 = true;
                if (i < 10) hop0_hit_at_10 = true;
                if (i < 20) hop0_hit_at_20 = true;
                if (i < 50) hop0_hit_at_50 = true;
            }
        }

        // Investigation / Candidate Selection Stage
        auto invest_start = Clock::now();
        amoeba::investigation::InvestigationMetrics invest_metrics;
        amoeba::investigation::InvestigationOptions invest_opts;
        invest_opts.max_total_candidates = 10;
        invest_opts.max_seeds = 3;
        invest_opts.max_discovered_per_seed = 3;

        std::vector<amoeba::investigation::InvestigatedCandidate> investigated_candidates;

        if (config_mode == "top50_no_investigation") {
            // Configuration B: Top-50 without Hop-1 investigation, bounded to Top-10
            invest_opts.enabled = false;
            investigated_candidates = investigator.investigate_candidates(
                q.question_text, search_results, invest_opts, &invest_metrics);
        } else if (config_mode == "baseline_top10") {
            // Configuration A: Hop-0 Top-10 + Bounded Investigation
            investigated_candidates = investigator.investigate_candidates(
                q.question_text, search_results, invest_opts, &invest_metrics);
        } else {
            // Configuration C (Default): Top-50 Reservoir + Deterministic Seed Pruning + Bounded Hop-1
            investigated_candidates = investigator.investigate_candidates(
                q.question_text, search_results, invest_opts, &invest_metrics);
        }
        double invest_ms = std::chrono::duration<double, std::milli>(Clock::now() - invest_start).count();

        std::vector<std::string> retrieved_files;
        std::vector<std::string> retrieved_symbols;
        bool hit_at_1 = false;
        bool hit_at_5 = false;
        bool hit_at_10 = false;
        bool hit_at_20 = false;
        bool hit_at_50 = false;
        bool target_selected_as_seed = false;
        bool hop1_discovered_target = false;

        // Check if any selected seed matched expected targets
        for (const auto& seed_name : invest_metrics.selected_seed_names) {
            for (const auto& exp_s : q.expected_symbols) {
                if (symbol_matches(seed_name, exp_s)) {
                    target_selected_as_seed = true;
                    break;
                }
            }
            if (target_selected_as_seed) break;
        }

        for (std::size_t i = 0; i < investigated_candidates.size(); ++i) {
            const auto& cand = investigated_candidates[i];
            const auto& sr = cand.result;
            auto f_str = sr.unit.file_path.generic_string();
            retrieved_files.push_back(f_str);
            retrieved_symbols.push_back(sr.unit.primary_element.name);

            bool hit = false;
            for (const auto& exp_f : q.expected_files) {
                if (path_matches(f_str, exp_f)) {
                    hit = true;
                    break;
                }
            }
            if (!hit) {
                for (const auto& exp_s : q.expected_symbols) {
                    if (symbol_matches(sr.unit.primary_element.name, exp_s)) {
                        hit = true;
                        break;
                    }
                }
            }

            if (hit) {
                if (i == 0) hit_at_1 = true;
                if (i < 5) hit_at_5 = true;
                if (i < 10) hit_at_10 = true;
                if (i < 20) hit_at_20 = true;
                if (i < 50) hit_at_50 = true;
                if (cand.is_hop1_discovered || cand.role != amoeba::investigation::InvestigationCandidateRole::Hop0Primary) {
                    hop1_discovered_target = true;
                }
            }
        }

        const bool is_negative_case = q.expected_files.empty() && q.expected_symbols.empty();

        // Evidence Assembly Stage
        auto bundle = investigator.assemble_bundle(q.question_text, investigated_candidates, assembler);

        // Sufficiency Stage
        auto suff_start = Clock::now();
        auto sufficiency = amoeba::evidence::EvidenceSufficiencyChecker::check(q.question_text, bundle);
        double suff_ms = std::chrono::duration<double, std::milli>(Clock::now() - suff_start).count();

        // Context Construction & Reasoning Stage
        auto context_package = context_builder.build(bundle);
        auto reason_start = Clock::now();
        auto grounded_answer = reasoning_service.answer_grounded(q.question_text, context_package, sufficiency);
        double reason_ms = std::chrono::duration<double, std::milli>(Clock::now() - reason_start).count();
        double total_ms = std::chrono::duration<double, std::milli>(Clock::now() - q_start).count();

        std::string correctness;
        bool usefulness = false;
        std::string failure_category;
        std::string diagnostic_explanation;

        if (is_negative_case) {
            if (!sufficiency.is_sufficient && grounded_answer.is_insufficient()) {
                correctness = "correct";
                usefulness = true;
                failure_category = "none";
                diagnostic_explanation = "Correctly triggered grounded refusal on absent repository feature.";
            } else {
                correctness = "incorrect";
                usefulness = false;
                failure_category = "groundedness_failure";
                diagnostic_explanation = "Hallucinated or accepted generic match on non-existent feature.";
            }
        } else {
            if (!hit_at_10) {
                correctness = "incorrect";
                usefulness = false;
                failure_category = "retrieval_failure";
                diagnostic_explanation = "Expected source file/symbol not recovered in top-10 candidate pool.";
            } else if (!sufficiency.is_sufficient) {
                correctness = "incorrect";
                usefulness = false;
                failure_category = "sufficiency_failure";
                diagnostic_explanation = "Candidate retrieved but evidence sufficiency gate rejected due to missing domain subject terms: " +
                                           sufficiency.reason;
            } else if (!grounded_answer.is_grounded()) {
                correctness = "incorrect";
                usefulness = false;
                failure_category = "reasoning_failure";
                diagnostic_explanation = "Sufficient evidence available but reasoning service failed grounded validation.";
            } else {
                correctness = "correct";
                usefulness = true;
                failure_category = "none";
                diagnostic_explanation = "Accurately retrieved target units, verified sufficiency, and grounded answer.";
            }
        }

        // Output JSON record for this question
        ofs << "  {\n"
            << "    \"question_id\": \"" << escape_json(q.question_id) << "\",\n"
            << "    \"repository\": \"" << escape_json(q.repo_name) << "\",\n"
            << "    \"source\": \"" << escape_json(q.source) << "\",\n"
            << "    \"taxonomy\": \"" << escape_json(q.taxonomy) << "\",\n"
            << "    \"difficulty\": \"" << escape_json(q.difficulty) << "\",\n"
            << "    \"expected_files\": [";
        for (std::size_t fi = 0; fi < q.expected_files.size(); ++fi) {
            ofs << "\"" << escape_json(q.expected_files[fi]) << "\"" << (fi + 1 < q.expected_files.size() ? ", " : "");
        }
        ofs << "],\n"
            << "    \"expected_symbols\": [";
        for (std::size_t si = 0; si < q.expected_symbols.size(); ++si) {
            ofs << "\"" << escape_json(q.expected_symbols[si]) << "\"" << (si + 1 < q.expected_symbols.size() ? ", " : "");
        }
        ofs << "],\n"
            << "    \"expected_relationships\": [";
        for (std::size_t ri = 0; ri < q.expected_relationships.size(); ++ri) {
            ofs << "\"" << escape_json(q.expected_relationships[ri]) << "\"" << (ri + 1 < q.expected_relationships.size() ? ", " : "");
        }
        ofs << "],\n"
            << "    \"expected_concepts\": [";
        for (std::size_t ci = 0; ci < q.expected_concepts.size(); ++ci) {
            ofs << "\"" << escape_json(q.expected_concepts[ci]) << "\"" << (ci + 1 < q.expected_concepts.size() ? ", " : "");
        }
        ofs << "],\n"
            << "    \"retrieved_files\": [";
        for (std::size_t fi = 0; fi < std::min<std::size_t>(5, retrieved_files.size()); ++fi) {
            ofs << "\"" << escape_json(retrieved_files[fi]) << "\"" << (fi + 1 < std::min<std::size_t>(5, retrieved_files.size()) ? ", " : "");
        }
        ofs << "],\n"
            << "    \"retrieved_symbols\": [";
        for (std::size_t si = 0; si < std::min<std::size_t>(5, retrieved_symbols.size()); ++si) {
            ofs << "\"" << escape_json(retrieved_symbols[si]) << "\"" << (si + 1 < std::min<std::size_t>(5, retrieved_symbols.size()) ? ", " : "");
        }
        ofs << "],\n"
            << "    \"target_rank_in_hop0\": " << target_rank_in_hop0 << ",\n"
            << "    \"in_top10\": " << (hop0_hit_at_10 ? "true" : "false") << ",\n"
            << "    \"in_11_to_20\": " << (target_rank_in_hop0 >= 11 && target_rank_in_hop0 <= 20 ? "true" : "false") << ",\n"
            << "    \"in_21_to_50\": " << (target_rank_in_hop0 >= 21 && target_rank_in_hop0 <= 50 ? "true" : "false") << ",\n"
            << "    \"outside_top50\": " << (target_rank_in_hop0 == 0 ? "true" : "false") << ",\n"
            << "    \"target_selected_as_seed\": " << (target_selected_as_seed ? "true" : "false") << ",\n"
            << "    \"hop0_hit_at_1\": " << (hop0_hit_at_1 ? "true" : "false") << ",\n"
            << "    \"hop0_hit_at_5\": " << (hop0_hit_at_5 ? "true" : "false") << ",\n"
            << "    \"hop0_hit_at_10\": " << (hop0_hit_at_10 ? "true" : "false") << ",\n"
            << "    \"hop0_hit_at_20\": " << (hop0_hit_at_20 ? "true" : "false") << ",\n"
            << "    \"hop0_hit_at_50\": " << (hop0_hit_at_50 ? "true" : "false") << ",\n"
            << "    \"hop1_triggered\": " << (invest_metrics.investigation_triggered ? "true" : "false") << ",\n"
            << "    \"seeds_investigated\": " << invest_metrics.seeds_investigated << ",\n"
            << "    \"hop1_candidates_discovered\": " << invest_metrics.hop1_candidates_discovered << ",\n"
            << "    \"hop1_discovered_target\": " << (hop1_discovered_target ? "true" : "false") << ",\n"
            << "    \"seed_selection_latency_ms\": " << invest_metrics.seed_selection_latency_ms << ",\n"
            << "    \"investigation_latency_ms\": " << invest_ms << ",\n"
            << "    \"retrieval_hit_at_1\": " << (hit_at_1 ? "true" : "false") << ",\n"
            << "    \"retrieval_hit_at_5\": " << (hit_at_5 ? "true" : "false") << ",\n"
            << "    \"retrieval_hit_at_10\": " << (hit_at_10 ? "true" : "false") << ",\n"
            << "    \"retrieval_hit_at_20\": " << (hit_at_20 ? "true" : "false") << ",\n"
            << "    \"retrieval_hit_at_50\": " << (hit_at_50 ? "true" : "false") << ",\n"
            << "    \"sufficiency_decision\": " << (sufficiency.is_sufficient ? "true" : "false") << ",\n"
            << "    \"sufficiency_reason\": \"" << escape_json(sufficiency.reason) << "\",\n"
            << "    \"tool_calls\": 0,\n"
            << "    \"tool_names\": [],\n"
            << "    \"evidence_items\": " << bundle.items.size() << ",\n"
            << "    \"evidence_items_count\": " << bundle.items.size() << ",\n"
            << "    \"final_answer\": \"" << escape_json(grounded_answer.answer_text) << "\",\n"
            << "    \"final_answer_status\": \"" << escape_json(amoeba::reasoning::to_string(grounded_answer.status)) << "\",\n"
            << "    \"final_answer_text\": \"" << escape_json(grounded_answer.answer_text) << "\",\n"
            << "    \"grounded\": " << (grounded_answer.is_grounded() ? "true" : "false") << ",\n"
            << "    \"correctness\": \"" << escape_json(correctness) << "\",\n"
            << "    \"usefulness\": " << (usefulness ? "true" : "false") << ",\n"
            << "    \"latency_ms\": " << total_ms << ",\n"
            << "    \"retrieval_latency_ms\": " << retr_ms << ",\n"
            << "    \"sufficiency_latency_ms\": " << suff_ms << ",\n"
            << "    \"reasoning_latency_ms\": " << reason_ms << ",\n"
            << "    \"total_latency_ms\": " << total_ms << ",\n"
            << "    \"failure_category\": \"" << escape_json(failure_category) << "\",\n"
            << "    \"diagnostic_explanation\": \"" << escape_json(diagnostic_explanation) << "\"\n"
            << "  }" << (q_idx + 1 < queries.size() ? ",\n" : "\n");

        std::cout << "    [" << q.question_id << "] Hit@1: " << (hit_at_1 ? "Y" : "N")
                  << " Hit@5: " << (hit_at_5 ? "Y" : "N")
                  << " Suff: " << (sufficiency.is_sufficient ? "Pass" : "Fail")
                  << " Status: " << amoeba::reasoning::to_string(grounded_answer.status)
                  << " (" << std::fixed << std::setprecision(1) << total_ms << " ms)\n";
    }

    ofs << "]\n";
    std::cout << "Wrote repository evaluation results to " << out_file << "\n";
    return 0;
}
