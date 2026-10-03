// Amoeba — Phase 8.4 Hybrid Fusion Recalibration Experiment
//
// Sweeps fusion strategies for a single repository and emits a per-question
// TSV row so the Python orchestrator can aggregate across all 10 repos.
//
// Strategies tested:
//   alpha_0.0 ... alpha_1.0  (step 0.1) — fixed WeightedScore, adaptive_fusion=false
//   rrf_k10   rrf_k20   rrf_k60         — ReciprocalRank fusion
//   adaptive                             — current production: adaptive_fusion=true
//   lexical_only                         — pure lexical  (semantic_top_k = 0)
//   semantic_only                        — pure semantic (lexical_top_k  = 0)
//
// Output TSV columns (tab-separated):
//   question_id  repo_name  is_negative
//   rank_alpha_0.0 ... rank_alpha_1.0
//   rank_rrf_k10  rank_rrf_k20  rank_rrf_k60
//   rank_adaptive
//   rank_lexical_only  rank_semantic_only
//
// rank = 1-indexed hit position within top-50.
//       -1 = negative question sentinel (safety tracking; not a miss)
//        0 = positive question — target absent from top-50

#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/scanner/repository_scanner.hpp"
#include "amoeba/semantic/deterministic_embedding_provider.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---- Data types -------------------------------------------------------------

struct QueryItem {
    std::string question_id;
    std::string repo_name;
    std::string question_text;
    std::vector<std::string> expected_files;
    std::vector<std::string> expected_symbols;
};

// ---- Match helpers ----------------------------------------------------------

[[nodiscard]] bool path_matches(std::string_view retrieved, std::string_view expected) {
    if (retrieved.empty() || expected.empty()) return false;
    std::string r(retrieved), e(expected);
    std::replace(r.begin(), r.end(), '\\', '/');
    std::replace(e.begin(), e.end(), '\\', '/');
    if (r == e || r.ends_with(e) || e.ends_with(r)) return true;
    if (r.find(e) != std::string::npos || e.find(r) != std::string::npos) return true;
    return fs::path(r).filename() == fs::path(e).filename();
}

[[nodiscard]] bool symbol_matches(std::string_view retrieved, std::string_view expected) {
    if (retrieved.empty() || expected.empty()) return false;
    return retrieved == expected ||
           retrieved.find(expected) != std::string::npos ||
           expected.find(retrieved) != std::string::npos;
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

// ---- TSV loader — includes negatives (empty expected lists) ----------------

std::vector<QueryItem> load_tsv(const fs::path& tsv_path) {
    std::vector<QueryItem> queries;
    std::ifstream ifs(tsv_path);
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::stringstream ss(line);
        std::string qid, repo, src, tax, diff, qtext, files_str, syms_str;
        if (!std::getline(ss, qid,  '\t') || !std::getline(ss, repo,  '\t') ||
            !std::getline(ss, src,  '\t') || !std::getline(ss, tax,   '\t') ||
            !std::getline(ss, diff, '\t') || !std::getline(ss, qtext, '\t')) continue;
        std::getline(ss, files_str, '\t');
        std::getline(ss, syms_str,  '\t');

        QueryItem q;
        q.question_id    = qid;
        q.repo_name      = repo;
        q.question_text  = qtext;
        q.expected_files   = split_str(files_str, '|');
        q.expected_symbols = split_str(syms_str,  '|');
        queries.push_back(std::move(q));
    }
    return queries;
}

// ---- Rank evaluator ---------------------------------------------------------

[[nodiscard]] int evaluate_rank(
    const std::vector<amoeba::retrieval::PrimarySearchResult>& results,
    const QueryItem& q)
{
    for (std::size_t r = 0; r < results.size(); ++r) {
        const auto f   = results[r].unit.file_path.generic_string();
        const auto sym = results[r].unit.primary_element.name;
        for (const auto& ef : q.expected_files)
            if (path_matches(f, ef)) return static_cast<int>(r + 1);
        for (const auto& es : q.expected_symbols)
            if (symbol_matches(sym, es)) return static_cast<int>(r + 1);
    }
    return 0;
}

// ---- Strategy descriptor ----------------------------------------------------

struct Strategy {
    std::string name;
    amoeba::retrieval::PrimarySearchOptions opts;
};

[[nodiscard]] std::vector<Strategy> build_strategies() {
    std::vector<Strategy> strats;
    strats.reserve(18);

    // Fixed alpha sweep 0.0 -> 1.0 (step 0.1), WeightedScore, adaptive_fusion=false
    for (int i = 0; i <= 10; ++i) {
        const double alpha = i / 10.0;

        // Build name: "alpha_0.0", "alpha_0.1", ..., "alpha_1.0"
        const int int_part  = static_cast<int>(alpha);
        const int frac_part = static_cast<int>((alpha - int_part) * 10.0 + 0.5);
        std::string name = "alpha_" + std::to_string(int_part) + '.' + std::to_string(frac_part);

        amoeba::retrieval::PrimarySearchOptions opt;
        opt.max_results     = 50;
        opt.lexical_top_k   = 50;
        opt.semantic_top_k  = 50;
        opt.alpha           = alpha;
        opt.adaptive_fusion = false;
        opt.fusion_method   = amoeba::hybrid::FusionMethod::WeightedScore;

        strats.push_back({std::move(name), opt});
    }

    // RRF k = 10, 20, 60
    for (double k : {10.0, 20.0, 60.0}) {
        std::string name = "rrf_k" + std::to_string(static_cast<int>(k));

        amoeba::retrieval::PrimarySearchOptions opt;
        opt.max_results     = 50;
        opt.lexical_top_k   = 50;
        opt.semantic_top_k  = 50;
        opt.adaptive_fusion = false;
        opt.fusion_method   = amoeba::hybrid::FusionMethod::ReciprocalRank;
        opt.rrf_k           = k;

        strats.push_back({std::move(name), opt});
    }

    // Adaptive baseline — current production behaviour (alpha=0.5 + intent overrides)
    {
        amoeba::retrieval::PrimarySearchOptions opt;
        opt.max_results     = 50;
        opt.lexical_top_k   = 50;
        opt.semantic_top_k  = 50;
        opt.alpha           = 0.5;
        opt.adaptive_fusion = true;
        strats.push_back({"adaptive", opt});
    }

    // Pure lexical: semantic_top_k = 0 so SemanticRetriever is never invoked
    {
        amoeba::retrieval::PrimarySearchOptions opt;
        opt.max_results     = 50;
        opt.lexical_top_k   = 50;
        opt.semantic_top_k  = 0;
        opt.alpha           = 1.0;
        opt.adaptive_fusion = false;
        strats.push_back({"lexical_only", opt});
    }

    // Pure semantic: lexical_top_k = 0 so SearchEngine is never invoked
    {
        amoeba::retrieval::PrimarySearchOptions opt;
        opt.max_results     = 50;
        opt.lexical_top_k   = 0;
        opt.semantic_top_k  = 50;
        opt.alpha           = 0.0;
        opt.adaptive_fusion = false;
        strats.push_back({"semantic_only", opt});
    }

    return strats;
}

// ---- main -------------------------------------------------------------------

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: amoeba_test_fusion <repo_dir> <tsv_file> <output_tsv_file>\n\n"
                  << "  repo_dir        - path to a single benchmark repository\n"
                  << "  tsv_file        - query TSV (same format as retrieval_benchmark)\n"
                  << "  output_tsv_file - destination for per-question rank TSV\n";
        return 1;
    }

    const fs::path repo_dir = argv[1];
    const fs::path tsv_file = argv[2];
    const fs::path out_file = argv[3];

    if (!fs::exists(repo_dir) || !fs::exists(tsv_file)) {
        std::cerr << "ERROR: repo_dir or tsv_file does not exist.\n";
        return 1;
    }

    auto queries = load_tsv(tsv_file);
    if (queries.empty()) {
        std::cerr << "ERROR: No queries loaded from " << tsv_file << "\n";
        return 1;
    }

    std::cerr << "[amoeba_test_fusion] " << repo_dir.filename().string()
              << " - " << queries.size() << " queries\n";

    // Build index + pipeline
    amoeba::semantic::DeterministicEmbeddingProvider embedding_provider{32};
    amoeba::parser::SourceParser parser;
    amoeba::scanner::RepositoryScanner scanner;

    const auto scan_res = scanner.scan(repo_dir);
    amoeba::index::InvertedIndex index;
    std::vector<amoeba::parser::ParsedFile> parsed_files;

    for (const auto& f : scan_res.files) {
        auto pf = parser.parse_file(f.path);
        if (pf.success) {
            index.add_parsed_file(pf);
            parsed_files.push_back(std::move(pf));
        }
    }

    auto pipeline = std::make_unique<amoeba::retrieval::PrimaryRetrievalPipeline>(
        parsed_files, index, embedding_provider);

    const auto strategies = build_strategies();

    // Write TSV header
    std::ofstream ofs(out_file);
    if (!ofs.is_open()) {
        std::cerr << "ERROR: Cannot open output file " << out_file << "\n";
        return 1;
    }

    ofs << "question_id\trepo_name\tis_negative";
    for (const auto& s : strategies) {
        ofs << '\t' << s.name;
    }
    ofs << '\n';

    // Evaluate every question across every strategy
    for (const auto& q : queries) {
        const bool is_neg = q.expected_files.empty() && q.expected_symbols.empty();
        ofs << q.question_id << '\t' << q.repo_name << '\t' << (is_neg ? "1" : "0");

        for (const auto& s : strategies) {
            const auto results = pipeline->search(q.question_text, s.opts);
            // -1 = negative sentinel; 0 = positive miss; 1..50 = hit rank
            const int rank = is_neg ? -1 : evaluate_rank(results, q);
            ofs << '\t' << rank;
        }
        ofs << '\n';
    }

    std::cerr << "[amoeba_test_fusion] Done: " << repo_dir.filename().string() << "\n";
    return 0;
}
