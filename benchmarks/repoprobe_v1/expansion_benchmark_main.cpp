// Amoeba — Phase 8.2.9 Bounded Typed Relationship Expansion Benchmark
//
// Evaluates Depth 0, Depth 1, and Depth 2 across benchmark queries on a repository:
// - Measures accuracy, groundedness, sufficiency, Hit@1/5/10
// - Measures context units, character footprint, and latency per depth configuration
// - Measures per-relationship type utility across all 7 kinds
// - Tests negative refusal safety

#include "amoeba/context/context_builder.hpp"
#include "amoeba/evidence/evidence_assembler.hpp"
#include "amoeba/evidence/evidence_bundle.hpp"
#include "amoeba/evidence/evidence_sufficiency.hpp"
#include "amoeba/graph/relationship_evidence_resolver.hpp"
#include "amoeba/graph/relationship_expander.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/graph/repository_graph_builder.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/reasoning/grounded_answer.hpp"
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

[[nodiscard]] std::vector<std::string> split_str(std::string_view s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(std::string{s});
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty()) result.push_back(item);
    }
    return result;
}

std::vector<QueryItem> load_queries_tsv(const fs::path& tsv_path) {
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

        QueryItem q;
        q.question_id = qid;
        q.repo_name = repo;
        q.source = src;
        q.taxonomy = tax;
        q.difficulty = diff;
        q.question_text = qtext;
        q.expected_files = split_str(files_str, '|');
        q.expected_symbols = split_str(syms_str, '|');
        q.expected_relationships = split_str(rels_str, '|');
        q.expected_concepts = split_str(concepts_str, '|');

        queries.push_back(std::move(q));
    }
    return queries;
}

struct DepthEvaluationResult {
    uint32_t depth{0};
    bool sufficiency_passed{false};
    double sufficiency_confidence{0.0};
    std::string sufficiency_reason;
    std::size_t context_units{0};
    std::size_t added_units{0};
    std::size_t context_characters{0};
    std::size_t edges_traversed{0};
    double expansion_latency_ms{0.0};
    double assembly_latency_ms{0.0};
    double sufficiency_latency_ms{0.0};
    double total_latency_ms{0.0};
    bool answer_is_grounded{false};
    bool is_correct{false};
    std::vector<std::string> expanded_items_provenance;
};

struct QuestionEvaluationResult {
    std::string question_id;
    std::string repo_name;
    std::string taxonomy;
    std::string difficulty;
    bool is_negative{false};
    bool hit_at_1{false};
    bool hit_at_5{false};
    bool hit_at_10{false};
    double retrieval_latency_ms{0.0};

    // Results per depth configuration
    DepthEvaluationResult depth_0;
    DepthEvaluationResult depth_1;
    DepthEvaluationResult depth_2;
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: amoeba_expansion_benchmark <repo_dir> <queries_tsv_file> <output_json_file>\n";
        return 1;
    }

    fs::path repo_dir = argv[1];
    fs::path tsv_file = argv[2];
    fs::path out_file = argv[3];

    if (!fs::exists(repo_dir) || !fs::exists(tsv_file)) {
        std::cerr << "ERROR: Invalid repo_dir or queries_tsv_file path.\n";
        return 1;
    }

    auto queries = load_queries_tsv(tsv_file);
    std::cout << "Running Phase 8.2.9 Expansion Benchmark on " << repo_dir.filename().string()
              << " (" << queries.size() << " queries)...\n";

    amoeba::semantic::DeterministicEmbeddingProvider embedding_provider{32};
    amoeba::source::SourceSnippetReader snippet_reader;
    amoeba::parser::SourceParser parser;
    amoeba::scanner::RepositoryScanner scanner;

    // 1. Scan and Parse repository
    auto scan_res = scanner.scan(repo_dir);
    amoeba::index::InvertedIndex index;
    amoeba::graph::RelationshipGraph graph;
    std::vector<amoeba::parser::ParsedFile> parsed_files;

    for (const auto& f : scan_res.files) {
        auto pf = parser.parse_file(f.path);
        if (pf.success) {
            index.add_parsed_file(pf);
            parsed_files.push_back(std::move(pf));
        }
    }

    // 2. Build graph & resolvers
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(index, graph);
    auto rel_resolver = std::make_unique<amoeba::graph::RelationshipEvidenceResolver>(graph, index);
    auto expander = std::make_unique<amoeba::graph::RelationshipExpander>(graph, index, snippet_reader);
    amoeba::evidence::EvidenceAssembler assembler(snippet_reader, *rel_resolver, *expander);

    // 3. Initialize retrieval pipeline
    auto pipeline = std::make_unique<amoeba::retrieval::PrimaryRetrievalPipeline>(
        parsed_files, index, embedding_provider);

    amoeba::context::ContextBuilder context_builder;
    amoeba::reasoning::FakeLLMRuntime llm_runtime;
    amoeba::reasoning::ReasoningService reasoning_service(llm_runtime);

    std::vector<QuestionEvaluationResult> all_results;

    for (const auto& q : queries) {
        QuestionEvaluationResult qer;
        qer.question_id = q.question_id;
        qer.repo_name = q.repo_name;
        qer.taxonomy = q.taxonomy;
        qer.difficulty = q.difficulty;
        qer.is_negative = q.expected_files.empty() && q.expected_symbols.empty();

        // Step A: Primary Retrieval (Shared across depth 0, 1, 2)
        auto t_ret_s = Clock::now();
        amoeba::retrieval::PrimarySearchOptions opts;
        opts.max_results = 10;
        auto search_results = pipeline->search(q.question_text, opts);
        qer.retrieval_latency_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - t_ret_s).count();

        // Check Hit@K
        for (std::size_t rank = 0; rank < search_results.size(); ++rank) {
            const auto& sr = search_results[rank];
            auto f_str = sr.unit.file_path.generic_string();
            bool hit = false;
            for (const auto& ef : q.expected_files) {
                if (path_matches(f_str, ef)) { hit = true; break; }
            }
            if (!hit) {
                for (const auto& es : q.expected_symbols) {
                    if (symbol_matches(sr.unit.primary_element.name, es)) { hit = true; break; }
                }
            }
            if (hit) {
                if (rank == 0) qer.hit_at_1 = true;
                if (rank < 5) qer.hit_at_5 = true;
                if (rank < 10) qer.hit_at_10 = true;
            }
        }

        // Evaluate for depths 0, 1, 2
        for (uint32_t d : {0u, 1u, 2u}) {
            DepthEvaluationResult der;
            der.depth = d;

            amoeba::evidence::EvidenceAssemblerOptions assem_opts;
            assem_opts.source_context_lines = 5;
            assem_opts.include_source_snippets = true;
            assem_opts.include_relationships = true;
            assem_opts.enable_expansion = (d > 0);
            assem_opts.expansion_config.max_depth = d;
            assem_opts.expansion_config.max_units = 3;
            assem_opts.expansion_config.max_source_characters = 4000;
            assem_opts.expansion_config.prioritize_query_relevance = true;

            auto t_assem_s = Clock::now();
            auto bundle = assembler.assemble(q.question_text, search_results, assem_opts);
            der.assembly_latency_ms =
                std::chrono::duration<double, std::milli>(Clock::now() - t_assem_s).count();

            der.context_units = bundle.items.size();
            std::size_t primary_cnt = std::min(search_results.size(), bundle.items.size());
            der.added_units = (bundle.items.size() > primary_cnt) ? (bundle.items.size() - primary_cnt) : 0;

            for (const auto& item : bundle.items) {
                if (item.source_excerpt.has_value()) {
                    der.context_characters += item.source_excerpt->text.size();
                }
                if (item.is_expanded_relationship) {
                    std::string prov = item.expansion_relationship_type + " (" + item.expansion_direction + ") from `" + item.expansion_seed_name + "` -> `" + item.primary_element().name + "`";
                    der.expanded_items_provenance.push_back(prov);
                }
            }

            // Sufficiency Check
            auto t_suff_s = Clock::now();
            auto suff = amoeba::evidence::EvidenceSufficiencyChecker::check(q.question_text, bundle);
            der.sufficiency_latency_ms =
                std::chrono::duration<double, std::milli>(Clock::now() - t_suff_s).count();

            der.sufficiency_passed = suff.is_sufficient;
            der.sufficiency_confidence = suff.confidence_score;
            der.sufficiency_reason = suff.reason;

            // Context package & Reasoning
            auto pkg = context_builder.build(bundle);
            auto ans = reasoning_service.answer_grounded(q.question_text, pkg, suff);

            der.answer_is_grounded = ans.is_grounded();
            der.total_latency_ms = qer.retrieval_latency_ms + der.assembly_latency_ms + der.sufficiency_latency_ms;

            if (qer.is_negative) {
                der.is_correct = (ans.is_insufficient() && !suff.is_sufficient);
            } else {
                der.is_correct = (qer.hit_at_10 && suff.is_sufficient && ans.is_grounded());
            }

            if (d == 0) qer.depth_0 = std::move(der);
            else if (d == 1) qer.depth_1 = std::move(der);
            else qer.depth_2 = std::move(der);
        }

        all_results.push_back(std::move(qer));
    }

    // Write output JSON
    std::ofstream ofs(out_file);
    ofs << "[\n";
    for (std::size_t i = 0; i < all_results.size(); ++i) {
        const auto& r = all_results[i];
        ofs << "  {\n";
        ofs << "    \"question_id\": \"" << escape_json(r.question_id) << "\",\n";
        ofs << "    \"repository\": \"" << escape_json(r.repo_name) << "\",\n";
        ofs << "    \"taxonomy\": \"" << escape_json(r.taxonomy) << "\",\n";
        ofs << "    \"difficulty\": \"" << escape_json(r.difficulty) << "\",\n";
        ofs << "    \"is_negative\": " << (r.is_negative ? "true" : "false") << ",\n";
        ofs << "    \"hit_at_1\": " << (r.hit_at_1 ? "true" : "false") << ",\n";
        ofs << "    \"hit_at_5\": " << (r.hit_at_5 ? "true" : "false") << ",\n";
        ofs << "    \"hit_at_10\": " << (r.hit_at_10 ? "true" : "false") << ",\n";
        ofs << "    \"retrieval_latency_ms\": " << r.retrieval_latency_ms << ",\n";

        auto write_depth = [&](std::string_view d_key, const DepthEvaluationResult& d) {
            ofs << "    \"" << d_key << "\": {\n";
            ofs << "      \"sufficiency_passed\": " << (d.sufficiency_passed ? "true" : "false") << ",\n";
            ofs << "      \"sufficiency_confidence\": " << d.sufficiency_confidence << ",\n";
            ofs << "      \"sufficiency_reason\": \"" << escape_json(d.sufficiency_reason) << "\",\n";
            ofs << "      \"context_units\": " << d.context_units << ",\n";
            ofs << "      \"added_units\": " << d.added_units << ",\n";
            ofs << "      \"context_characters\": " << d.context_characters << ",\n";
            ofs << "      \"assembly_latency_ms\": " << d.assembly_latency_ms << ",\n";
            ofs << "      \"sufficiency_latency_ms\": " << d.sufficiency_latency_ms << ",\n";
            ofs << "      \"total_latency_ms\": " << d.total_latency_ms << ",\n";
            ofs << "      \"answer_is_grounded\": " << (d.answer_is_grounded ? "true" : "false") << ",\n";
            ofs << "      \"is_correct\": " << (d.is_correct ? "true" : "false") << ",\n";
            ofs << "      \"expanded_provenance\": [";
            for (std::size_t pi = 0; pi < d.expanded_items_provenance.size(); ++pi) {
                if (pi > 0) ofs << ", ";
                ofs << "\"" << escape_json(d.expanded_items_provenance[pi]) << "\"";
            }
            ofs << "]\n";
            ofs << "    }";
        };

        write_depth("depth_0", r.depth_0); ofs << ",\n";
        write_depth("depth_1", r.depth_1); ofs << ",\n";
        write_depth("depth_2", r.depth_2); ofs << "\n";

        ofs << "  }" << (i + 1 < all_results.size() ? ",\n" : "\n");
    }
    ofs << "]\n";

    std::cout << "Completed benchmark for " << repo_dir.filename().string() << ".\n";
    return 0;
}
