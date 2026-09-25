#include "amoeba/eval/benchmark_result.hpp"
#include "amoeba/eval/ranking_metrics.hpp"

#include <gtest/gtest.h>
#include <vector>

namespace amoeba::eval::test {

TEST(RankingMetricsTest, PerfectRankingYieldsUnitMetrics) {
    std::vector<std::string> retrieved = {"docA", "docB", "docC"};
    std::unordered_map<std::string, uint32_t> grades = {
        {"docA", 3},
        {"docB", 2},
        {"docC", 1},
    };

    auto metrics = compute_ranking_metrics(retrieved, grades);
    EXPECT_DOUBLE_EQ(metrics.p_at_1, 1.0);
    EXPECT_DOUBLE_EQ(metrics.p_at_3, 1.0);
    EXPECT_DOUBLE_EQ(metrics.mrr, 1.0);
    EXPECT_DOUBLE_EQ(metrics.ndcg_at_5, 1.0);
}

TEST(RankingMetricsTest, SuboptimalRankingMetrics) {
    std::vector<std::string> retrieved = {"docIrrelevant", "docA", "docB"};
    std::unordered_map<std::string, uint32_t> grades = {
        {"docA", 3},
        {"docB", 2},
    };

    auto metrics = compute_ranking_metrics(retrieved, grades);
    EXPECT_DOUBLE_EQ(metrics.p_at_1, 0.0);
    EXPECT_DOUBLE_EQ(metrics.mrr, 0.5);  // First hit at rank 2
    EXPECT_GT(metrics.ndcg_at_5, 0.0);
    EXPECT_LT(metrics.ndcg_at_5, 1.0);
}

TEST(RankingMetricsTest, AverageMetrics) {
    RankingMetrics m1{.p_at_1 = 1.0, .mrr = 1.0};
    RankingMetrics m2{.p_at_1 = 0.0, .mrr = 0.5};

    std::vector<RankingMetrics> list = {m1, m2};
    auto avg = RankingMetrics::average(list);

    EXPECT_DOUBLE_EQ(avg.p_at_1, 0.5);
    EXPECT_DOUBLE_EQ(avg.mrr, 0.75);
}

TEST(BenchmarkResultTest, SerializationFormats) {
    BenchmarkResult res{
        .experiment_name = "phase6_baseline",
        .dataset_name = "eval_corpus_v1",
        .method_name = "semantic_brute_force",
        .query_count = 10,
        .corpus_files = 50,
        .corpus_elements = 250,
        .corpus_terms = 1200,
        .corpus_relationships = 400,
        .embedding_dimensions = 64,
        .indexing_time_ms = 12.5,
        .query_latency_ms = 0.15,
        .memory_bytes = 45000,
        .metrics = {.p_at_1 = 0.8, .p_at_3 = 0.7, .p_at_5 = 0.6, .mrr = 0.85, .ndcg_at_5 = 0.82},
        .timestamp = "2026-09-25T22:00:00Z",
        .build_configuration = "Release",
        .environment_info = "Windows x86_64",
    };

    std::string json_str = res.to_json();
    EXPECT_NE(json_str.find("\"experiment\": \"phase6_baseline\""), std::string::npos);
    EXPECT_NE(json_str.find("\"p_at_1\": 0.8000"), std::string::npos);

    std::string csv_row = res.to_csv_row();
    EXPECT_NE(csv_row.find("phase6_baseline,eval_corpus_v1,semantic_brute_force"),
              std::string::npos);
}

}  // namespace amoeba::eval::test
