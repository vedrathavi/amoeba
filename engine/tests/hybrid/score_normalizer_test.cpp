#include "amoeba/hybrid/score_normalizer.hpp"

#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <vector>

using namespace amoeba::hybrid;

TEST(ScoreNormalizerTest, EmptyInputReturnsEmpty) {
    const std::vector<double> scores = {};
    const auto normalized = ScoreNormalizer::min_max_normalize(scores);
    EXPECT_TRUE(normalized.empty());
}

TEST(ScoreNormalizerTest, SingleScoreReturnsOne) {
    const std::vector<double> scores = {42.5};
    const auto normalized = ScoreNormalizer::min_max_normalize(scores);
    ASSERT_EQ(normalized.size(), 1u);
    EXPECT_DOUBLE_EQ(normalized[0], 1.0);
}

TEST(ScoreNormalizerTest, IdenticalScoresReturnAllOnes) {
    const std::vector<double> scores = {5.0, 5.0, 5.0, 5.0};
    const auto normalized = ScoreNormalizer::min_max_normalize(scores);
    ASSERT_EQ(normalized.size(), 4u);
    for (double v : normalized) {
        EXPECT_DOUBLE_EQ(v, 1.0);
    }
}

TEST(ScoreNormalizerTest, StandardRangeNormalization) {
    const std::vector<double> scores = {10.0, 20.0, 30.0, 40.0, 50.0};
    const auto normalized = ScoreNormalizer::min_max_normalize(scores);
    ASSERT_EQ(normalized.size(), 5u);
    EXPECT_DOUBLE_EQ(normalized[0], 0.0);
    EXPECT_DOUBLE_EQ(normalized[1], 0.25);
    EXPECT_DOUBLE_EQ(normalized[2], 0.5);
    EXPECT_DOUBLE_EQ(normalized[3], 0.75);
    EXPECT_DOUBLE_EQ(normalized[4], 1.0);
}

TEST(ScoreNormalizerTest, NegativeScoresHandling) {
    const std::vector<double> scores = {-10.0, -5.0, 0.0, 5.0, 10.0};
    const auto normalized = ScoreNormalizer::min_max_normalize(scores);
    ASSERT_EQ(normalized.size(), 5u);
    EXPECT_DOUBLE_EQ(normalized[0], 0.0);
    EXPECT_DOUBLE_EQ(normalized[1], 0.25);
    EXPECT_DOUBLE_EQ(normalized[2], 0.5);
    EXPECT_DOUBLE_EQ(normalized[3], 0.75);
    EXPECT_DOUBLE_EQ(normalized[4], 1.0);
}

TEST(ScoreNormalizerTest, HandlesNonFiniteValuesGracefully) {
    const double nan_val = std::numeric_limits<double>::quiet_NaN();
    const double inf_val = std::numeric_limits<double>::infinity();
    const std::vector<double> scores = {10.0, nan_val, 20.0, inf_val, 30.0};

    const auto normalized = ScoreNormalizer::min_max_normalize(scores);
    ASSERT_EQ(normalized.size(), 5u);
    EXPECT_DOUBLE_EQ(normalized[0], 0.0);
    EXPECT_DOUBLE_EQ(normalized[1], 0.0);
    EXPECT_DOUBLE_EQ(normalized[2], 0.5);
    EXPECT_DOUBLE_EQ(normalized[3], 0.0);
    EXPECT_DOUBLE_EQ(normalized[4], 1.0);
}
