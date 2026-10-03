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

TEST(FusionStrategyTest, RRFK10SmoothingCalculation) {
    ScoredCandidate c1;
    c1.element_id = 1;
    c1.element.name = "elem1";
    c1.lexical_rank = 1;      // 1 / (10 + 1) = 1/11 ~ 0.090909
    c1.semantic_rank = 0;

    ScoredCandidate c2;
    c2.element_id = 2;
    c2.element.name = "elem2";
    c2.lexical_rank = 2;      // 1 / (10 + 2) = 1/12 ~ 0.083333
    c2.semantic_rank = 5;     // 1 / (10 + 5) = 1/15 ~ 0.066667 -> total ~ 0.150000

    std::vector<ScoredCandidate> candidates = {c1, c2};
    const auto fused = FusionStrategy::fuse_rrf(candidates, 10.0);
    ASSERT_EQ(fused.size(), 2u);
    EXPECT_EQ(fused[0].element_id, 2u);
    EXPECT_NEAR(fused[0].fused_score, (1.0 / 12.0) + (1.0 / 15.0), 1e-6);
    EXPECT_EQ(fused[1].element_id, 1u);
    EXPECT_NEAR(fused[1].fused_score, (1.0 / 11.0), 1e-6);
}

TEST(FusionStrategyTest, RRFStrongLexicalWeakSemantic) {
    ScoredCandidate c1;
    c1.element_id = 1;
    c1.element.name = "lex_strong";
    c1.lexical_rank = 1;      // 1 / 11 ~ 0.0909
    c1.semantic_rank = 40;    // 1 / 50 = 0.0200 -> total ~ 0.1109

    ScoredCandidate c2;
    c2.element_id = 2;
    c2.element.name = "sem_moderate";
    c2.lexical_rank = 25;     // 1 / 35 ~ 0.0285
    c2.semantic_rank = 10;    // 1 / 20 = 0.0500 -> total ~ 0.0785

    std::vector<ScoredCandidate> candidates = {c2, c1};
    const auto fused = FusionStrategy::fuse_rrf(candidates, 10.0);
    ASSERT_EQ(fused.size(), 2u);
    EXPECT_EQ(fused[0].element_id, 1u);
    EXPECT_EQ(fused[1].element_id, 2u);
}

TEST(FusionStrategyTest, RRFStrongSemanticWeakLexical) {
    ScoredCandidate c1;
    c1.element_id = 1;
    c1.element.name = "sem_strong";
    c1.lexical_rank = 45;     // 1 / 55 ~ 0.0181
    c1.semantic_rank = 1;     // 1 / 11 ~ 0.0909 -> total ~ 0.1090

    ScoredCandidate c2;
    c2.element_id = 2;
    c2.element.name = "lex_moderate";
    c2.lexical_rank = 20;     // 1 / 30 ~ 0.0333
    c2.semantic_rank = 25;    // 1 / 35 ~ 0.0285 -> total ~ 0.0618

    std::vector<ScoredCandidate> candidates = {c2, c1};
    const auto fused = FusionStrategy::fuse_rrf(candidates, 10.0);
    ASSERT_EQ(fused.size(), 2u);
    EXPECT_EQ(fused[0].element_id, 1u);
    EXPECT_EQ(fused[1].element_id, 2u);
}

TEST(FusionStrategyTest, RRFBothModalitiesAgree) {
    ScoredCandidate c1;
    c1.element_id = 1;
    c1.element.name = "dual_top";
    c1.lexical_rank = 1;      // 1 / 11
    c1.semantic_rank = 1;     // 1 / 11 -> total ~ 0.1818

    ScoredCandidate c2;
    c2.element_id = 2;
    c2.element.name = "single_top";
    c2.lexical_rank = 1;      // 1 / 11 -> total ~ 0.0909
    c2.semantic_rank = 0;

    std::vector<ScoredCandidate> candidates = {c2, c1};
    const auto fused = FusionStrategy::fuse_rrf(candidates, 10.0);
    ASSERT_EQ(fused.size(), 2u);
    EXPECT_EQ(fused[0].element_id, 1u);
    EXPECT_EQ(fused[1].element_id, 2u);
}

TEST(FusionStrategyTest, RRFTargetOnlyInLexicalOrSemantic) {
    ScoredCandidate c_lex;
    c_lex.element_id = 10;
    c_lex.element.name = "lex_only";
    c_lex.lexical_rank = 2;   // 1 / 12 ~ 0.0833
    c_lex.semantic_rank = 0;

    ScoredCandidate c_sem;
    c_sem.element_id = 20;
    c_sem.element.name = "sem_only";
    c_sem.lexical_rank = 0;
    c_sem.semantic_rank = 3;  // 1 / 13 ~ 0.0769

    std::vector<ScoredCandidate> candidates = {c_sem, c_lex};
    const auto fused = FusionStrategy::fuse_rrf(candidates, 10.0);
    ASSERT_EQ(fused.size(), 2u);
    EXPECT_EQ(fused[0].element_id, 10u);
    EXPECT_EQ(fused[1].element_id, 20u);
}

TEST(FusionStrategyTest, RRFUnretrievedCandidatesScoreZeroAndTieBreakDeterministically) {
    ScoredCandidate c1;
    c1.element_id = 100;
    c1.element.name = "unretrieved_high_id";
    c1.lexical_rank = 0;
    c1.semantic_rank = 0;

    ScoredCandidate c2;
    c2.element_id = 5;
    c2.element.name = "unretrieved_low_id";
    c2.lexical_rank = 0;
    c2.semantic_rank = 0;

    ScoredCandidate c3;
    c3.element_id = 50;
    c3.element.name = "retrieved";
    c3.lexical_rank = 10;
    c3.semantic_rank = 0;

    std::vector<ScoredCandidate> candidates = {c1, c2, c3};
    const auto fused = FusionStrategy::fuse_rrf(candidates, 10.0);
    ASSERT_EQ(fused.size(), 3u);
    EXPECT_EQ(fused[0].element_id, 50u);
    EXPECT_EQ(fused[1].element_id, 5u);
    EXPECT_EQ(fused[2].element_id, 100u);
}

