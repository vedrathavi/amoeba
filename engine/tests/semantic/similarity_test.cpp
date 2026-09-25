#include "amoeba/semantic/similarity.hpp"

#include <gtest/gtest.h>
#include <vector>

namespace amoeba::semantic::test {

TEST(SimilarityTest, IdenticalVectorsHaveUnitSimilarity) {
    std::vector<float> a = {1.0f, 0.0f, 0.0f};
    std::vector<float> b = {1.0f, 0.0f, 0.0f};
    EXPECT_NEAR(cosine_similarity(a, b), 1.0f, 1e-5f);

    std::vector<float> a2 = {0.5f, 0.5f, 0.5f, 0.5f};
    std::vector<float> b2 = {0.5f, 0.5f, 0.5f, 0.5f};
    EXPECT_NEAR(cosine_similarity(a2, b2), 1.0f, 1e-5f);
}

TEST(SimilarityTest, OrthogonalVectorsHaveZeroSimilarity) {
    std::vector<float> a = {1.0f, 0.0f};
    std::vector<float> b = {0.0f, 1.0f};
    EXPECT_NEAR(cosine_similarity(a, b), 0.0f, 1e-5f);
}

TEST(SimilarityTest, OppositeVectorsHaveNegativeUnitSimilarity) {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    std::vector<float> b = {-1.0f, -2.0f, -3.0f};
    EXPECT_NEAR(cosine_similarity(a, b), -1.0f, 1e-5f);
}

TEST(SimilarityTest, ZeroVectorHandledSafelyWithoutNaN) {
    std::vector<float> zero = {0.0f, 0.0f, 0.0f};
    std::vector<float> normal = {1.0f, 2.0f, 3.0f};

    EXPECT_FLOAT_EQ(cosine_similarity(zero, normal), 0.0f);
    EXPECT_FLOAT_EQ(cosine_similarity(normal, zero), 0.0f);
    EXPECT_FLOAT_EQ(cosine_similarity(zero, zero), 0.0f);
}

TEST(SimilarityTest, DimensionMismatchHandledSafely) {
    std::vector<float> a = {1.0f, 2.0f};
    std::vector<float> b = {1.0f, 2.0f, 3.0f};
    EXPECT_FLOAT_EQ(cosine_similarity(a, b), 0.0f);
}

TEST(SimilarityTest, EmptyVectorsHandledSafely) {
    std::vector<float> empty;
    std::vector<float> normal = {1.0f, 2.0f};
    EXPECT_FLOAT_EQ(cosine_similarity(empty, normal), 0.0f);
    EXPECT_FLOAT_EQ(cosine_similarity(normal, empty), 0.0f);
    EXPECT_FLOAT_EQ(cosine_similarity(empty, empty), 0.0f);
}

TEST(SimilarityTest, EmbeddingStructOverload) {
    Embedding e1{.element_id = 1, .values = {1.0f, 0.0f}};
    Embedding e2{.element_id = 2, .values = {0.0f, 1.0f}};
    EXPECT_NEAR(cosine_similarity(e1, e2), 0.0f, 1e-5f);
}

}  // namespace amoeba::semantic::test
