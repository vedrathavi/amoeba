#include "amoeba/semantic/semantic_index.hpp"

#include <gtest/gtest.h>

namespace amoeba::semantic::test {

TEST(SemanticIndexTest, AddLookupAndSize) {
    SemanticIndex index;
    EXPECT_TRUE(index.empty());
    EXPECT_EQ(index.size(), 0u);

    Embedding emb1{.element_id = 10, .values = {1.0f, 0.0f, 0.0f}};
    EXPECT_TRUE(index.add(emb1));
    EXPECT_EQ(index.size(), 1u);
    EXPECT_EQ(index.dimensions(), 3u);
    EXPECT_TRUE(index.contains(10));

    const auto* found = index.lookup(10);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->element_id, 10u);
    EXPECT_EQ(found->values, emb1.values);

    // Duplicate add returns false
    EXPECT_FALSE(index.add(emb1));
}

TEST(SemanticIndexTest, RejectDimensionMismatch) {
    SemanticIndex index;
    EXPECT_TRUE(index.add(1, {1.0f, 2.0f}));
    EXPECT_EQ(index.dimensions(), 2u);

    // Adding 3D vector to 2D index must fail
    EXPECT_FALSE(index.add(2, {1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(index.size(), 1u);
}

TEST(SemanticIndexTest, ReplaceExistingEmbedding) {
    SemanticIndex index;
    index.add(10, {1.0f, 0.0f});

    Embedding new_emb{.element_id = 10, .values = {0.0f, 1.0f}};
    EXPECT_TRUE(index.replace(new_emb));  // true because existed

    const auto* found = index.lookup(10);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->values[1], 1.0f);
}

TEST(SemanticIndexTest, RemoveAndClear) {
    SemanticIndex index;
    index.add(1, {1.0f, 0.0f});
    index.add(2, {0.0f, 1.0f});
    EXPECT_EQ(index.size(), 2u);

    EXPECT_TRUE(index.remove(1));
    EXPECT_EQ(index.size(), 1u);
    EXPECT_FALSE(index.contains(1));
    EXPECT_FALSE(index.remove(99));  // Non-existent

    index.clear();
    EXPECT_TRUE(index.empty());
    EXPECT_EQ(index.size(), 0u);
    EXPECT_EQ(index.dimensions(), 0u);
}

TEST(SemanticIndexTest, MemoryEstimation) {
    SemanticIndex index;
    index.add(1, std::vector<float>(64, 0.5f));
    index.add(2, std::vector<float>(64, 0.5f));

    std::size_t mem = index.estimate_memory_bytes();
    EXPECT_GT(mem, 0u);
}

}  // namespace amoeba::semantic::test
