#include "amoeba/hybrid/hybrid_retriever.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/semantic/deterministic_embedding_provider.hpp"
#include "amoeba/semantic/semantic_document.hpp"

#include <gtest/gtest.h>

using namespace amoeba;
using namespace amoeba::hybrid;
using namespace amoeba::index;
using namespace amoeba::parser;
using namespace amoeba::semantic;

class HybridRetrieverTest : public ::testing::Test {
protected:
    SourceParser parser;
    InvertedIndex inverted_index;
    SemanticIndex semantic_index;
    DeterministicEmbeddingProvider embedding_provider{64};

    void SetUp() override {
        auto auth_file =
            parser.parse_source("export class AuthService {\n"
                                "    static authenticateUser(user: string, pass: string) {\n"
                                "        return true;\n"
                                "    }\n"
                                "    static validateCredentials(credentials: any) {\n"
                                "        return true;\n"
                                "    }\n"
                                "}\n",
                                "TypeScript", "src/auth/auth_service.ts");

        auto repo_file = parser.parse_source("class UserRepository {\n"
                                             "public:\n"
                                             "    User findUserById(int id) { return {}; }\n"
                                             "};\n",
                                             "C++", "src/repo/user_repository.cpp");

        auto billing_file = parser.parse_source("class PaymentProcessor:\n"
                                                "    def process_transaction(self, amount):\n"
                                                "        return True\n",
                                                "Python", "pkg/billing/payment_processor.py");

        inverted_index.add_parsed_file(auth_file);
        inverted_index.add_parsed_file(repo_file);
        inverted_index.add_parsed_file(billing_file);

        // Build embeddings for all parsed elements
        for (ElementId id = 0; id < inverted_index.element_count(); ++id) {
            const auto& elem = inverted_index.get_element(id);
            const auto& file = inverted_index.get_file(elem.file_id);
            const std::string text =
                SemanticTextFormatter::format(file.language, file.file_path, elem.element);
            auto emb = embedding_provider.embed(text);
            semantic_index.add(id, std::move(emb));
        }
    }
};

TEST_F(HybridRetrieverTest, EmptyQueryReturnsEmpty) {
    HybridRetriever retriever(inverted_index, semantic_index, embedding_provider);
    const auto results = retriever.search("");
    EXPECT_TRUE(results.empty());
}

TEST_F(HybridRetrieverTest, AlphaOneMatchesLexicalOrdering) {
    HybridRetriever retriever(inverted_index, semantic_index, embedding_provider);
    SearchEngine lexical_engine(inverted_index);

    const auto lex_results = lexical_engine.search(
        "authenticateUser", {.ranker_type = RankerType::CodeAware, .max_results = 5});
    const auto hybrid_results =
        retriever.search("authenticateUser",
                         {.alpha = 1.0, .lexical_ranker = RankerType::CodeAware, .max_results = 5});

    ASSERT_GE(hybrid_results.size(), lex_results.size());
    for (std::size_t i = 0; i < lex_results.size(); ++i) {
        EXPECT_EQ(hybrid_results[i].element.name, lex_results[i].element.name);
        EXPECT_EQ(hybrid_results[i].lexical_rank, static_cast<uint32_t>(i + 1));
    }
}

TEST_F(HybridRetrieverTest, AlphaZeroMatchesSemanticOrdering) {
    HybridRetriever retriever(inverted_index, semantic_index, embedding_provider);
    SemanticRetriever sem_retriever(semantic_index);

    const auto sem_results =
        sem_retriever.retrieve_text("authenticate user login", embedding_provider, {.top_k = 5});
    const auto hybrid_results = retriever.search(
        "authenticate user login", {.alpha = 0.0, .semantic_top_k = 5, .max_results = 5});

    ASSERT_FALSE(hybrid_results.empty());
    ASSERT_EQ(hybrid_results.size(), sem_results.size());
    for (std::size_t i = 0; i < hybrid_results.size(); ++i) {
        EXPECT_EQ(hybrid_results[i].element_id, sem_results[i].element_id);
        EXPECT_EQ(hybrid_results[i].semantic_rank, static_cast<uint32_t>(i + 1));
    }
}

TEST_F(HybridRetrieverTest, BalancedHybridCombinesScores) {
    HybridRetriever retriever(inverted_index, semantic_index, embedding_provider);
    const auto results = retriever.search("validateCredentials", {.alpha = 0.5, .max_results = 5});

    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].element.name, "validateCredentials");
    EXPECT_GE(results[0].normalized_lexical_score, 0.0);
    EXPECT_LE(results[0].normalized_lexical_score, 1.0);
    EXPECT_GE(results[0].normalized_semantic_score, 0.0);
    EXPECT_LE(results[0].normalized_semantic_score, 1.0);
    EXPECT_GE(results[0].hybrid_score, 0.0);
    EXPECT_LE(results[0].hybrid_score, 1.0);
}

TEST_F(HybridRetrieverTest, KindFilterRestrictsCandidates) {
    HybridRetriever retriever(inverted_index, semantic_index, embedding_provider);
    const auto class_results = retriever.search(
        "Auth", {.alpha = 0.5, .max_results = 5, .kind_filter = ElementKind::Class});

    for (const auto& r : class_results) {
        EXPECT_EQ(r.element.kind, ElementKind::Class);
    }
}

TEST_F(HybridRetrieverTest, ReciprocalRankFusionExecution) {
    HybridRetriever retriever(inverted_index, semantic_index, embedding_provider);
    const auto results = retriever.search(
        "findUserById",
        {.fusion_method = FusionMethod::ReciprocalRank, .rrf_k = 60.0, .max_results = 5});

    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].element.name, "findUserById");
    EXPECT_GT(results[0].hybrid_score, 0.0);
}
