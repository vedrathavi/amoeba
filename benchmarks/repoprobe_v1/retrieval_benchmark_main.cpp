// Amoeba — Phase 8.3 Fast Retrieval-Only Benchmark
//
// Evaluates retrieval recall without LLM reasoning or evidence assembly:
// - Query understanding + Lexical + Semantic + Hybrid Fusion
// - Top-1, Top-5, Top-10, Top-20, Top-50 Hit Rates, MRR, Target Rank
// - Measures detailed latencies (QueryUnderstanding, Lexical, Semantic, Fusion)
// - Evaluates across Lexical-Only, Semantic-Only, and Hybrid modes

#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/retrieval/query_understanding.hpp"
#include "amoeba/scanner/repository_scanner.hpp"
#include "amoeba/semantic/deterministic_embedding_provider.hpp"

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

struct ModeResult {
    bool hit_at_1{false};
    bool hit_at_5{false};
    bool hit_at_10{false};
    bool hit_at_20{false};
    bool hit_at_50{false};
    int target_rank{0}; // 1-indexed, 0 if not found in top 50
    double mrr{0.0};
    std::vector<std::string> top_candidates;
};

struct RetrievalQuestionResult {
    std::string question_id;
    std::string repo_name;
    std::string taxonomy;
    std::string difficulty;
    bool is_negative{false};

    // Query analysis
    std::string clean_query;
    std::vector<std::string> technical_entities;
    std::vector<std::string> search_terms;
    std::vector<std::string> subject_terms;

    // Latency
    double query_understanding_ms{0.0};
    double lexical_ms{0.0};
    double semantic_ms{0.0};
    double fusion_ms{0.0};
    double total_retrieval_ms{0.0};

    // Modes
    ModeResult hybrid;
    ModeResult lexical_only;
    ModeResult semantic_only;

    // Detailed Top-10 retrieved items
    struct CandidateDetail {
        std::size_t rank{0};
        std::string file_path;
        std::string symbol_name;
        std::string kind;
        double lexical_score{0.0};
        double semantic_score{0.0};
        double hybrid_score{0.0};
        std::string provenance;
        bool is_match{false};
    };
    std::vector<CandidateDetail> top_10_details;
};

ModeResult evaluate_search_results(
    const std::vector<amoeba::retrieval::PrimarySearchResult>& results,
    const std::vector<std::string>& expected_files,
    const std::vector<std::string>& expected_symbols) {
    ModeResult mr;
    for (std::size_t rank = 0; rank < results.size(); ++rank) {
        const auto& sr = results[rank];
        const auto f_str = sr.unit.file_path.generic_string();
        const auto sym_str = sr.unit.primary_element.name;

        bool hit = false;
        for (const auto& ef : expected_files) {
            if (path_matches(f_str, ef)) { hit = true; break; }
        }
        if (!hit) {
            for (const auto& es : expected_symbols) {
                if (symbol_matches(sym_str, es)) { hit = true; break; }
            }
        }

        if (rank < 5) {
            mr.top_candidates.push_back(f_str + ":" + sym_str);
        }

        if (hit) {
            if (mr.target_rank == 0) {
                mr.target_rank = static_cast<int>(rank + 1);
                mr.mrr = 1.0 / static_cast<double>(rank + 1);
            }
            if (rank == 0) mr.hit_at_1 = true;
            if (rank < 5) mr.hit_at_5 = true;
            if (rank < 10) mr.hit_at_10 = true;
            if (rank < 20) mr.hit_at_20 = true;
            if (rank < 50) mr.hit_at_50 = true;
        }
    }
    return mr;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: amoeba_retrieval_benchmark <repo_dir> <queries_tsv_file> <output_json_file>\n";
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
    std::cout << "Running Phase 8.3 Retrieval Benchmark on " << repo_dir.filename().string()
              << " (" << queries.size() << " queries)...\n";

    amoeba::semantic::DeterministicEmbeddingProvider embedding_provider{32};
    amoeba::parser::SourceParser parser;
    amoeba::scanner::RepositoryScanner scanner;

    // 1. Scan and Parse repository
    auto scan_res = scanner.scan(repo_dir);
    amoeba::index::InvertedIndex index;
    std::vector<amoeba::parser::ParsedFile> parsed_files;

    for (const auto& f : scan_res.files) {
        auto pf = parser.parse_file(f.path);
        if (pf.success) {
            index.add_parsed_file(pf);
            parsed_files.push_back(std::move(pf));
        }
    }

    // 2. Initialize primary retrieval pipeline
    auto pipeline = std::make_unique<amoeba::retrieval::PrimaryRetrievalPipeline>(
        parsed_files, index, embedding_provider);

    std::vector<RetrievalQuestionResult> all_results;

    for (const auto& q : queries) {
        RetrievalQuestionResult qr;
        qr.question_id = q.question_id;
        qr.repo_name = q.repo_name;
        qr.taxonomy = q.taxonomy;
        qr.difficulty = q.difficulty;
        qr.is_negative = q.expected_files.empty() && q.expected_symbols.empty();

        // 1. Query Understanding Analysis & Latency
        auto t_qu_s = Clock::now();
        auto query_rep = amoeba::retrieval::QueryUnderstanding::analyze(q.question_text);
        qr.query_understanding_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - t_qu_s).count();

        qr.clean_query = query_rep.clean_technical_query;
        qr.technical_entities = query_rep.technical_entities;
        qr.search_terms = query_rep.all_search_terms;
        qr.subject_terms = query_rep.subject_terms;

        // 2. Hybrid Retrieval (Top 50)
        amoeba::retrieval::PrimarySearchOptions hybrid_opts;
        hybrid_opts.max_results = 50;
        hybrid_opts.lexical_top_k = 50;
        hybrid_opts.semantic_top_k = 50;
        hybrid_opts.alpha = 0.5;
        hybrid_opts.adaptive_fusion = true;

        amoeba::retrieval::PipelineMetrics metrics;
        auto hybrid_results = pipeline->search_with_metrics(q.question_text, hybrid_opts, metrics);

        qr.lexical_ms = metrics.lexical_ms;
        qr.semantic_ms = metrics.semantic_ms;
        qr.fusion_ms = metrics.fusion_ms;
        qr.total_retrieval_ms = metrics.total_ms;

        qr.hybrid = evaluate_search_results(hybrid_results, q.expected_files, q.expected_symbols);

        // Record Top 10 candidate details
        for (std::size_t rank = 0; rank < std::min<std::size_t>(10, hybrid_results.size()); ++rank) {
            const auto& sr = hybrid_results[rank];
            const auto f_str = sr.unit.file_path.generic_string();
            const auto sym_str = sr.unit.primary_element.name;

            bool hit = false;
            for (const auto& ef : q.expected_files) {
                if (path_matches(f_str, ef)) { hit = true; break; }
            }
            if (!hit) {
                for (const auto& es : q.expected_symbols) {
                    if (symbol_matches(sym_str, es)) { hit = true; break; }
                }
            }

            std::string prov = (sr.provenance == amoeba::retrieval::RetrievalProvenance::HybridBoth) ? "Both"
                             : (sr.provenance == amoeba::retrieval::RetrievalProvenance::LexicalOnly) ? "LexicalOnly"
                             : "SemanticOnly";

            qr.top_10_details.push_back({
                .rank = rank + 1,
                .file_path = f_str,
                .symbol_name = sym_str,
                .kind = std::string(amoeba::parser::to_string(sr.unit.primary_element.kind)),
                .lexical_score = sr.lexical_score,
                .semantic_score = sr.semantic_score,
                .hybrid_score = sr.hybrid_score,
                .provenance = prov,
                .is_match = hit,
            });
        }

        // 3. Lexical Only (alpha = 1.0)
        amoeba::retrieval::PrimarySearchOptions lex_opts;
        lex_opts.max_results = 50;
        lex_opts.lexical_top_k = 50;
        lex_opts.semantic_top_k = 0;
        lex_opts.alpha = 1.0;
        lex_opts.adaptive_fusion = false;
        auto lex_results = pipeline->search(q.question_text, lex_opts);
        qr.lexical_only = evaluate_search_results(lex_results, q.expected_files, q.expected_symbols);

        // 4. Semantic Only (alpha = 0.0)
        amoeba::retrieval::PrimarySearchOptions sem_opts;
        sem_opts.max_results = 50;
        sem_opts.lexical_top_k = 0;
        sem_opts.semantic_top_k = 50;
        sem_opts.alpha = 0.0;
        sem_opts.adaptive_fusion = false;
        auto sem_results = pipeline->search(q.question_text, sem_opts);
        qr.semantic_only = evaluate_search_results(sem_results, q.expected_files, q.expected_symbols);

        all_results.push_back(std::move(qr));
    }

    // Output JSON
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
        ofs << "    \"clean_query\": \"" << escape_json(r.clean_query) << "\",\n";
        
        ofs << "    \"technical_entities\": [";
        for (size_t k = 0; k < r.technical_entities.size(); ++k) {
            if (k > 0) ofs << ", ";
            ofs << "\"" << escape_json(r.technical_entities[k]) << "\"";
        }
        ofs << "],\n";

        ofs << "    \"query_understanding_ms\": " << r.query_understanding_ms << ",\n";
        ofs << "    \"lexical_ms\": " << r.lexical_ms << ",\n";
        ofs << "    \"semantic_ms\": " << r.semantic_ms << ",\n";
        ofs << "    \"fusion_ms\": " << r.fusion_ms << ",\n";
        ofs << "    \"total_retrieval_ms\": " << r.total_retrieval_ms << ",\n";

        auto write_mode = [&](std::string_view name, const ModeResult& m) {
            ofs << "    \"" << name << "\": {\n";
            ofs << "      \"hit_at_1\": " << (m.hit_at_1 ? "true" : "false") << ",\n";
            ofs << "      \"hit_at_5\": " << (m.hit_at_5 ? "true" : "false") << ",\n";
            ofs << "      \"hit_at_10\": " << (m.hit_at_10 ? "true" : "false") << ",\n";
            ofs << "      \"hit_at_20\": " << (m.hit_at_20 ? "true" : "false") << ",\n";
            ofs << "      \"hit_at_50\": " << (m.hit_at_50 ? "true" : "false") << ",\n";
            ofs << "      \"target_rank\": " << m.target_rank << ",\n";
            ofs << "      \"mrr\": " << m.mrr << "\n";
            ofs << "    }";
        };

        write_mode("hybrid", r.hybrid); ofs << ",\n";
        write_mode("lexical_only", r.lexical_only); ofs << ",\n";
        write_mode("semantic_only", r.semantic_only); ofs << ",\n";

        ofs << "    \"top_10_details\": [\n";
        for (size_t d = 0; d < r.top_10_details.size(); ++d) {
            const auto& det = r.top_10_details[d];
            ofs << "      {\n";
            ofs << "        \"rank\": " << det.rank << ",\n";
            ofs << "        \"file_path\": \"" << escape_json(det.file_path) << "\",\n";
            ofs << "        \"symbol_name\": \"" << escape_json(det.symbol_name) << "\",\n";
            ofs << "        \"kind\": \"" << escape_json(det.kind) << "\",\n";
            ofs << "        \"lexical_score\": " << det.lexical_score << ",\n";
            ofs << "        \"semantic_score\": " << det.semantic_score << ",\n";
            ofs << "        \"hybrid_score\": " << det.hybrid_score << ",\n";
            ofs << "        \"provenance\": \"" << escape_json(det.provenance) << "\",\n";
            ofs << "        \"is_match\": " << (det.is_match ? "true" : "false") << "\n";
            ofs << "      }" << (d + 1 < r.top_10_details.size() ? ",\n" : "\n");
        }
        ofs << "    ]\n";

        ofs << "  }" << (i + 1 < all_results.size() ? ",\n" : "\n");
    }
    ofs << "]\n";

    std::cout << "Completed retrieval benchmark for " << repo_dir.filename().string() << ".\n";
    return 0;
}
