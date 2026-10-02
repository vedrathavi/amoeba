// Amoeba — Semantic Retrieval Forensic Audit Runner
//
// Evaluates Lexical, Semantic (at Top-1, 5, 10, 20, 50, 100), and Hybrid retrieval independently.
// Produces score distributions, semantic document comparisons, taxonomy breakdowns, and latency profiles.

#include "amoeba/context/context_builder.hpp"
#include "amoeba/evidence/evidence_assembler.hpp"
#include "amoeba/evidence/evidence_bundle.hpp"
#include "amoeba/evidence/evidence_sufficiency.hpp"
#include "amoeba/graph/relationship_evidence_resolver.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/scanner/repository_scanner.hpp"
#include "amoeba/semantic/deterministic_embedding_provider.hpp"
#include "amoeba/semantic/semantic_document.hpp"
#include "amoeba/semantic/similarity.hpp"
#include "amoeba/source/source_snippet_reader.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
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
            std::vector<std::string> res;
            if (str.empty()) return res;
            std::stringstream pss(str);
            std::string tok;
            while (std::getline(pss, tok, '|')) {
                if (!tok.empty()) res.push_back(tok);
            }
            return res;
        };

        item.expected_files = split_pipe(files_str);
        item.expected_symbols = split_pipe(syms_str);
        item.expected_relationships = split_pipe(rels_str);
        item.expected_concepts = split_pipe(concepts_str);

        queries.push_back(std::move(item));
    }
    return queries;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: amoeba_semantic_audit <repo_dir> <queries_tsv_file> <output_json_file>\n";
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
        std::cerr << "ERROR: Queries TSV does not exist: " << tsv_file << "\n";
        return 1;
    }

    auto queries = load_queries_tsv(tsv_file);
    std::cout << "[Semantic Audit] Loaded " << queries.size() << " queries for " << repo_dir.filename().string() << "\n";

    // 1. Scan Repository
    amoeba::scanner::RepositoryScanner scanner;
    auto scan_res = scanner.scan(repo_dir);

    // 2. Parse Files & Build Index
    amoeba::parser::SourceParser parser;
    amoeba::index::InvertedIndex index;
    std::vector<amoeba::parser::ParsedFile> parsed_files;

    for (const auto& file_meta : scan_res.files) {
        auto pf = parser.parse_file(file_meta.path);
        if (pf.success) {
            index.add_parsed_file(pf);
            parsed_files.push_back(std::move(pf));
        }
    }

    // 3. Build Retrieval Pipeline
    amoeba::semantic::DeterministicEmbeddingProvider embedding_provider{32};
    auto pipeline = std::make_unique<amoeba::retrieval::PrimaryRetrievalPipeline>(
        parsed_files, index, embedding_provider);

    std::cout << "  Units: " << pipeline->primary_unit_count() << ", Elements: " << index.element_count() << "\n";

    // 4. Detailed Evaluation per Query
    std::ofstream ofs(out_file);
    ofs << "[\n";

    for (std::size_t q_idx = 0; q_idx < queries.size(); ++q_idx) {
        const auto& q = queries[q_idx];

        // A. Pure Lexical Search (Top-50)
        amoeba::retrieval::PrimarySearchOptions lex_opts;
        lex_opts.alpha = 1.0;
        lex_opts.adaptive_fusion = false;
        lex_opts.lexical_top_k = 50;
        lex_opts.max_results = 50;
        auto lex_results = pipeline->search(q.question_text, lex_opts);

        // B. Pure Semantic Search (Top-100)
        amoeba::retrieval::PrimarySearchOptions sem_opts;
        sem_opts.alpha = 0.0;
        sem_opts.adaptive_fusion = false;
        sem_opts.semantic_top_k = 100;
        sem_opts.max_results = 100;

        // Measure semantic timing breakdown
        auto t_embed_start = Clock::now();
        auto query_vec = embedding_provider.embed(q.question_text);
        auto t_embed_end = Clock::now();
        double embed_ms = std::chrono::duration<double, std::milli>(t_embed_end - t_embed_start).count();

        auto t_sem_start = Clock::now();
        auto sem_results = pipeline->search(q.question_text, sem_opts);
        auto t_sem_end = Clock::now();
        double total_sem_ms = std::chrono::duration<double, std::milli>(t_sem_end - t_sem_start).count();

        // C. Live Hybrid Search (Top-10)
        amoeba::retrieval::PrimarySearchOptions hyb_opts;
        hyb_opts.alpha = 0.5;
        hyb_opts.adaptive_fusion = true;
        hyb_opts.max_results = 10;
        auto hyb_results = pipeline->search(q.question_text, hyb_opts);

        auto is_target = [&](const amoeba::retrieval::RetrievalUnit& u) {
            auto f_str = u.file_path.generic_string();
            for (const auto& ef : q.expected_files) {
                if (path_matches(f_str, ef)) return true;
            }
            for (const auto& es : q.expected_symbols) {
                if (symbol_matches(u.primary_element.name, es)) return true;
            }
            return false;
        };

        // Lexical hits
        int lex_target_rank = -1;
        for (std::size_t i = 0; i < lex_results.size(); ++i) {
            if (is_target(lex_results[i].unit)) {
                lex_target_rank = static_cast<int>(i + 1);
                break;
            }
        }

        // Semantic hits at depths 1, 5, 10, 20, 50, 100
        int sem_target_rank = -1;
        float sem_target_score = -1.0f;
        for (std::size_t i = 0; i < sem_results.size(); ++i) {
            if (is_target(sem_results[i].unit)) {
                sem_target_rank = static_cast<int>(i + 1);
                sem_target_score = static_cast<float>(sem_results[i].semantic_score);
                break;
            }
        }

        bool sem_hit_1 = (sem_target_rank >= 1 && sem_target_rank <= 1);
        bool sem_hit_5 = (sem_target_rank >= 1 && sem_target_rank <= 5);
        bool sem_hit_10 = (sem_target_rank >= 1 && sem_target_rank <= 10);
        bool sem_hit_20 = (sem_target_rank >= 1 && sem_target_rank <= 20);
        bool sem_hit_50 = (sem_target_rank >= 1 && sem_target_rank <= 50);
        bool sem_hit_100 = (sem_target_rank >= 1 && sem_target_rank <= 100);

        bool lex_hit_10 = (lex_target_rank >= 1 && lex_target_rank <= 10);

        // Hybrid hits
        int hyb_target_rank = -1;
        for (std::size_t i = 0; i < hyb_results.size(); ++i) {
            if (is_target(hyb_results[i].unit)) {
                hyb_target_rank = static_cast<int>(i + 1);
                break;
            }
        }
        bool hyb_hit_10 = (hyb_target_rank >= 1 && hyb_target_rank <= 10);

        // Semantic scores
        float top1_sem_score = sem_results.empty() ? 0.0f : static_cast<float>(sem_results[0].semantic_score);
        float top5_sem_score = (sem_results.size() >= 5) ? static_cast<float>(sem_results[4].semantic_score) : 0.0f;
        float top10_sem_score = (sem_results.size() >= 10) ? static_cast<float>(sem_results[9].semantic_score) : 0.0f;

        float score_margin = (sem_target_score >= 0.0f) ? (top1_sem_score - sem_target_score) : -1.0f;

        // Top 3 semantic text representations
        std::vector<std::string> top3_sem_texts;
        for (std::size_t i = 0; i < std::min<std::size_t>(3, sem_results.size()); ++i) {
            top3_sem_texts.push_back(amoeba::semantic::SemanticTextFormatter::format_unit(sem_results[i].unit));
        }

        // Expected unit text representation
        std::string expected_unit_text = "(not found in primary units)";
        for (const auto& u : pipeline->units()) {
            if (is_target(u)) {
                expected_unit_text = amoeba::semantic::SemanticTextFormatter::format_unit(u);
                break;
            }
        }

        // Write JSON entry
        ofs << "  {\n"
            << "    \"question_id\": \"" << escape_json(q.question_id) << "\",\n"
            << "    \"repo_name\": \"" << escape_json(q.repo_name) << "\",\n"
            << "    \"source\": \"" << escape_json(q.source) << "\",\n"
            << "    \"taxonomy\": \"" << escape_json(q.taxonomy) << "\",\n"
            << "    \"difficulty\": \"" << escape_json(q.difficulty) << "\",\n"
            << "    \"question_text\": \"" << escape_json(q.question_text) << "\",\n"
            << "    \"lex_hit_10\": " << (lex_hit_10 ? "true" : "false") << ",\n"
            << "    \"lex_target_rank\": " << lex_target_rank << ",\n"
            << "    \"sem_hit_1\": " << (sem_hit_1 ? "true" : "false") << ",\n"
            << "    \"sem_hit_5\": " << (sem_hit_5 ? "true" : "false") << ",\n"
            << "    \"sem_hit_10\": " << (sem_hit_10 ? "true" : "false") << ",\n"
            << "    \"sem_hit_20\": " << (sem_hit_20 ? "true" : "false") << ",\n"
            << "    \"sem_hit_50\": " << (sem_hit_50 ? "true" : "false") << ",\n"
            << "    \"sem_hit_100\": " << (sem_hit_100 ? "true" : "false") << ",\n"
            << "    \"sem_target_rank\": " << sem_target_rank << ",\n"
            << "    \"sem_target_score\": " << sem_target_score << ",\n"
            << "    \"hyb_hit_10\": " << (hyb_hit_10 ? "true" : "false") << ",\n"
            << "    \"hyb_target_rank\": " << hyb_target_rank << ",\n"
            << "    \"top1_sem_score\": " << top1_sem_score << ",\n"
            << "    \"top5_sem_score\": " << top5_sem_score << ",\n"
            << "    \"top10_sem_score\": " << top10_sem_score << ",\n"
            << "    \"score_margin\": " << score_margin << ",\n"
            << "    \"embed_ms\": " << embed_ms << ",\n"
            << "    \"total_sem_ms\": " << total_sem_ms << ",\n"
            << "    \"expected_unit_text\": \"" << escape_json(expected_unit_text) << "\",\n"
            << "    \"top1_sem_text\": \"" << (top3_sem_texts.empty() ? "" : escape_json(top3_sem_texts[0])) << "\",\n"
            << "    \"top2_sem_text\": \"" << (top3_sem_texts.size() > 1 ? escape_json(top3_sem_texts[1]) : "") << "\",\n"
            << "    \"top3_sem_text\": \"" << (top3_sem_texts.size() > 2 ? escape_json(top3_sem_texts[2]) : "") << "\"\n"
            << "  }" << ((q_idx + 1 < queries.size()) ? ",\n" : "\n");
    }

    ofs << "]\n";
    std::cout << "  Audit results saved to " << out_file.string() << "\n";
    return 0;
}
