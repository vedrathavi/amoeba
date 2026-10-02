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
#include "amoeba/index/inverted_index.hpp"
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
        std::cerr << "Usage: amoeba_repoprobe_benchmark <repo_dir> <queries_tsv_file> <output_json_file>\n";
        return 1;
    }

    fs::path repo_dir = argv[1];
    fs::path tsv_file = argv[2];
    fs::path out_file = argv[3];

    if (!fs::exists(repo_dir)) {
        std::cerr << "ERROR: Repo directory does not exist: " << repo_dir << "\n";
        return 1;
    }

    if (!fs::exists(tsv_file)) {
        std::cerr << "ERROR: Queries file does not exist: " << tsv_file << "\n";
        return 1;
    }

    auto queries = load_queries_tsv(tsv_file);
    std::cout << "Loaded " << queries.size() << " queries for " << repo_dir.filename().string() << "\n";

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

    auto rel_resolver = std::make_unique<amoeba::graph::RelationshipEvidenceResolver>(graph, index);
    auto pipeline = std::make_unique<amoeba::retrieval::PrimaryRetrievalPipeline>(
        parsed_files, index, embedding_provider);
    std::cout << "  Built primary retrieval pipeline with " << pipeline->primary_unit_count() << " primary units.\n";

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

        // Retrieval Stage
        auto retr_start = Clock::now();
        amoeba::retrieval::PrimarySearchOptions search_opts;
        search_opts.max_results = 10;
        auto search_results = pipeline->search(q.question_text, search_opts);
        double retr_ms = std::chrono::duration<double, std::milli>(Clock::now() - retr_start).count();

        std::vector<std::string> retrieved_files;
        std::vector<std::string> retrieved_symbols;
        bool hit_at_1 = false;
        bool hit_at_5 = false;
        bool hit_at_10 = false;

        for (std::size_t i = 0; i < search_results.size(); ++i) {
            const auto& sr = search_results[i];
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
            }
        }

        const bool is_negative_case = q.expected_files.empty() && q.expected_symbols.empty();

        // Evidence Assembly Stage
        auto bundle = assembler.assemble(q.question_text, search_results);

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
                diagnostic_explanation = "Expected source file/symbol not recovered in top-10 search results.";
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
            << "    \"retrieval_hit_at_1\": " << (hit_at_1 ? "true" : "false") << ",\n"
            << "    \"retrieval_hit_at_5\": " << (hit_at_5 ? "true" : "false") << ",\n"
            << "    \"retrieval_hit_at_10\": " << (hit_at_10 ? "true" : "false") << ",\n"
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
