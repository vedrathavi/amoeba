#include "amoeba/hybrid/fusion_strategy.hpp"

#include <gtest/gtest.h>
#include <vector>

using namespace amoeba::hybrid;
using namespace amoeba::parser;

TEST(FusionStrategyTest, WeightedScoreAlphaOnePureLexical) {
    ScoredCandidate c1;
    c1.element_id = 1;
    c1.element.name = "elem1";
    c1.normalized_lexical_score = 0.8;
    c1.normalized_semantic_score = 0.2;

    ScoredCandidate c2;
    c2.element_id = 2;
    c2.element.name = "elem2";
    c2.normalized_lexical_score = 0.4;
    c2.normalized_semantic_score = 0.9;

    std::vector<ScoredCandidate> candidates = {c1, c2};

    const auto fused = FusionStrategy::fuse_weighted(candidates, 1.0);
    ASSERT_EQ(fused.size(), 2u);
    EXPECT_EQ(fused[0].element_id, 1u);
    EXPECT_DOUBLE_EQ(fused[0].fused_score, 0.8);
    EXPECT_EQ(fused[1].element_id, 2u);
    EXPECT_DOUBLE_EQ(fused[1].fused_score, 0.4);
}

TEST(FusionStrategyTest, WeightedScoreAlphaZeroPureSemantic) {
    ScoredCandidate c1;
    c1.element_id = 1;
    c1.element.name = "elem1";
    c1.normalized_lexical_score = 0.8;
    c1.normalized_semantic_score = 0.2;

    ScoredCandidate c2;
    c2.element_id = 2;
    c2.element.name = "elem2";
    c2.normalized_lexical_score = 0.4;
    c2.normalized_semantic_score = 0.9;

    std::vector<ScoredCandidate> candidates = {c1, c2};

    const auto fused = FusionStrategy::fuse_weighted(candidates, 0.0);
    ASSERT_EQ(fused.size(), 2u);
    EXPECT_EQ(fused[0].element_id, 2u);
    EXPECT_DOUBLE_EQ(fused[0].fused_score, 0.9);
    EXPECT_EQ(fused[1].element_id, 1u);
    EXPECT_DOUBLE_EQ(fused[1].fused_score, 0.2);
}

TEST(FusionStrategyTest, WeightedScoreEqualWeights) {
    ScoredCandidate c1;
    c1.element_id = 1;
    c1.element.name = "elem1";
    c1.normalized_lexical_score = 0.8;
    c1.normalized_semantic_score = 0.4;  // 0.5*0.8 + 0.5*0.4 = 0.60

    ScoredCandidate c2;
    c2.element_id = 2;
    c2.element.name = "elem2";
    c2.normalized_lexical_score = 0.6;
    c2.normalized_semantic_score = 0.8;  // 0.5*0.6 + 0.5*0.8 = 0.70

    std::vector<ScoredCandidate> candidates = {c1, c2};

    const auto fused = FusionStrategy::fuse_weighted(candidates, 0.5);
    ASSERT_EQ(fused.size(), 2u);
    EXPECT_EQ(fused[0].element_id, 2u);
    EXPECT_DOUBLE_EQ(fused[0].fused_score, 0.7);
    EXPECT_EQ(fused[1].element_id, 1u);
    EXPECT_DOUBLE_EQ(fused[1].fused_score, 0.6);
}

TEST(FusionStrategyTest, DeterministicTieBreakingByElementId) {
    ScoredCandidate c1;
    c1.element_id = 42;
    c1.element.name = "elem42";
    c1.normalized_lexical_score = 0.5;
    c1.normalized_semantic_score = 0.5;

    ScoredCandidate c2;
    c2.element_id = 7;
    c2.element.name = "elem7";
    c2.normalized_lexical_score = 0.5;
    c2.normalized_semantic_score = 0.5;

    ScoredCandidate c3;
    c3.element_id = 19;
    c3.element.name = "elem19";
    c3.normalized_lexical_score = 0.5;
    c3.normalized_semantic_score = 0.5;

    std::vector<ScoredCandidate> candidates = {c1, c2, c3};

    const auto fused = FusionStrategy::fuse_weighted(candidates, 0.5);
    ASSERT_EQ(fused.size(), 3u);
    EXPECT_EQ(fused[0].element_id, 7u);
    EXPECT_EQ(fused[1].element_id, 19u);
    EXPECT_EQ(fused[2].element_id, 42u);
}

TEST(FusionStrategyTest, ReciprocalRankFusionCalculation) {
    ScoredCandidate c1;
    c1.element_id = 1;
    c1.element.name = "elem1";
    c1.lexical_rank = 1;
    c1.semantic_rank = 3;

    ScoredCandidate c2;
    c2.element_id = 2;
    c2.element.name = "elem2";
    c2.lexical_rank = 2;
    c2.semantic_rank = 1;

    ScoredCandidate c3;
    c3.element_id = 3;
    c3.element.name = "elem3";
    c3.lexical_rank = 0;
    c3.semantic_rank = 2;

    std::vector<ScoredCandidate> candidates = {c1, c2, c3};

    const auto fused = FusionStrategy::fuse_rrf(candidates, 60.0);
    ASSERT_EQ(fused.size(), 3u);
    EXPECT_EQ(fused[0].element_id, 2u);
    EXPECT_EQ(fused[1].element_id, 1u);
    EXPECT_EQ(fused[2].element_id, 3u);
}
