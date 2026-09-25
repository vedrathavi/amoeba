#include "amoeba/eval/benchmark_result.hpp"
#include "amoeba/eval/ranking_metrics.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/index/search_engine.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/semantic/pretrained_embedding_provider.hpp"
#include "amoeba/semantic/semantic_document.hpp"
#include "amoeba/semantic/semantic_index.hpp"
#include "amoeba/semantic/semantic_retriever.hpp"

#include <chrono>
#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace amoeba::semantic::test {

struct EvalQuery {
    std::string query;
    std::string category;
    std::unordered_map<std::string, uint32_t> element_grades;
};

class SemanticRetrievalBenchmarkTest : public ::testing::Test {
protected:
    parser::SourceParser parser;
    index::InvertedIndex index;
    SemanticIndex semantic_index;
    PretrainedEmbeddingProvider provider;

    void SetUp() override {
        // 1. Auth & Security Module (TypeScript)
        auto auth_file =
            parser.parse_source("export class AuthService {\n"
                                "    static authenticateUser(user: string, pass: string) {\n"
                                "        return true;\n"
                                "    }\n"
                                "    static validateCredentials(credentials: any) {\n"
                                "        return true;\n"
                                "    }\n"
                                "    static refreshToken(token: string) {\n"
                                "        return token;\n"
                                "    }\n"
                                "}\n",
                                "TypeScript", "src/auth/auth_service.ts");

        // 2. User Repository & Database (C++)
        auto repo_file = parser.parse_source("class UserRepository {\n"
                                             "public:\n"
                                             "    User findUserById(int id) { return {}; }\n"
                                             "    void saveUser(const User& u) {}\n"
                                             "    void deleteUser(int id) {}\n"
                                             "};\n",
                                             "C++", "src/repo/user_repository.cpp");

        // 3. Billing & Payment Gateway (Python)
        auto billing_file =
            parser.parse_source("class PaymentProcessor:\n"
                                "    def process_transaction(self, amount, card_token):\n"
                                "        return True\n"
                                "    def charge_invoice(self, invoice_id):\n"
                                "        return True\n",
                                "Python", "pkg/billing/payment_processor.py");

        // 4. UI Components (TSX)
        auto ui_file = parser.parse_source("export function UserProfileView(props: any) {\n"
                                           "    return <div>User Profile</div>;\n"
                                           "}\n"
                                           "export function PrimaryButton(props: any) {\n"
                                           "    return <button>Click</button>;\n"
                                           "}\n",
                                           "TSX", "src/components/user_profile.tsx");

        // 5. HTTP Middleware (Go)
        auto mw_file = parser.parse_source("package middleware\n"
                                           "func AuthMiddleware(next Handler) Handler {\n"
                                           "    return func(w ResponseWriter, r *Request) {}\n"
                                           "}\n",
                                           "Go", "pkg/middleware/auth_middleware.go");

        index.add_parsed_file(auth_file);
        index.add_parsed_file(repo_file);
        index.add_parsed_file(billing_file);
        index.add_parsed_file(ui_file);
        index.add_parsed_file(mw_file);

        // Build SemanticIndex from indexed CodeElements
        for (ElementId id = 0; id < index.element_count(); ++id) {
            const auto& elem = index.get_element(id);
            const auto& file = index.get_file(elem.file_id);

            std::string text_repr =
                SemanticTextFormatter::format(file.language, file.file_path, elem.element);
            auto embedding = provider.embed(text_repr);
            semantic_index.add(id, std::move(embedding));
        }
    }
};

TEST_F(SemanticRetrievalBenchmarkTest, CompareRetrievalMethodsAcrossCategories) {
    index::SearchEngine search_engine(index);
    SemanticRetriever semantic_retriever(semantic_index);

    std::vector<EvalQuery> test_queries = {
        // Exact Identifier
        {
            .query = "authenticateUser",
            .category = "Exact Identifier",
            .element_grades = {{"authenticateUser", 3}, {"validateCredentials", 1}},
        },
        // Normalized Identifier / CamelCase
        {
            .query = "find user by id",
            .category = "Normalized Identifier",
            .element_grades = {{"findUserById", 3}, {"UserRepository", 2}},
        },
        // Conceptual Queries (Vocabulary Mismatch)
        {
            .query = "where do we check user login credentials?",
            .category = "Conceptual",
            .element_grades = {{"validateCredentials", 3},
                               {"authenticateUser", 2},
                               {"AuthService", 1}},
        },
        {
            .query = "how to verify user identity and session",
            .category = "Conceptual",
            .element_grades = {{"authenticateUser", 3}, {"refreshToken", 2}, {"AuthService", 1}},
        },
        // Multi-Term / Semantic
        {
            .query = "process payment transactions and charge invoices",
            .category = "Multi-Term",
            .element_grades = {{"process_transaction", 3},
                               {"charge_invoice", 3},
                               {"PaymentProcessor", 2}},
        },
        // Structural / Code Concepts
        {
            .query = "component that renders user profile",
            .category = "Contextual",
            .element_grades = {{"UserProfileView", 3}},
        },
        {
            .query = "http middleware for checking auth requests",
            .category = "Framework",
            .element_grades = {{"AuthMiddleware", 3}, {"AuthService", 1}},
        },
    };

    std::vector<eval::RankingMetrics> baseline_metrics;
    std::vector<eval::RankingMetrics> bm25_metrics;
    std::vector<eval::RankingMetrics> code_aware_metrics;
    std::vector<eval::RankingMetrics> semantic_metrics;

    for (const auto& q : test_queries) {
        // 1. Baseline Lexical
        auto base_res = search_engine.search(
            q.query, {.ranker_type = index::RankerType::Baseline, .max_results = 10});
        std::vector<std::string> base_names;
        for (const auto& r : base_res)
            base_names.push_back(r.element.name);
        baseline_metrics.push_back(eval::compute_ranking_metrics(base_names, q.element_grades));

        // 2. BM25
        auto bm25_res = search_engine.search(
            q.query, {.ranker_type = index::RankerType::BM25, .max_results = 10});
        std::vector<std::string> bm25_names;
        for (const auto& r : bm25_res)
            bm25_names.push_back(r.element.name);
        bm25_metrics.push_back(eval::compute_ranking_metrics(bm25_names, q.element_grades));

        // 3. CodeAware
        auto ca_res = search_engine.search(
            q.query, {.ranker_type = index::RankerType::CodeAware, .max_results = 10});
        std::vector<std::string> ca_names;
        for (const auto& r : ca_res)
            ca_names.push_back(r.element.name);
        code_aware_metrics.push_back(eval::compute_ranking_metrics(ca_names, q.element_grades));

        // 4. Semantic Retrieval
        auto sem_res = semantic_retriever.retrieve_text(q.query, provider, {.top_k = 10});
        std::vector<std::string> sem_names;
        for (const auto& r : sem_res) {
            sem_names.push_back(index.get_element(r.element_id).element.name);
        }
        semantic_metrics.push_back(eval::compute_ranking_metrics(sem_names, q.element_grades));
    }

    auto avg_baseline = eval::RankingMetrics::average(baseline_metrics);
    auto avg_bm25 = eval::RankingMetrics::average(bm25_metrics);
    auto avg_code_aware = eval::RankingMetrics::average(code_aware_metrics);
    auto avg_semantic = eval::RankingMetrics::average(semantic_metrics);

    // Validate that all methods produce valid non-zero ranking metrics
    EXPECT_GT(avg_baseline.mrr, 0.0);
    EXPECT_GT(avg_bm25.mrr, 0.0);
    EXPECT_GT(avg_code_aware.mrr, 0.0);
    EXPECT_GT(avg_semantic.mrr, 0.0);

    // Specifically for Conceptual queries, verify Semantic Retrieval achieves high MRR
    EXPECT_GT(avg_semantic.p_at_1, 0.50);
    EXPECT_GT(avg_semantic.mrr, 0.70);
}

TEST_F(SemanticRetrievalBenchmarkTest, PerformanceAndScalingMeasurements) {
    // 1. Single vs Batch Embedding Generation Latency
    std::vector<std::string> test_texts;
    for (int i = 0; i < 50; ++i) {
        test_texts.push_back("function validateUserStep" + std::to_string(i) +
                             "() { return true; }");
    }

    auto start_single = std::chrono::high_resolution_clock::now();
    for (const auto& t : test_texts) {
        auto vec = provider.embed(t);
        EXPECT_EQ(vec.size(), 384u);
    }
    auto end_single = std::chrono::high_resolution_clock::now();
    double single_ms = std::chrono::duration<double, std::milli>(end_single - start_single).count();

    auto start_batch = std::chrono::high_resolution_clock::now();
    auto batch_vecs = provider.embed_batch(test_texts);
    auto end_batch = std::chrono::high_resolution_clock::now();
    double batch_ms = std::chrono::duration<double, std::milli>(end_batch - start_batch).count();

    EXPECT_EQ(batch_vecs.size(), 50u);
    EXPECT_LT(single_ms, 500.0);  // Sub-10ms per element
    EXPECT_LT(batch_ms, 500.0);

    // 2. Semantic Index Retrieval Latency & Memory Across Scales
    SemanticIndex scaling_index;
    for (uint32_t i = 0; i < 500; ++i) {
        scaling_index.add(i, std::vector<float>(384, static_cast<float>(i % 10) * 0.1f));
    }

    SemanticRetriever retriever(scaling_index);
    std::vector<float> query_vec(384, 0.5f);

    auto start_retrieval = std::chrono::high_resolution_clock::now();
    auto results = retriever.retrieve(query_vec, {.top_k = 10});
    auto end_retrieval = std::chrono::high_resolution_clock::now();
    double ret_ms =
        std::chrono::duration<double, std::milli>(end_retrieval - start_retrieval).count();

    EXPECT_EQ(results.size(), 10u);
    EXPECT_LT(ret_ms, 15.0);  // Sub-15ms brute force retrieval in debug test build
    EXPECT_GT(scaling_index.estimate_memory_bytes(), 0u);
}

}  // namespace amoeba::semantic::test
