// Phase 6.5 — Native Retrieval Evaluation Benchmark
//
// Measures whether RetrievalUnit-based primary retrieval improves retrieval behavior
// compared with raw-CodeElement retrieval architecture using real native C++ components.

#include "amoeba/eval/ranking_metrics.hpp"
#include "amoeba/hybrid/hybrid_retriever.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/index/search_engine.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/retrieval/supporting_evidence_resolver.hpp"
#include "amoeba/scanner/repository_scanner.hpp"
#include "amoeba/semantic/pretrained_embedding_provider.hpp"
#include "amoeba/semantic/semantic_document.hpp"
#include "amoeba/semantic/semantic_index.hpp"
#include "amoeba/semantic/semantic_retriever.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct FrozenQuery {
    std::string id;
    std::string query;
    std::string category;
    std::string expected_name;
    std::string expected_file;
    std::string alternate_name;
    std::string alternate_file;
    std::string rationale;
};

const std::vector<FrozenQuery> FROZEN_QUERIES = {
    {
        .id = "Q1",
        .query = "CalendarGrid",
        .category = "Exact Identifier",
        .expected_name = "CalendarGrid",
        .expected_file = "src/components/calendar/CalendarGrid.tsx",
        .alternate_name = "",
        .alternate_file = "",
        .rationale = "Direct exact match for 7-column monthly calendar matrix component.",
    },
    {
        .id = "Q2",
        .query = "use calendar",
        .category = "Normalized Identifier",
        .expected_name = "useCalendar",
        .expected_file = "src/hooks/useCalendar.ts",
        .alternate_name = "",
        .alternate_file = "",
        .rationale = "Space-separated normalized query targeting camelCase React date hook.",
    },
    {
        .id = "Q3",
        .query = "LocalStorage",
        .category = "Partial/Subword",
        .expected_name = "useLocalStorage",
        .expected_file = "src/hooks/useLocalStorage.ts",
        .alternate_name = "",
        .alternate_file = "",
        .rationale = "Subword search for client-side persistence handler for notes & highlights.",
    },
    {
        .id = "Q4",
        .query = "FloatingToolbar action",
        .category = "Multi-Term",
        .expected_name = "FloatingToolbar",
        .expected_file = "src/components/floating-toolbar/FloatingToolbar.tsx",
        .alternate_name = "",
        .alternate_file = "",
        .rationale = "Multi-term query for floating navigation action bar.",
    },
    {
        .id = "Q5",
        .query = "formatDate formatStr",
        .category = "Contextual Lexical",
        .expected_name = "formatDate",
        .expected_file = "src/lib/dateUtils.ts",
        .alternate_name = "",
        .alternate_file = "",
        .rationale = "Function identifier combined with its signature parameter term.",
    },
    {
        .id = "Q6",
        .query = "RootLayout Next.js metadata",
        .category = "Framework",
        .expected_name = "RootLayout",
        .expected_file = "src/app/layout.tsx",
        .alternate_name = "",
        .alternate_file = "",
        .rationale = "Next.js App Router root layout managing global metadata.",
    },
    {
        .id = "Q7",
        .query = "Dialog",
        .category = "Ambiguous",
        .expected_name = "Dialog",
        .expected_file = "src/components/ui/dialog.tsx",
        .alternate_name = "",
        .alternate_file = "",
        .rationale = "Ambiguous query spanning modal primitive definitions and page usage sites.",
    },
    {
        .id = "Q8",
        .query = "Where is the location destination photo and travel description rendered for each "
                 "month?",
        .category = "Conceptual/Semantic",
        .expected_name = "ImagePanel",
        .expected_file = "src/components/calendar/ImagePanel.tsx",
        .alternate_name = "getImagePanelData",
        .alternate_file = "src/lib/monthLocationData.ts",
        .rationale = "Natural language question targeting landscape photography showcase.",
    },
    {
        .id = "Q9",
        .query = "MonthLocation getImagePanelData",
        .category = "Cross-File",
        .expected_name = "MonthLocation",
        .expected_file = "src/lib/monthLocationData.ts",
        .alternate_name = "getImagePanelData",
        .alternate_file = "src/lib/monthLocationData.ts",
        .rationale =
            "Cross-file data contract connecting destination datasets with UI presentation.",
    },
    {
        .id = "Q10",
        .query = "CalendarDay rendered inside CalendarGrid",
        .category = "Component Relationship",
        .expected_name = "CalendarDay",
        .expected_file = "src/components/calendar/CalendarDay.tsx",
        .alternate_name = "CalendarGrid",
        .alternate_file = "src/components/calendar/CalendarGrid.tsx",
        .rationale = "Structural parent-child React relationship mapping date intervals to cells.",
    },
};

struct QueryMatchResult {
    std::string top_name;
    std::string top_file;
    std::string top_kind;
    uint32_t target_rank{0};  // 1-indexed, 0 if not in top-k
    bool target_found{false};
    double score{0.0};
    double latency_ms{0.0};
    std::vector<std::string> supporting_evidence_names;
};

struct MethodEvaluation {
    std::string name;
    std::string architecture;  // "BEFORE (Raw CodeElements)" or "AFTER (RetrievalUnits)"
    amoeba::eval::RankingMetrics aggregate_metrics;
    double avg_latency_ms{0.0};
    std::vector<QueryMatchResult> query_results;
};

bool check_match(const std::string& name, const std::string& file, const FrozenQuery& q) {
    auto normalize = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    };

    const std::string n_name = normalize(name);
    const std::string n_file = normalize(file);

    if (!q.expected_name.empty() && n_name == normalize(q.expected_name)) {
        if (q.expected_file.empty() ||
            n_file.find(normalize(q.expected_file)) != std::string::npos) {
            return true;
        }
    }
    if (!q.alternate_name.empty() && n_name == normalize(q.alternate_name)) {
        if (q.alternate_file.empty() ||
            n_file.find(normalize(q.alternate_file)) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string escape_json(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        if (c == '"')
            o << "\\\"";
        else if (c == '\\')
            o << "\\\\";
        else if (c == '\b')
            o << "\\b";
        else if (c == '\f')
            o << "\\f";
        else if (c == '\n')
            o << "\\n";
        else if (c == '\r')
            o << "\\r";
        else if (c == '\t')
            o << "\\t";
        else if (static_cast<unsigned char>(c) <= 0x1f) {
            o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        } else {
            o << c;
        }
    }
    return o.str();
}

int main(int argc, char** argv) {
    std::cout << "=================================================================\n";
    std::cout << " Amoeba Phase 6.5 — Native Retrieval Evaluation & Hardening\n";
    std::cout << "=================================================================\n\n";

    fs::path corpus_path = "demo_test_projects/calendar";
    if (argc > 1) {
        corpus_path = argv[1];
    }

    if (!fs::exists(corpus_path)) {
        std::cerr << "ERROR: Holdout corpus path not found: " << corpus_path << "\n";
        return 1;
    }

    std::cout << "1. Scanning & Parsing Corpus: " << corpus_path.generic_string() << "\n";
    amoeba::scanner::RepositoryScanner scanner;
    const auto scan_res = scanner.scan(corpus_path);

    amoeba::parser::SourceParser parser;
    std::vector<amoeba::parser::ParsedFile> parsed_files;
    parsed_files.reserve(scan_res.files.size());

    std::size_t total_raw_elements = 0;
    for (const auto& fi : scan_res.files) {
        auto pf = parser.parse_file(fi.path);
        if (pf.success) {
            total_raw_elements += pf.elements.size();
            parsed_files.push_back(std::move(pf));
        }
    }

    std::cout << "   Files scanned: " << scan_res.total_files_discovered << "\n";
    std::cout << "   Source files parsed: " << parsed_files.size() << "\n";
    std::cout << "   Raw CodeElements: " << total_raw_elements << "\n";

    // Build InvertedIndex
    amoeba::index::InvertedIndex raw_index;
    for (const auto& pf : parsed_files) {
        raw_index.add_parsed_file(pf);
    }
    std::cout << "   InvertedIndex elements: " << raw_index.element_count() << "\n";
    std::cout << "   InvertedIndex terms: " << raw_index.term_count() << "\n";
    std::cout << "   InvertedIndex postings: " << raw_index.posting_count() << "\n";

    // Initialize PretrainedEmbeddingProvider (384-dim MiniLM-compatible)
    std::cout << "\n2. Initializing Pretrained Embedding Provider...\n";
    amoeba::semantic::PretrainedEmbeddingProvider provider;
    std::cout << "   Provider name: " << provider.provider_name() << "\n";
    std::cout << "   Dimensions: " << provider.dimensions() << "\n";

    // ─── BEFORE Architecture: Raw CodeElement Semantic Index ─────────────────
    std::cout << "\n3. Building BEFORE Architecture (Raw CodeElement Semantic Index)...\n";
    const auto t_raw_sem_start = Clock::now();
    amoeba::semantic::SemanticIndex raw_semantic_index;
    for (amoeba::index::ElementId id = 0;
         id < static_cast<amoeba::index::ElementId>(raw_index.element_count()); ++id) {
        const auto& ie = raw_index.get_element(id);
        const auto& file = raw_index.get_file(ie.file_id);
        const auto doc = amoeba::semantic::SemanticTextFormatter::create_document(
            id, file.language, file.file_path, ie.element);
        const auto emb_vals = provider.embed(doc.text_representation);
        if (!emb_vals.empty()) {
            raw_semantic_index.add(amoeba::semantic::Embedding{
                .element_id = id,
                .values = emb_vals,
            });
        }
    }
    const auto t_raw_sem_end = Clock::now();
    const double raw_sem_build_ms =
        std::chrono::duration<double, std::milli>(t_raw_sem_end - t_raw_sem_start).count();
    std::cout << "   Raw SemanticIndex size: " << raw_semantic_index.size() << " docs in "
              << raw_sem_build_ms << " ms\n";

    amoeba::index::SearchEngine raw_search_engine(raw_index);
    amoeba::semantic::SemanticRetriever raw_semantic_retriever(raw_semantic_index);
    amoeba::hybrid::HybridRetriever raw_hybrid_retriever(raw_index, raw_semantic_index, provider);

    // ─── AFTER Architecture: Primary Retrieval Pipeline ──────────────────────
    std::cout << "\n4. Building AFTER Architecture (Primary Retrieval Pipeline)...\n";
    const auto t_pipeline_start = Clock::now();
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, raw_index, provider);
    const auto t_pipeline_end = Clock::now();
    const double pipeline_build_ms =
        std::chrono::duration<double, std::milli>(t_pipeline_end - t_pipeline_start).count();

    std::cout << "   Primary RetrievalUnits: " << pipeline.primary_unit_count() << "\n";
    std::cout << "   Supporting Elements: " << pipeline.supporting_element_count() << "\n";
    std::cout << "   Primary SemanticIndex size: " << pipeline.semantic_index().size()
              << " docs in " << pipeline_build_ms << " ms\n";
    const double reduction_pct = 100.0 * (1.0 - static_cast<double>(pipeline.primary_unit_count()) /
                                                    static_cast<double>(total_raw_elements));
    std::cout << "   Representation candidate reduction: " << std::fixed << std::setprecision(1)
              << reduction_pct << "%\n";

    // ─── Run Evaluations ─────────────────────────────────────────────────────
    std::cout << "\n5. Executing Native C++ Benchmark across 10 Frozen Queries...\n";

    std::vector<MethodEvaluation> all_methods;

    // Helper to evaluate BEFORE methods
    auto eval_before_lexical = [&](std::string name, amoeba::index::RankerType ranker) {
        MethodEvaluation me;
        me.name = name;
        me.architecture = "BEFORE (Raw CodeElements)";
        std::vector<amoeba::eval::RankingMetrics> query_metrics;
        double total_lat = 0.0;

        for (const auto& q : FROZEN_QUERIES) {
            amoeba::index::SearchOptions opts{
                .match_mode = amoeba::index::MatchMode::AnyTerm,
                .ranker_type = ranker,
                .max_results = 20,
            };

            const auto t0 = Clock::now();
            auto results = raw_search_engine.search(q.query, opts);
            const auto t1 = Clock::now();
            const double lat = std::chrono::duration<double, std::milli>(t1 - t0).count();
            total_lat += lat;

            std::vector<std::string> retrieved_names;
            std::unordered_map<std::string, uint32_t> grades;
            QueryMatchResult q_res;
            q_res.latency_ms = lat;

            if (!results.empty()) {
                q_res.top_name = results[0].element.name;
                q_res.top_file = results[0].file_path.generic_string();
                q_res.top_kind = std::string(amoeba::parser::to_string(results[0].element.kind));
                q_res.score = results[0].score;
            }

            for (std::size_t i = 0; i < results.size(); ++i) {
                const auto& r = results[i];
                const std::string name = r.element.name;
                const std::string file = r.file_path.generic_string();
                retrieved_names.push_back(name);

                if (check_match(name, file, q)) {
                    grades[name] = 3;
                    if (!q_res.target_found) {
                        q_res.target_found = true;
                        q_res.target_rank = static_cast<uint32_t>(i + 1);
                    }
                }
            }

            auto m = amoeba::eval::compute_ranking_metrics(retrieved_names, grades);
            query_metrics.push_back(m);
            me.query_results.push_back(q_res);
        }

        me.aggregate_metrics = amoeba::eval::RankingMetrics::average(query_metrics);
        me.avg_latency_ms = total_lat / FROZEN_QUERIES.size();
        all_methods.push_back(std::move(me));
    };

    auto eval_before_semantic = [&]() {
        MethodEvaluation me;
        me.name = "Raw Semantic (MiniLM-384)";
        me.architecture = "BEFORE (Raw CodeElements)";
        std::vector<amoeba::eval::RankingMetrics> query_metrics;
        double total_lat = 0.0;

        for (const auto& q : FROZEN_QUERIES) {
            amoeba::semantic::SemanticRetrievalOptions sem_opts{.top_k = 20};

            const auto t0 = Clock::now();
            auto results = raw_semantic_retriever.retrieve_text(q.query, provider, sem_opts);
            const auto t1 = Clock::now();
            const double lat = std::chrono::duration<double, std::milli>(t1 - t0).count();
            total_lat += lat;

            std::vector<std::string> retrieved_names;
            std::unordered_map<std::string, uint32_t> grades;
            QueryMatchResult q_res;
            q_res.latency_ms = lat;

            if (!results.empty()) {
                const auto& ie = raw_index.get_element(results[0].element_id);
                const auto& file = raw_index.get_file(ie.file_id);
                q_res.top_name = ie.element.name;
                q_res.top_file = file.file_path.generic_string();
                q_res.top_kind = std::string(amoeba::parser::to_string(ie.element.kind));
                q_res.score = results[0].similarity_score;
            }

            for (std::size_t i = 0; i < results.size(); ++i) {
                const auto& r = results[i];
                const auto& ie = raw_index.get_element(r.element_id);
                const auto& file = raw_index.get_file(ie.file_id);
                const std::string name = ie.element.name;
                const std::string fpath = file.file_path.generic_string();
                retrieved_names.push_back(name);

                if (check_match(name, fpath, q)) {
                    grades[name] = 3;
                    if (!q_res.target_found) {
                        q_res.target_found = true;
                        q_res.target_rank = static_cast<uint32_t>(i + 1);
                    }
                }
            }

            auto m = amoeba::eval::compute_ranking_metrics(retrieved_names, grades);
            query_metrics.push_back(m);
            me.query_results.push_back(q_res);
        }

        me.aggregate_metrics = amoeba::eval::RankingMetrics::average(query_metrics);
        me.avg_latency_ms = total_lat / FROZEN_QUERIES.size();
        all_methods.push_back(std::move(me));
    };

    auto eval_before_hybrid = [&](std::string name, double alpha,
                                  amoeba::hybrid::FusionMethod method) {
        MethodEvaluation me;
        me.name = name;
        me.architecture = "BEFORE (Raw CodeElements)";
        std::vector<amoeba::eval::RankingMetrics> query_metrics;
        double total_lat = 0.0;

        for (const auto& q : FROZEN_QUERIES) {
            amoeba::hybrid::HybridSearchOptions hyb_opts{
                .alpha = alpha,
                .fusion_method = method,
                .rrf_k = 60.0,
                .lexical_ranker = amoeba::index::RankerType::CodeAware,
                .lexical_top_k = 50,
                .semantic_top_k = 50,
                .max_results = 20,
            };

            const auto t0 = Clock::now();
            auto results = raw_hybrid_retriever.search(q.query, hyb_opts);
            const auto t1 = Clock::now();
            const double lat = std::chrono::duration<double, std::milli>(t1 - t0).count();
            total_lat += lat;

            std::vector<std::string> retrieved_names;
            std::unordered_map<std::string, uint32_t> grades;
            QueryMatchResult q_res;
            q_res.latency_ms = lat;

            if (!results.empty()) {
                q_res.top_name = results[0].element.name;
                q_res.top_file = results[0].file_path.generic_string();
                q_res.top_kind = std::string(amoeba::parser::to_string(results[0].element.kind));
                q_res.score = results[0].hybrid_score;
            }

            for (std::size_t i = 0; i < results.size(); ++i) {
                const auto& r = results[i];
                const std::string name = r.element.name;
                const std::string file = r.file_path.generic_string();
                retrieved_names.push_back(name);

                if (check_match(name, file, q)) {
                    grades[name] = 3;
                    if (!q_res.target_found) {
                        q_res.target_found = true;
                        q_res.target_rank = static_cast<uint32_t>(i + 1);
                    }
                }
            }

            auto m = amoeba::eval::compute_ranking_metrics(retrieved_names, grades);
            query_metrics.push_back(m);
            me.query_results.push_back(q_res);
        }

        me.aggregate_metrics = amoeba::eval::RankingMetrics::average(query_metrics);
        me.avg_latency_ms = total_lat / FROZEN_QUERIES.size();
        all_methods.push_back(std::move(me));
    };

    // Helper to evaluate AFTER methods
    auto eval_after = [&](std::string name, double alpha, amoeba::index::RankerType ranker,
                          amoeba::hybrid::FusionMethod method) {
        MethodEvaluation me;
        me.name = name;
        me.architecture = "AFTER (RetrievalUnits)";
        std::vector<amoeba::eval::RankingMetrics> query_metrics;
        double total_lat = 0.0;

        for (const auto& q : FROZEN_QUERIES) {
            amoeba::retrieval::PrimarySearchOptions opts{
                .alpha = alpha,
                .fusion_method = method,
                .rrf_k = 60.0,
                .lexical_ranker = ranker,
                .lexical_top_k = 50,
                .semantic_top_k = 50,
                .max_results = 20,
            };

            amoeba::retrieval::PipelineMetrics p_metrics;
            const auto t0 = Clock::now();
            auto results = pipeline.search_with_metrics(q.query, opts, p_metrics);
            const auto t1 = Clock::now();
            const double lat = std::chrono::duration<double, std::milli>(t1 - t0).count();
            total_lat += lat;

            std::vector<std::string> retrieved_names;
            std::unordered_map<std::string, uint32_t> grades;
            QueryMatchResult q_res;
            q_res.latency_ms = lat;

            if (!results.empty()) {
                q_res.top_name = results[0].unit.primary_element.name;
                q_res.top_file = results[0].unit.file_path.generic_string();
                q_res.top_kind =
                    std::string(amoeba::parser::to_string(results[0].unit.primary_element.kind));
                q_res.score = results[0].hybrid_score;
                for (const auto& ev : results[0].unit.supporting_elements) {
                    q_res.supporting_evidence_names.push_back(ev.name);
                }
            }

            for (std::size_t i = 0; i < results.size(); ++i) {
                const auto& r = results[i];
                const std::string name = r.unit.primary_element.name;
                const std::string file = r.unit.file_path.generic_string();
                retrieved_names.push_back(name);

                if (check_match(name, file, q)) {
                    grades[name] = 3;
                    if (!q_res.target_found) {
                        q_res.target_found = true;
                        q_res.target_rank = static_cast<uint32_t>(i + 1);
                    }
                }
            }

            auto m = amoeba::eval::compute_ranking_metrics(retrieved_names, grades);
            query_metrics.push_back(m);
            me.query_results.push_back(q_res);
        }

        me.aggregate_metrics = amoeba::eval::RankingMetrics::average(query_metrics);
        me.avg_latency_ms = total_lat / FROZEN_QUERIES.size();
        all_methods.push_back(std::move(me));
    };

    // Evaluate BEFORE methods
    eval_before_lexical("Raw Baseline", amoeba::index::RankerType::Baseline);
    eval_before_lexical("Raw BM25", amoeba::index::RankerType::BM25);
    eval_before_lexical("Raw CodeAware", amoeba::index::RankerType::CodeAware);
    eval_before_semantic();
    eval_before_hybrid("Raw Hybrid (a=0.5)", 0.5, amoeba::hybrid::FusionMethod::WeightedScore);
    eval_before_hybrid("Raw Hybrid (a=0.3)", 0.3, amoeba::hybrid::FusionMethod::WeightedScore);
    eval_before_hybrid("Raw Hybrid RRF", 0.5, amoeba::hybrid::FusionMethod::ReciprocalRank);

    // Evaluate AFTER methods
    eval_after("Unit Baseline", 1.0, amoeba::index::RankerType::Baseline,
               amoeba::hybrid::FusionMethod::WeightedScore);
    eval_after("Unit BM25", 1.0, amoeba::index::RankerType::BM25,
               amoeba::hybrid::FusionMethod::WeightedScore);
    eval_after("Unit CodeAware", 1.0, amoeba::index::RankerType::CodeAware,
               amoeba::hybrid::FusionMethod::WeightedScore);
    eval_after("Unit Semantic (MiniLM-384)", 0.0, amoeba::index::RankerType::CodeAware,
               amoeba::hybrid::FusionMethod::WeightedScore);
    eval_after("Unit Hybrid (a=0.5)", 0.5, amoeba::index::RankerType::CodeAware,
               amoeba::hybrid::FusionMethod::WeightedScore);
    eval_after("Unit Hybrid (a=0.3)", 0.3, amoeba::index::RankerType::CodeAware,
               amoeba::hybrid::FusionMethod::WeightedScore);
    eval_after("Unit Hybrid RRF", 0.5, amoeba::index::RankerType::CodeAware,
               amoeba::hybrid::FusionMethod::ReciprocalRank);

    // ─── Print Results Table ─────────────────────────────────────────────────
    std::cout << "\n==============================================================================="
                 "==========================\n";
    std::cout << " NATIVE RETRIEVAL BENCHMARK RESULTS (N=10 Frozen Queries, " << total_raw_elements
              << " Raw Elements -> " << pipeline.primary_unit_count() << " Units)\n";
    std::cout << "================================================================================="
                 "========================\n";
    std::cout << std::left << std::setw(28) << "Method" << std::right << std::setw(8) << "P@1"
              << std::setw(8) << "P@3" << std::setw(8) << "P@5" << std::setw(10) << "Rec@5"
              << std::setw(10) << "Rec@10" << std::setw(8) << "MRR" << std::setw(9) << "NDCG@5"
              << std::setw(10) << "NDCG@10" << std::setw(12) << "Latency" << "\n";
    std::cout << "---------------------------------------------------------------------------------"
                 "------------------------\n";

    for (const auto& me : all_methods) {
        std::cout << std::left << std::setw(28) << me.name << std::right << std::fixed
                  << std::setprecision(3) << std::setw(8) << me.aggregate_metrics.p_at_1
                  << std::setw(8) << me.aggregate_metrics.p_at_3 << std::setw(8)
                  << me.aggregate_metrics.p_at_5 << std::setw(10) << me.aggregate_metrics.r_at_5
                  << std::setw(10) << me.aggregate_metrics.r_at_10 << std::setw(8)
                  << me.aggregate_metrics.mrr << std::setw(9) << me.aggregate_metrics.ndcg_at_5
                  << std::setw(10) << me.aggregate_metrics.ndcg_at_10 << std::setw(10)
                  << me.avg_latency_ms << " ms\n";
    }
    std::cout << "================================================================================="
                 "========================\n\n";

    // ─── Per-Query Breakdown ─────────────────────────────────────────────────
    std::cout << "PER-QUERY TARGET RANKS COMPARISON:\n";
    std::cout << "---------------------------------------------------------------------------------"
                 "------------------------\n";
    std::cout << std::left << std::setw(6) << "ID" << std::setw(26) << "Query" << std::setw(12)
              << "Raw Base" << std::setw(12) << "Raw BM25" << std::setw(12) << "Raw CA"
              << std::setw(12) << "Raw Sem" << std::setw(12) << "Unit CA" << std::setw(12)
              << "Unit Sem" << std::setw(12) << "Unit Hyb\n";
    std::cout << "---------------------------------------------------------------------------------"
                 "------------------------\n";

    for (std::size_t qi = 0; qi < FROZEN_QUERIES.size(); ++qi) {
        const auto& q = FROZEN_QUERIES[qi];
        std::string q_sub = q.query;
        if (q_sub.size() > 24)
            q_sub = q_sub.substr(0, 22) + "..";

        auto format_rank = [](const QueryMatchResult& qr) {
            if (!qr.target_found || qr.target_rank == 0)
                return std::string("FAIL");
            return "#" + std::to_string(qr.target_rank);
        };

        std::cout << std::left << std::setw(6) << q.id << std::setw(26) << q_sub << std::setw(12)
                  << format_rank(all_methods[0].query_results[qi])                    // Raw Base
                  << std::setw(12) << format_rank(all_methods[1].query_results[qi])   // Raw BM25
                  << std::setw(12) << format_rank(all_methods[2].query_results[qi])   // Raw CA
                  << std::setw(12) << format_rank(all_methods[3].query_results[qi])   // Raw Sem
                  << std::setw(12) << format_rank(all_methods[9].query_results[qi])   // Unit CA
                  << std::setw(12) << format_rank(all_methods[10].query_results[qi])  // Unit Sem
                  << std::setw(12) << format_rank(all_methods[11].query_results[qi])  // Unit Hyb
                  << "\n";
    }
    std::cout << "---------------------------------------------------------------------------------"
                 "------------------------\n\n";

    // ─── Export JSON / CSV Results ───────────────────────────────────────────
    fs::create_directories("benchmarks/phase-06.5/results");

    // 1. queries.json
    {
        std::ofstream ofs("benchmarks/phase-06.5/queries.json");
        ofs << "[\n";
        for (std::size_t i = 0; i < FROZEN_QUERIES.size(); ++i) {
            const auto& q = FROZEN_QUERIES[i];
            ofs << "  {\n"
                << "    \"id\": \"" << q.id << "\",\n"
                << "    \"query\": \"" << escape_json(q.query) << "\",\n"
                << "    \"category\": \"" << escape_json(q.category) << "\",\n"
                << "    \"expected_name\": \"" << escape_json(q.expected_name) << "\",\n"
                << "    \"expected_file\": \"" << escape_json(q.expected_file) << "\",\n"
                << "    \"alternate_name\": \"" << escape_json(q.alternate_name) << "\",\n"
                << "    \"alternate_file\": \"" << escape_json(q.alternate_file) << "\",\n"
                << "    \"rationale\": \"" << escape_json(q.rationale) << "\"\n"
                << "  }" << (i + 1 < FROZEN_QUERIES.size() ? ",\n" : "\n");
        }
        ofs << "]\n";
    }

    // 2. raw_code_elements.json
    {
        std::ofstream ofs("benchmarks/phase-06.5/results/raw_code_elements.json");
        ofs << "{\n"
            << "  \"architecture\": \"BEFORE (Raw CodeElements)\",\n"
            << "  \"total_elements\": " << total_raw_elements << ",\n"
            << "  \"methods\": [\n";
        for (std::size_t mi = 0; mi < 7; ++mi) {
            const auto& me = all_methods[mi];
            ofs << "    {\n"
                << "      \"name\": \"" << me.name << "\",\n"
                << "      \"p_at_1\": " << me.aggregate_metrics.p_at_1 << ",\n"
                << "      \"p_at_3\": " << me.aggregate_metrics.p_at_3 << ",\n"
                << "      \"p_at_5\": " << me.aggregate_metrics.p_at_5 << ",\n"
                << "      \"recall_at_5\": " << me.aggregate_metrics.r_at_5 << ",\n"
                << "      \"recall_at_10\": " << me.aggregate_metrics.r_at_10 << ",\n"
                << "      \"mrr\": " << me.aggregate_metrics.mrr << ",\n"
                << "      \"ndcg_at_5\": " << me.aggregate_metrics.ndcg_at_5 << ",\n"
                << "      \"ndcg_at_10\": " << me.aggregate_metrics.ndcg_at_10 << ",\n"
                << "      \"latency_ms\": " << me.avg_latency_ms << "\n"
                << "    }" << (mi + 1 < 7 ? ",\n" : "\n");
        }
        ofs << "  ]\n}\n";
    }

    // 3. retrieval_units.json
    {
        std::ofstream ofs("benchmarks/phase-06.5/results/retrieval_units.json");
        ofs << "{\n"
            << "  \"architecture\": \"AFTER (Primary RetrievalUnits)\",\n"
            << "  \"primary_units\": " << pipeline.primary_unit_count() << ",\n"
            << "  \"supporting_elements\": " << pipeline.supporting_element_count() << ",\n"
            << "  \"candidate_reduction_pct\": " << reduction_pct << ",\n"
            << "  \"methods\": [\n";
        for (std::size_t mi = 7; mi < all_methods.size(); ++mi) {
            const auto& me = all_methods[mi];
            ofs << "    {\n"
                << "      \"name\": \"" << me.name << "\",\n"
                << "      \"p_at_1\": " << me.aggregate_metrics.p_at_1 << ",\n"
                << "      \"p_at_3\": " << me.aggregate_metrics.p_at_3 << ",\n"
                << "      \"p_at_5\": " << me.aggregate_metrics.p_at_5 << ",\n"
                << "      \"recall_at_5\": " << me.aggregate_metrics.r_at_5 << ",\n"
                << "      \"recall_at_10\": " << me.aggregate_metrics.r_at_10 << ",\n"
                << "      \"mrr\": " << me.aggregate_metrics.mrr << ",\n"
                << "      \"ndcg_at_5\": " << me.aggregate_metrics.ndcg_at_5 << ",\n"
                << "      \"ndcg_at_10\": " << me.aggregate_metrics.ndcg_at_10 << ",\n"
                << "      \"latency_ms\": " << me.avg_latency_ms << "\n"
                << "    }" << (mi + 1 < all_methods.size() ? ",\n" : "\n");
        }
        ofs << "  ]\n}\n";
    }

    // 4. comparison.csv
    {
        std::ofstream ofs("benchmarks/phase-06.5/results/comparison.csv");
        ofs << "Architecture,Method,P@1,P@3,P@5,Recall@5,Recall@10,MRR,NDCG@5,NDCG@10,LatencyMs\n";
        for (const auto& me : all_methods) {
            ofs << "\"" << me.architecture << "\",\"" << me.name << "\","
                << me.aggregate_metrics.p_at_1 << "," << me.aggregate_metrics.p_at_3 << ","
                << me.aggregate_metrics.p_at_5 << "," << me.aggregate_metrics.r_at_5 << ","
                << me.aggregate_metrics.r_at_10 << "," << me.aggregate_metrics.mrr << ","
                << me.aggregate_metrics.ndcg_at_5 << "," << me.aggregate_metrics.ndcg_at_10 << ","
                << me.avg_latency_ms << "\n";
        }
    }

    // 5. per_query.json
    {
        std::ofstream ofs("benchmarks/phase-06.5/results/per_query.json");
        ofs << "[\n";
        for (std::size_t qi = 0; qi < FROZEN_QUERIES.size(); ++qi) {
            const auto& q = FROZEN_QUERIES[qi];
            ofs << "  {\n"
                << "    \"query_id\": \"" << q.id << "\",\n"
                << "    \"query\": \"" << escape_json(q.query) << "\",\n"
                << "    \"category\": \"" << escape_json(q.category) << "\",\n"
                << "    \"target\": \"" << escape_json(q.expected_name) << "\",\n"
                << "    \"methods\": {\n";
            for (std::size_t mi = 0; mi < all_methods.size(); ++mi) {
                const auto& me = all_methods[mi];
                const auto& qr = me.query_results[qi];
                ofs << "      \"" << escape_json(me.name) << "\": {\n"
                    << "        \"top_name\": \"" << escape_json(qr.top_name) << "\",\n"
                    << "        \"top_file\": \"" << escape_json(qr.top_file) << "\",\n"
                    << "        \"top_kind\": \"" << escape_json(qr.top_kind) << "\",\n"
                    << "        \"target_rank\": " << qr.target_rank << ",\n"
                    << "        \"target_found\": " << (qr.target_found ? "true" : "false") << ",\n"
                    << "        \"score\": " << qr.score << ",\n"
                    << "        \"latency_ms\": " << qr.latency_ms;
                if (!qr.supporting_evidence_names.empty()) {
                    ofs << ",\n        \"supporting_evidence_sample\": [";
                    for (std::size_t ei = 0;
                         ei < std::min<std::size_t>(5, qr.supporting_evidence_names.size()); ++ei) {
                        ofs << "\"" << escape_json(qr.supporting_evidence_names[ei]) << "\""
                            << (ei + 1 < std::min<std::size_t>(5,
                                                               qr.supporting_evidence_names.size())
                                    ? ", "
                                    : "");
                    }
                    ofs << "]";
                }
                ofs << "\n      }" << (mi + 1 < all_methods.size() ? ",\n" : "\n");
            }
            ofs << "    }\n"
                << "  }" << (qi + 1 < FROZEN_QUERIES.size() ? ",\n" : "\n");
        }
        ofs << "]\n";
    }

    std::cout << "All benchmark artifacts written to benchmarks/phase-06.5/results/\n";
    return 0;
}
