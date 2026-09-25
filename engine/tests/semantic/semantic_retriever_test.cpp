#include "amoeba/semantic/deterministic_embedding_provider.hpp"
#include "amoeba/semantic/semantic_index.hpp"
#include "amoeba/semantic/semantic_retriever.hpp"

#include <gtest/gtest.h>

namespace amoeba::semantic::test {

TEST(SemanticRetrieverTest, SpecificationExampleRanking) {
    // Vector specification test:
    // A = [1.0, 0.0]
    // B = [0.9, 0.1]
    // C = [0.0, 1.0]
    // Query = [1.0, 0.0] -> Expected ranking: A, B, C

    SemanticIndex index;
    index.add(1, {1.0f, 0.0f});
    index.add(2, {0.9f, 0.1f});
    index.add(3, {0.0f, 1.0f});

    SemanticRetriever retriever(index);

    std::vector<float> query = {1.0f, 0.0f};
    auto results = retriever.retrieve(query, {.top_k = 10});

    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0].element_id, 1u);
    EXPECT_EQ(results[1].element_id, 2u);
    EXPECT_EQ(results[2].element_id, 3u);

    EXPECT_GT(results[0].similarity_score, results[1].similarity_score);
    EXPECT_GT(results[1].similarity_score, results[2].similarity_score);
}

TEST(SemanticRetrieverTest, TopKConstraint) {
    SemanticIndex index;
    for (uint32_t i = 0; i < 10; ++i) {
        index.add(i, {1.0f, static_cast<float>(i)});
    }

    SemanticRetriever retriever(index);
    std::vector<float> query = {1.0f, 0.0f};

    auto top3 = retriever.retrieve(query, {.top_k = 3});
    EXPECT_EQ(top3.size(), 3u);

    auto top20 = retriever.retrieve(query, {.top_k = 20});
    EXPECT_EQ(top20.size(), 10u);
}

TEST(SemanticRetrieverTest, EmptyIndexReturnsEmpty) {
    SemanticIndex index;
    SemanticRetriever retriever(index);

    std::vector<float> query = {1.0f, 0.0f};
    auto results = retriever.retrieve(query);
    EXPECT_TRUE(results.empty());
}

TEST(SemanticRetrieverTest, DeterministicTieBreaking) {
    // Elements with identical vectors/scores must break ties deterministically on ElementId
    SemanticIndex index;
    index.add(42, {1.0f, 1.0f});
    index.add(10, {1.0f, 1.0f});
    index.add(5, {1.0f, 1.0f});

    SemanticRetriever retriever(index);
    std::vector<float> query = {1.0f, 1.0f};

    auto results = retriever.retrieve(query, {.top_k = 10});
    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0].element_id, 5u);
    EXPECT_EQ(results[1].element_id, 10u);
    EXPECT_EQ(results[2].element_id, 42u);
}

TEST(SemanticRetrieverTest, RetrieveTextWithProvider) {
    DeterministicEmbeddingProvider provider(32);
    SemanticIndex index;

    auto vec_auth = provider.embed("authenticate user login credentials");
    auto vec_repo = provider.embed("user database sql query repository");

    index.add(1, vec_auth);
    index.add(2, vec_repo);

    SemanticRetriever retriever(index);

    auto results = retriever.retrieve_text("login credentials", provider, {.top_k = 5});
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results.front().element_id, 1u);
}

}  // namespace amoeba::semantic::test
