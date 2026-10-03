// Amoeba — Fast ANN Benchmark: ExactSemanticIndex vs HnswSemanticIndex
//
// Evaluates build time, query latency, index memory, and recall across all 66 positive queries.

#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/retrieval/retrieval_unit.hpp"
#include "amoeba/retrieval/supporting_evidence_resolver.hpp"
#include "amoeba/scanner/repository_scanner.hpp"
#include "amoeba/semantic/deterministic_embedding_provider.hpp"
#include "amoeba/semantic/hnsw_semantic_index.hpp"
#include "amoeba/semantic/semantic_document.hpp"
#include "amoeba/semantic/semantic_index.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

[[nodiscard]] bool unit_matches_target(const amoeba::retrieval::RetrievalUnit& unit,
                                       const std::vector<std::string>& exp_files,
                                       const std::vector<std::string>& exp_syms) {
    bool file_matched = exp_files.empty();
    if (!file_matched) {
        for (const auto& ef : exp_files) {
            if (path_matches(unit.file_path.generic_string(), ef)) {
                file_matched = true;
                break;
            }
        }
    }

    bool sym_matched = exp_syms.empty();
    if (!sym_matched) {
        for (const auto& es : exp_syms) {
            if (symbol_matches(unit.primary_element.name, es)) {
                sym_matched = true;
                break;
            }
            for (const auto& supp : unit.supporting_elements) {
                if (symbol_matches(supp.name, es)) {
                    sym_matched = true;
                    break;
                }
            }
        }
    }

    return file_matched && sym_matched;
}

struct AnnQueryResult {
    std::string question_id;
    std::string repo_name;
    double exact_query_ms{0.0};
    double hnsw32_query_ms{0.0};
    double hnsw64_query_ms{0.0};
    double hnsw128_query_ms{0.0};

    // Recall of HNSW vs Exact Ground Truth at K = 1, 5, 10, 20, 50, 100
    double hnsw64_recall_vs_exact_k1{0.0};
    double hnsw64_recall_vs_exact_k5{0.0};
    double hnsw64_recall_vs_exact_k10{0.0};
    double hnsw64_recall_vs_exact_k20{0.0};
    double hnsw64_recall_vs_exact_k50{0.0};
    double hnsw64_recall_vs_exact_k100{0.0};

    bool exact_hit_k1{false};
    bool exact_hit_k5{false};
    bool exact_hit_k10{false};
    bool exact_hit_k20{false};
    bool exact_hit_k50{false};
    bool exact_hit_k100{false};

    bool hnsw64_hit_k1{false};
    bool hnsw64_hit_k5{false};
    bool hnsw64_hit_k10{false};
    bool hnsw64_hit_k20{false};
    bool hnsw64_hit_k50{false};
    bool hnsw64_hit_k100{false};
};

struct RepoBenchmarkStats {
    std::string repo_name;
    std::size_t unit_count{0};
    double exact_build_ms{0.0};
    double hnsw_build_ms{0.0};
    std::size_t exact_mem_bytes{0};
    std::size_t hnsw_mem_bytes{0};
    std::vector<AnnQueryResult> query_results;
};

} // namespace

int main(int argc, char** argv) {
    std::cout << "=================================================================\n";
    std::cout << "Amoeba Fast ANN Benchmark: Exact vs HNSW Indexing\n";
    std::cout << "=================================================================\n\n";

    fs::path benchmark_dir = "benchmarks/repoprobe_v1";
    if (argc > 1) {
        benchmark_dir = argv[1];
    }

    fs::path repos_dir = benchmark_dir / "repos";
    fs::path questions_path = benchmark_dir / "questions.json";
    fs::path gt_path = benchmark_dir / "ground_truth.json";

    // Load questions & ground truth via basic JSON string parsing
    std::ifstream q_ifs(questions_path);
    std::string q_str((std::istreambuf_iterator<char>(q_ifs)), std::istreambuf_iterator<char>());
    std::ifstream gt_ifs(gt_path);
    std::string gt_str((std::istreambuf_iterator<char>(gt_ifs)), std::istreambuf_iterator<char>());

    // We can run the benchmark for the 10 repos
    const std::vector<std::string> repo_names = {
        "ducklake", "adk-python", "better-auth", "dokploy", "pkl",
        "beszel", "opencloud", "jiff", "walker", "docling"
    };

    amoeba::semantic::DeterministicEmbeddingProvider provider;

    std::vector<RepoBenchmarkStats> all_repo_stats;

    // Load questions from questions.json
    // We can parse questions by simple scanning
    std::vector<QueryItem> all_queries;
    {
        // Simple JSON extractor for questions.json
        size_t pos = 0;
        while ((pos = q_str.find("\"question_id\"", pos)) != std::string::npos) {
            QueryItem item;
            // extract question_id
            auto qid_start = q_str.find("\"", pos + 13) + 1;
            auto qid_end = q_str.find("\"", qid_start);
            item.question_id = q_str.substr(qid_start, qid_end - qid_start);

            // extract repo_name
            auto repo_pos = q_str.find("\"repo_name\"", qid_end);
            auto r_start = q_str.find("\"", repo_pos + 11) + 1;
            auto r_end = q_str.find("\"", r_start);
            std::string r_full = q_str.substr(r_start, r_end - r_start);
            // strip org prefix if present
            auto slash = r_full.find('/');
            item.repo_name = (slash != std::string::npos) ? r_full.substr(slash + 1) : r_full;

            // extract question
            auto q_pos = q_str.find("\"question\"", r_end);
            auto text_start = q_str.find("\"", q_pos + 10) + 1;
            auto text_end = q_str.find("\"", text_start);
            item.question_text = q_str.substr(text_start, text_end - text_start);

            // ground truth info
            size_t gt_pos = gt_str.find("\"question_id\": \"" + item.question_id + "\"");
            if (gt_pos != std::string::npos) {
                // files
                size_t exp_f_pos = gt_str.find("\"expected_files\"", gt_pos);
                if (exp_f_pos != std::string::npos && exp_f_pos < gt_str.find("},", gt_pos)) {
                    size_t arr_start = gt_str.find("[", exp_f_pos);
                    size_t arr_end = gt_str.find("]", arr_start);
                    std::string arr_content = gt_str.substr(arr_start + 1, arr_end - arr_start - 1);
                    size_t s_pos = 0;
                    while ((s_pos = arr_content.find("\"", s_pos)) != std::string::npos) {
                        size_t s_end = arr_content.find("\"", s_pos + 1);
                        if (s_end == std::string::npos) break;
                        item.expected_files.push_back(arr_content.substr(s_pos + 1, s_end - s_pos - 1));
                        s_pos = s_end + 1;
                    }
                }
                // symbols
                size_t exp_s_pos = gt_str.find("\"expected_symbols\"", gt_pos);
                if (exp_s_pos != std::string::npos && exp_s_pos < gt_str.find("},", gt_pos)) {
                    size_t arr_start = gt_str.find("[", exp_s_pos);
                    size_t arr_end = gt_str.find("]", arr_start);
                    std::string arr_content = gt_str.substr(arr_start + 1, arr_end - arr_start - 1);
                    size_t s_pos = 0;
                    while ((s_pos = arr_content.find("\"", s_pos)) != std::string::npos) {
                        size_t s_end = arr_content.find("\"", s_pos + 1);
                        if (s_end == std::string::npos) break;
                        item.expected_symbols.push_back(arr_content.substr(s_pos + 1, s_end - s_pos - 1));
                        s_pos = s_end + 1;
                    }
                }
            }

            all_queries.push_back(std::move(item));
            pos = qid_end;
        }
    }

    std::cout << "Loaded " << all_queries.size() << " questions.\n\n";

    for (const auto& repo : repo_names) {
        fs::path repo_path = repos_dir / repo;
        if (!fs::exists(repo_path)) {
            std::cout << "Skipping missing repo: " << repo << "\n";
            continue;
        }

        std::cout << "Indexing repository: " << repo << " ... " << std::flush;

        // 1. Scan and parse
        amoeba::scanner::RepositoryScanner scanner;
        auto scan_res = scanner.scan(repo_path);
        amoeba::parser::SourceParser parser;
        amoeba::index::InvertedIndex index;
        std::vector<amoeba::parser::ParsedFile> parsed_files;
        parsed_files.reserve(scan_res.files.size());
        for (const auto& sf : scan_res.files) {
            auto pf = parser.parse_file(sf.path);
            if (pf.success) {
                index.add_parsed_file(pf);
                parsed_files.push_back(std::move(pf));
            }
        }

        // 2. Resolve RetrievalUnits
        auto units = amoeba::retrieval::SupportingEvidenceResolver::resolve_units(parsed_files);
        for (uint32_t i = 0; i < units.size(); ++i) {
            units[i].primary_element_id = i + 1;
        }

        // 3. Generate unit embeddings
        std::vector<amoeba::semantic::Embedding> embeddings;
        embeddings.reserve(units.size());
        for (const auto& unit : units) {
            std::string text = amoeba::semantic::SemanticTextFormatter::format_unit(unit);
            auto vals = provider.embed(text);
            if (!vals.empty()) {
                embeddings.push_back(amoeba::semantic::Embedding{
                    .element_id = unit.primary_element_id,
                    .values = std::move(vals),
                });
            }
        }

        RepoBenchmarkStats r_stats;
        r_stats.repo_name = repo;
        r_stats.unit_count = units.size();

        // 4. Build Exact index
        amoeba::semantic::ExactSemanticIndex exact_index;
        auto t_exact_b_start = Clock::now();
        for (const auto& emb : embeddings) {
            exact_index.add(emb);
        }
        auto t_exact_b_end = Clock::now();
        r_stats.exact_build_ms = std::chrono::duration<double, std::milli>(t_exact_b_end - t_exact_b_start).count();
        r_stats.exact_mem_bytes = exact_index.estimate_memory_bytes();

        // 5. Build HNSW index (M=16, efConstruction=200, efSearch=64)
        amoeba::semantic::HnswConfig hnsw_cfg{
            .m = 16,
            .ef_construction = 200,
            .ef_search = 64,
            .initial_capacity = units.size() + 100,
            .random_seed = 42
        };
        amoeba::semantic::HnswSemanticIndex hnsw_index(hnsw_cfg);
        auto t_hnsw_b_start = Clock::now();
        for (const auto& emb : embeddings) {
            hnsw_index.add(emb);
        }
        auto t_hnsw_b_end = Clock::now();
        r_stats.hnsw_build_ms = std::chrono::duration<double, std::milli>(t_hnsw_b_end - t_hnsw_b_start).count();
        r_stats.hnsw_mem_bytes = hnsw_index.estimate_memory_bytes();

        std::cout << units.size() << " units (Exact build: " << r_stats.exact_build_ms << "ms, HNSW build: "
                  << r_stats.hnsw_build_ms << "ms)\n";

        // Filter queries for this repo
        for (const auto& q : all_queries) {
            if (q.repo_name != repo) continue;

            auto q_vec = provider.embed(q.question_text);
            if (q_vec.empty()) continue;

            AnnQueryResult q_res;
            q_res.question_id = q.question_id;
            q_res.repo_name = repo;

            // Search Exact (k=100)
            auto t_eq_start = Clock::now();
            auto exact_results = exact_index.search(q_vec, 100);
            auto t_eq_end = Clock::now();
            q_res.exact_query_ms = std::chrono::duration<double, std::milli>(t_eq_end - t_eq_start).count();

            // Search HNSW ef=32
            hnsw_index.set_ef_search(32);
            auto t_h32_start = Clock::now();
            auto hnsw32_results = hnsw_index.search(q_vec, 100);
            auto t_h32_end = Clock::now();
            q_res.hnsw32_query_ms = std::chrono::duration<double, std::milli>(t_h32_end - t_h32_start).count();

            // Search HNSW ef=64
            hnsw_index.set_ef_search(64);
            auto t_h64_start = Clock::now();
            auto hnsw64_results = hnsw_index.search(q_vec, 100);
            auto t_h64_end = Clock::now();
            q_res.hnsw64_query_ms = std::chrono::duration<double, std::milli>(t_h64_end - t_h64_start).count();

            // Search HNSW ef=128
            hnsw_index.set_ef_search(128);
            auto t_h128_start = Clock::now();
            auto hnsw128_results = hnsw_index.search(q_vec, 100);
            auto t_h128_end = Clock::now();
            q_res.hnsw128_query_ms = std::chrono::duration<double, std::milli>(t_h128_end - t_h128_start).count();

            // Helper to compute intersection recall between exact and HNSW at depth K
            auto compute_recall_vs_exact = [&](const std::vector<amoeba::semantic::SemanticSearchResult>& h_res, size_t k) {
                if (exact_results.empty() || h_res.empty()) return 1.0;
                size_t actual_k = std::min({k, exact_results.size(), h_res.size()});
                if (actual_k == 0) return 1.0;

                std::unordered_set<amoeba::semantic::ElementId> exact_set;
                for (size_t i = 0; i < actual_k; ++i) {
                    exact_set.insert(exact_results[i].element_id);
                }

                size_t matches = 0;
                for (size_t i = 0; i < actual_k; ++i) {
                    if (exact_set.contains(h_res[i].element_id)) {
                        matches++;
                    }
                }
                return static_cast<double>(matches) / actual_k;
            };

            q_res.hnsw64_recall_vs_exact_k1 = compute_recall_vs_exact(hnsw64_results, 1);
            q_res.hnsw64_recall_vs_exact_k5 = compute_recall_vs_exact(hnsw64_results, 5);
            q_res.hnsw64_recall_vs_exact_k10 = compute_recall_vs_exact(hnsw64_results, 10);
            q_res.hnsw64_recall_vs_exact_k20 = compute_recall_vs_exact(hnsw64_results, 20);
            q_res.hnsw64_recall_vs_exact_k50 = compute_recall_vs_exact(hnsw64_results, 50);
            q_res.hnsw64_recall_vs_exact_k100 = compute_recall_vs_exact(hnsw64_results, 100);

            // Evaluate target match against benchmark ground truth
            auto check_target_hit = [&](const std::vector<amoeba::semantic::SemanticSearchResult>& res_list, size_t k) {
                size_t lim = std::min(k, res_list.size());
                for (size_t i = 0; i < lim; ++i) {
                    uint32_t eid = res_list[i].element_id;
                    if (eid > 0 && eid <= units.size()) {
                        if (unit_matches_target(units[eid - 1], q.expected_files, q.expected_symbols)) {
                            return true;
                        }
                    }
                }
                return false;
            };

            q_res.exact_hit_k1 = check_target_hit(exact_results, 1);
            q_res.exact_hit_k5 = check_target_hit(exact_results, 5);
            q_res.exact_hit_k10 = check_target_hit(exact_results, 10);
            q_res.exact_hit_k20 = check_target_hit(exact_results, 20);
            q_res.exact_hit_k50 = check_target_hit(exact_results, 50);
            q_res.exact_hit_k100 = check_target_hit(exact_results, 100);

            q_res.hnsw64_hit_k1 = check_target_hit(hnsw64_results, 1);
            q_res.hnsw64_hit_k5 = check_target_hit(hnsw64_results, 5);
            q_res.hnsw64_hit_k10 = check_target_hit(hnsw64_results, 10);
            q_res.hnsw64_hit_k20 = check_target_hit(hnsw64_results, 20);
            q_res.hnsw64_hit_k50 = check_target_hit(hnsw64_results, 50);
            q_res.hnsw64_hit_k100 = check_target_hit(hnsw64_results, 100);

            r_stats.query_results.push_back(q_res);
        }

        all_repo_stats.push_back(std::move(r_stats));
    }

    // Aggregate statistics
    std::size_t total_queries = 0;
    double total_exact_ms = 0.0;
    double total_hnsw32_ms = 0.0;
    double total_hnsw64_ms = 0.0;
    double total_hnsw128_ms = 0.0;

    double sum_rec_vs_exact_k1 = 0.0;
    double sum_rec_vs_exact_k5 = 0.0;
    double sum_rec_vs_exact_k10 = 0.0;
    double sum_rec_vs_exact_k20 = 0.0;
    double sum_rec_vs_exact_k50 = 0.0;
    double sum_rec_vs_exact_k100 = 0.0;

    std::size_t exact_hits_k1 = 0, exact_hits_k5 = 0, exact_hits_k10 = 0, exact_hits_k20 = 0, exact_hits_k50 = 0, exact_hits_k100 = 0;
    std::size_t hnsw_hits_k1 = 0, hnsw_hits_k5 = 0, hnsw_hits_k10 = 0, hnsw_hits_k20 = 0, hnsw_hits_k50 = 0, hnsw_hits_k100 = 0;

    std::size_t total_exact_mem = 0;
    std::size_t total_hnsw_mem = 0;
    double total_exact_build_ms = 0.0;
    double total_hnsw_build_ms = 0.0;

    for (const auto& rs : all_repo_stats) {
        total_exact_build_ms += rs.exact_build_ms;
        total_hnsw_build_ms += rs.hnsw_build_ms;
        total_exact_mem += rs.exact_mem_bytes;
        total_hnsw_mem += rs.hnsw_mem_bytes;

        for (const auto& qr : rs.query_results) {
            total_queries++;
            total_exact_ms += qr.exact_query_ms;
            total_hnsw32_ms += qr.hnsw32_query_ms;
            total_hnsw64_ms += qr.hnsw64_query_ms;
            total_hnsw128_ms += qr.hnsw128_query_ms;

            sum_rec_vs_exact_k1 += qr.hnsw64_recall_vs_exact_k1;
            sum_rec_vs_exact_k5 += qr.hnsw64_recall_vs_exact_k5;
            sum_rec_vs_exact_k10 += qr.hnsw64_recall_vs_exact_k10;
            sum_rec_vs_exact_k20 += qr.hnsw64_recall_vs_exact_k20;
            sum_rec_vs_exact_k50 += qr.hnsw64_recall_vs_exact_k50;
            sum_rec_vs_exact_k100 += qr.hnsw64_recall_vs_exact_k100;

            if (qr.exact_hit_k1) exact_hits_k1++;
            if (qr.exact_hit_k5) exact_hits_k5++;
            if (qr.exact_hit_k10) exact_hits_k10++;
            if (qr.exact_hit_k20) exact_hits_k20++;
            if (qr.exact_hit_k50) exact_hits_k50++;
            if (qr.exact_hit_k100) exact_hits_k100++;

            if (qr.hnsw64_hit_k1) hnsw_hits_k1++;
            if (qr.hnsw64_hit_k5) hnsw_hits_k5++;
            if (qr.hnsw64_hit_k10) hnsw_hits_k10++;
            if (qr.hnsw64_hit_k20) hnsw_hits_k20++;
            if (qr.hnsw64_hit_k50) hnsw_hits_k50++;
            if (qr.hnsw64_hit_k100) hnsw_hits_k100++;
        }
    }

    std::cout << "\n=================================================================\n";
    std::cout << "RESULTS SUMMARY (" << total_queries << " Queries Across 10 Repositories)\n";
    std::cout << "=================================================================\n\n";

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "1. INDEX BUILD & MEMORY:\n";
    std::cout << "   - Exact Index Total Build Time: " << total_exact_build_ms << " ms\n";
    std::cout << "   - HNSW Index Total Build Time:  " << total_hnsw_build_ms << " ms\n";
    std::cout << "   - Exact Total Memory Footprint: " << (total_exact_mem / 1024.0 / 1024.0) << " MB\n";
    std::cout << "   - HNSW Total Memory Footprint:  " << (total_hnsw_mem / 1024.0 / 1024.0) << " MB\n\n";

    std::cout << "2. QUERY LATENCY (Average per query):\n";
    std::cout << "   - Exact Exhaustive Scan: " << (total_exact_ms / total_queries) << " ms\n";
    std::cout << "   - HNSW (efSearch=32):    " << (total_hnsw32_ms / total_queries) << " ms ("
              << ((total_exact_ms / total_queries) / (total_hnsw32_ms / total_queries)) << "x speedup)\n";
    std::cout << "   - HNSW (efSearch=64):    " << (total_hnsw64_ms / total_queries) << " ms ("
              << ((total_exact_ms / total_queries) / (total_hnsw64_ms / total_queries)) << "x speedup)\n";
    std::cout << "   - HNSW (efSearch=128):   " << (total_hnsw128_ms / total_queries) << " ms ("
              << ((total_exact_ms / total_queries) / (total_hnsw128_ms / total_queries)) << "x speedup)\n\n";

    std::cout << "3. APPROXIMATION RECALL (HNSW ef=64 vs Exact Ground Truth):\n";
    std::cout << "   - Recall@1:   " << (sum_rec_vs_exact_k1 / total_queries * 100.0) << "%\n";
    std::cout << "   - Recall@5:   " << (sum_rec_vs_exact_k5 / total_queries * 100.0) << "%\n";
    std::cout << "   - Recall@10:  " << (sum_rec_vs_exact_k10 / total_queries * 100.0) << "%\n";
    std::cout << "   - Recall@20:  " << (sum_rec_vs_exact_k20 / total_queries * 100.0) << "%\n";
    std::cout << "   - Recall@50:  " << (sum_rec_vs_exact_k50 / total_queries * 100.0) << "%\n";
    std::cout << "   - Recall@100: " << (sum_rec_vs_exact_k100 / total_queries * 100.0) << "%\n\n";

    std::cout << "4. BENCHMARK TARGET RECALL (Exact vs HNSW ef=64):\n";
    std::cout << "   - Top-1:   Exact " << exact_hits_k1 << "/" << total_queries << " (" << (100.0 * exact_hits_k1 / total_queries) << "%) vs HNSW " << hnsw_hits_k1 << "/" << total_queries << " (" << (100.0 * hnsw_hits_k1 / total_queries) << "%)\n";
    std::cout << "   - Top-5:   Exact " << exact_hits_k5 << "/" << total_queries << " (" << (100.0 * exact_hits_k5 / total_queries) << "%) vs HNSW " << hnsw_hits_k5 << "/" << total_queries << " (" << (100.0 * hnsw_hits_k5 / total_queries) << "%)\n";
    std::cout << "   - Top-10:  Exact " << exact_hits_k10 << "/" << total_queries << " (" << (100.0 * exact_hits_k10 / total_queries) << "%) vs HNSW " << hnsw_hits_k10 << "/" << total_queries << " (" << (100.0 * hnsw_hits_k10 / total_queries) << "%)\n";
    std::cout << "   - Top-20:  Exact " << exact_hits_k20 << "/" << total_queries << " (" << (100.0 * exact_hits_k20 / total_queries) << "%) vs HNSW " << hnsw_hits_k20 << "/" << total_queries << " (" << (100.0 * hnsw_hits_k20 / total_queries) << "%)\n";
    std::cout << "   - Top-50:  Exact " << exact_hits_k50 << "/" << total_queries << " (" << (100.0 * exact_hits_k50 / total_queries) << "%) vs HNSW " << hnsw_hits_k50 << "/" << total_queries << " (" << (100.0 * hnsw_hits_k50 / total_queries) << "%)\n";
    std::cout << "   - Top-100: Exact " << exact_hits_k100 << "/" << total_queries << " (" << (100.0 * exact_hits_k100 / total_queries) << "%) vs HNSW " << hnsw_hits_k100 << "/" << total_queries << " (" << (100.0 * hnsw_hits_k100 / total_queries) << "%)\n\n";

    return 0;
}
