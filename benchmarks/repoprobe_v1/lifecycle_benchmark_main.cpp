// Amoeba — Cold vs Warm Lifecycle & Persistence Benchmark
//
// Measures scanning, parsing, embedding, index build, save/load, cold vs warm query latency
// for ExactSemanticIndex vs HnswSemanticIndex across small, medium, and large repositories.

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
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

struct RepoBenchmarkSummary {
    std::string repo_name;
    std::size_t file_count{0};
    std::size_t unit_count{0};

    double scan_and_parse_ms{0.0};
    double embedding_generation_ms{0.0};

    double exact_build_ms{0.0};
    double exact_save_ms{0.0};
    double exact_load_ms{0.0};
    std::size_t exact_file_bytes{0};

    double hnsw_build_ms{0.0};
    double hnsw_save_ms{0.0};
    double hnsw_load_ms{0.0};
    std::size_t hnsw_file_bytes{0};

    // Cold vs Warm Query (Exact)
    double exact_cold_query_ms{0.0};
    double exact_warm_query_avg_ms{0.0};
    double exact_total_1_query_ms{0.0};
    double exact_total_10_queries_ms{0.0};
    double exact_total_100_queries_ms{0.0};

    // Cold vs Warm Query (HNSW In-Memory)
    double hnsw_cold_query_ms{0.0};
    double hnsw_warm_query_avg_ms{0.0};
    double hnsw_total_1_query_ms{0.0};
    double hnsw_total_10_queries_ms{0.0};
    double hnsw_total_100_queries_ms{0.0};

    // Cold vs Warm Query (HNSW Loaded from Persistence)
    double hnsw_loaded_cold_query_ms{0.0};
    double hnsw_loaded_warm_query_avg_ms{0.0};
    double hnsw_loaded_total_1_query_ms{0.0};
    double hnsw_loaded_total_10_queries_ms{0.0};
    double hnsw_loaded_total_100_queries_ms{0.0};
};

} // namespace

int main(int argc, char** argv) {
    std::cout << "=================================================================\n";
    std::cout << "Amoeba Lifecycle & Persistence Benchmark (Cold vs Warm)\n";
    std::cout << "=================================================================\n\n";

    fs::path benchmark_dir = "benchmarks/repoprobe_v1";
    if (argc > 1) {
        benchmark_dir = argv[1];
    }

    fs::path repos_dir = benchmark_dir / "repos";
    const std::vector<std::string> target_repos = {
        "ducklake",    // Small (1.8k units)
        "better-auth", // Medium (5.3k units)
        "adk-python"   // Large (19.9k units)
    };

    amoeba::semantic::DeterministicEmbeddingProvider provider;

    // Test queries
    const std::vector<std::string> benchmark_queries = {
        "Where are custom functions or macros registered?",
        "How does parquet data flush into storage?",
        "How are authentication bearer tokens exchanged?",
        "What base class handles agent state lifecycle?",
        "How are session cookies validated across endpoints?",
        "Where is schema migration logic defined for transactions?",
        "What middleware handles request rate limiting?",
        "How does configuration parser handle syntax trees?",
        "Where is error logging dispatched to sinks?",
        "How are background worker tasks scheduled?"
    };

    std::vector<std::vector<float>> query_embeddings;
    query_embeddings.reserve(benchmark_queries.size());
    for (const auto& q : benchmark_queries) {
        query_embeddings.push_back(provider.embed(q));
    }

    std::vector<RepoBenchmarkSummary> results;

    for (const auto& repo : target_repos) {
        fs::path repo_path = repos_dir / repo;
        if (!fs::exists(repo_path)) {
            std::cout << "Skipping missing repo: " << repo << "\n";
            continue;
        }

        std::cout << "Processing repository: " << repo << " ...\n";
        RepoBenchmarkSummary rbs;
        rbs.repo_name = repo;

        // 1. Scan and Parse
        auto t_scan_start = Clock::now();
        amoeba::scanner::RepositoryScanner scanner;
        auto scan_res = scanner.scan(repo_path);
        rbs.file_count = scan_res.files.size();

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
        auto units = amoeba::retrieval::SupportingEvidenceResolver::resolve_units(parsed_files);
        for (uint32_t i = 0; i < units.size(); ++i) {
            units[i].primary_element_id = i + 1;
        }
        auto t_scan_end = Clock::now();
        rbs.scan_and_parse_ms = std::chrono::duration<double, std::milli>(t_scan_end - t_scan_start).count();
        rbs.unit_count = units.size();

        // 2. Embedding Generation
        auto t_emb_start = Clock::now();
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
        auto t_emb_end = Clock::now();
        rbs.embedding_generation_ms = std::chrono::duration<double, std::milli>(t_emb_end - t_emb_start).count();

        // 3. Exact Index Build, Save, Load
        amoeba::semantic::ExactSemanticIndex exact_index;
        auto t_ex_b_start = Clock::now();
        for (const auto& emb : embeddings) {
            exact_index.add(emb);
        }
        auto t_ex_b_end = Clock::now();
        rbs.exact_build_ms = std::chrono::duration<double, std::milli>(t_ex_b_end - t_ex_b_start).count();

        fs::path exact_save_file = fs::temp_directory_path() / (repo + "_exact.bin");
        auto t_ex_s_start = Clock::now();
        exact_index.save(exact_save_file);
        auto t_ex_s_end = Clock::now();
        rbs.exact_save_ms = std::chrono::duration<double, std::milli>(t_ex_s_end - t_ex_s_start).count();
        rbs.exact_file_bytes = fs::file_size(exact_save_file);

        amoeba::semantic::ExactSemanticIndex exact_loaded;
        auto t_ex_l_start = Clock::now();
        exact_loaded.load(exact_save_file);
        auto t_ex_l_end = Clock::now();
        rbs.exact_load_ms = std::chrono::duration<double, std::milli>(t_ex_l_end - t_ex_l_start).count();

        // 4. HNSW Index Build, Save, Load
        amoeba::semantic::HnswConfig hnsw_cfg{
            .m = 16,
            .ef_construction = 200,
            .ef_search = 64,
            .initial_capacity = units.size() + 100,
            .random_seed = 42
        };
        amoeba::semantic::HnswSemanticIndex hnsw_index(hnsw_cfg);
        auto t_hn_b_start = Clock::now();
        for (const auto& emb : embeddings) {
            hnsw_index.add(emb);
        }
        auto t_hn_b_end = Clock::now();
        rbs.hnsw_build_ms = std::chrono::duration<double, std::milli>(t_hn_b_end - t_hn_b_start).count();

        fs::path hnsw_save_file = fs::temp_directory_path() / (repo + "_hnsw.bin");
        auto t_hn_s_start = Clock::now();
        hnsw_index.save(hnsw_save_file);
        auto t_hn_s_end = Clock::now();
        rbs.hnsw_save_ms = std::chrono::duration<double, std::milli>(t_hn_s_end - t_hn_s_start).count();
        rbs.hnsw_file_bytes = fs::file_size(hnsw_save_file);
        if (fs::exists(hnsw_save_file.string() + ".graph")) {
            rbs.hnsw_file_bytes += fs::file_size(hnsw_save_file.string() + ".graph");
        }

        amoeba::semantic::HnswSemanticIndex hnsw_loaded;
        auto t_hn_l_start = Clock::now();
        hnsw_loaded.load(hnsw_save_file);
        auto t_hn_l_end = Clock::now();
        rbs.hnsw_load_ms = std::chrono::duration<double, std::milli>(t_hn_l_end - t_hn_l_start).count();

        // 5. Query Latencies: Exact
        // Cold Query
        auto t_q1_s = Clock::now();
        exact_index.search(query_embeddings[0], 10);
        auto t_q1_e = Clock::now();
        rbs.exact_cold_query_ms = std::chrono::duration<double, std::milli>(t_q1_e - t_q1_s).count();

        // Warm Queries (100 repetitions)
        double warm_sum = 0.0;
        for (int rep = 0; rep < 100; ++rep) {
            const auto& q_vec = query_embeddings[rep % query_embeddings.size()];
            auto t_s = Clock::now();
            exact_index.search(q_vec, 10);
            auto t_e = Clock::now();
            warm_sum += std::chrono::duration<double, std::milli>(t_e - t_s).count();
        }
        rbs.exact_warm_query_avg_ms = warm_sum / 100.0;
        rbs.exact_total_1_query_ms = rbs.scan_and_parse_ms + rbs.embedding_generation_ms + rbs.exact_build_ms + rbs.exact_cold_query_ms;
        rbs.exact_total_10_queries_ms = rbs.scan_and_parse_ms + rbs.embedding_generation_ms + rbs.exact_build_ms + rbs.exact_cold_query_ms + (9 * rbs.exact_warm_query_avg_ms);
        rbs.exact_total_100_queries_ms = rbs.scan_and_parse_ms + rbs.embedding_generation_ms + rbs.exact_build_ms + rbs.exact_cold_query_ms + (99 * rbs.exact_warm_query_avg_ms);

        // 6. Query Latencies: HNSW (In-Memory)
        auto t_hn_q1_s = Clock::now();
        hnsw_index.search(query_embeddings[0], 10);
        auto t_hn_q1_e = Clock::now();
        rbs.hnsw_cold_query_ms = std::chrono::duration<double, std::milli>(t_hn_q1_e - t_hn_q1_s).count();

        double hnsw_warm_sum = 0.0;
        for (int rep = 0; rep < 100; ++rep) {
            const auto& q_vec = query_embeddings[rep % query_embeddings.size()];
            auto t_s = Clock::now();
            hnsw_index.search(q_vec, 10);
            auto t_e = Clock::now();
            hnsw_warm_sum += std::chrono::duration<double, std::milli>(t_e - t_s).count();
        }
        rbs.hnsw_warm_query_avg_ms = hnsw_warm_sum / 100.0;
        rbs.hnsw_total_1_query_ms = rbs.scan_and_parse_ms + rbs.embedding_generation_ms + rbs.hnsw_build_ms + rbs.hnsw_cold_query_ms;
        rbs.hnsw_total_10_queries_ms = rbs.scan_and_parse_ms + rbs.embedding_generation_ms + rbs.hnsw_build_ms + rbs.hnsw_cold_query_ms + (9 * rbs.hnsw_warm_query_avg_ms);
        rbs.hnsw_total_100_queries_ms = rbs.scan_and_parse_ms + rbs.embedding_generation_ms + rbs.hnsw_build_ms + rbs.hnsw_cold_query_ms + (99 * rbs.hnsw_warm_query_avg_ms);

        // 7. Query Latencies: HNSW (Loaded from Persisted File)
        auto t_hnl_q1_s = Clock::now();
        hnsw_loaded.search(query_embeddings[0], 10);
        auto t_hnl_q1_e = Clock::now();
        rbs.hnsw_loaded_cold_query_ms = std::chrono::duration<double, std::milli>(t_hnl_q1_e - t_hnl_q1_s).count();

        double hnl_warm_sum = 0.0;
        for (int rep = 0; rep < 100; ++rep) {
            const auto& q_vec = query_embeddings[rep % query_embeddings.size()];
            auto t_s = Clock::now();
            hnsw_loaded.search(q_vec, 10);
            auto t_e = Clock::now();
            hnl_warm_sum += std::chrono::duration<double, std::milli>(t_e - t_s).count();
        }
        rbs.hnsw_loaded_warm_query_avg_ms = hnl_warm_sum / 100.0;
        // With persistence: No parse/embed/HNSW build! Just loadIndex + query!
        rbs.hnsw_loaded_total_1_query_ms = rbs.hnsw_load_ms + rbs.hnsw_loaded_cold_query_ms;
        rbs.hnsw_loaded_total_10_queries_ms = rbs.hnsw_load_ms + rbs.hnsw_loaded_cold_query_ms + (9 * rbs.hnsw_loaded_warm_query_avg_ms);
        rbs.hnsw_loaded_total_100_queries_ms = rbs.hnsw_load_ms + rbs.hnsw_loaded_cold_query_ms + (99 * rbs.hnsw_loaded_warm_query_avg_ms);

        // Clean up temp files
        fs::remove(exact_save_file);
        fs::remove(hnsw_save_file);
        fs::remove(hnsw_save_file.string() + ".graph");

        results.push_back(rbs);
    }

    std::cout << "\n=================================================================\n";
    std::cout << "LIFECYCLE & PERSISTENCE BENCHMARK RESULTS\n";
    std::cout << "=================================================================\n\n";

    std::cout << std::fixed << std::setprecision(2);

    for (const auto& r : results) {
        std::cout << "-----------------------------------------------------------------\n";
        std::cout << "REPOSITORY: " << r.repo_name << " (" << r.file_count << " files, " << r.unit_count << " primary units)\n";
        std::cout << "-----------------------------------------------------------------\n";
        std::cout << "  [1] Pre-computation Latencies:\n";
        std::cout << "      - Scan & Parse:               " << r.scan_and_parse_ms << " ms\n";
        std::cout << "      - Unit Embedding Generation:  " << r.embedding_generation_ms << " ms\n";
        std::cout << "      - Exact Index Construction:   " << r.exact_build_ms << " ms\n";
        std::cout << "      - HNSW Graph Construction:    " << r.hnsw_build_ms << " ms\n\n";

        std::cout << "  [2] Persistence Characteristics (Disk):\n";
        std::cout << "      - Exact: Save=" << r.exact_save_ms << " ms, Load=" << r.exact_load_ms
                  << " ms, FileSize=" << (r.exact_file_bytes / 1024.0) << " KB\n";
        std::cout << "      - HNSW:  Save=" << r.hnsw_save_ms << " ms, Load=" << r.hnsw_load_ms
                  << " ms, FileSize=" << (r.hnsw_file_bytes / 1024.0) << " KB\n\n";

        std::cout << "  [3] Search Query Latency (Isolated Vector Search):\n";
        std::cout << "      - Exact:       Cold=" << r.exact_cold_query_ms << " ms, Warm Avg=" << r.exact_warm_query_avg_ms << " ms\n";
        std::cout << "      - HNSW Memory: Cold=" << r.hnsw_cold_query_ms << " ms, Warm Avg=" << r.hnsw_warm_query_avg_ms << " ms ("
                  << (r.exact_warm_query_avg_ms / r.hnsw_warm_query_avg_ms) << "x speedup)\n";
        std::cout << "      - HNSW Loaded: Cold=" << r.hnsw_loaded_cold_query_ms << " ms, Warm Avg=" << r.hnsw_loaded_warm_query_avg_ms << " ms\n\n";

        std::cout << "  [4] End-to-End User Experience (Startup + Precompute + N Queries):\n";
        std::cout << "      • 1 Query Total Execution Time:\n";
        std::cout << "        - Exact (from scratch):      " << r.exact_total_1_query_ms << " ms\n";
        std::cout << "        - HNSW (from scratch):       " << r.hnsw_total_1_query_ms << " ms\n";
        std::cout << "        - HNSW (from persisted disk):" << r.hnsw_loaded_total_1_query_ms << " ms ("
                  << (r.hnsw_total_1_query_ms / r.hnsw_loaded_total_1_query_ms) << "x faster cold start)\n";

        std::cout << "      • 10 Queries Total Execution Time:\n";
        std::cout << "        - Exact (from scratch):      " << r.exact_total_10_queries_ms << " ms\n";
        std::cout << "        - HNSW (from scratch):       " << r.hnsw_total_10_queries_ms << " ms\n";
        std::cout << "        - HNSW (from persisted disk):" << r.hnsw_loaded_total_10_queries_ms << " ms\n";

        std::cout << "      • 100 Queries Total Execution Time:\n";
        std::cout << "        - Exact (from scratch):      " << r.exact_total_100_queries_ms << " ms\n";
        std::cout << "        - HNSW (from scratch):       " << r.hnsw_total_100_queries_ms << " ms\n";
        std::cout << "        - HNSW (from persisted disk):" << r.hnsw_loaded_total_100_queries_ms << " ms\n\n";
    }

    return 0;
}
