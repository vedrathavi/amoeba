#include "amoeba/semantic/hnsw_semantic_index.hpp"
#include "amoeba/semantic/deterministic_embedding_provider.hpp"
#include "amoeba/semantic/semantic_index.hpp"

#include <fstream>
#include <gtest/gtest.h>
#include <random>
#include <vector>

namespace amoeba::semantic {
namespace {

TEST(HnswSemanticIndexTest, EmptyIndexReturnsEmptySearchResults) {
    HnswSemanticIndex index;
    EXPECT_TRUE(index.empty());
    EXPECT_EQ(index.size(), 0);
    EXPECT_EQ(index.dimensions(), 0);

    std::vector<float> query = {1.0f, 0.0f, 0.0f};
    auto results = index.search(query, 5);
    EXPECT_TRUE(results.empty());
}

TEST(HnswSemanticIndexTest, SingleVectorInsertionAndSearch) {
    HnswSemanticIndex index;
    std::vector<float> vec = {0.6f, 0.8f, 0.0f};
    EXPECT_TRUE(index.add(42, vec));

    EXPECT_FALSE(index.empty());
    EXPECT_EQ(index.size(), 1);
    EXPECT_EQ(index.dimensions(), 3);
    EXPECT_TRUE(index.contains(42));
    EXPECT_FALSE(index.contains(99));

    const auto* lookup_emb = index.lookup(42);
    ASSERT_NE(lookup_emb, nullptr);
    EXPECT_EQ(lookup_emb->element_id, 42);
    EXPECT_EQ(lookup_emb->values, vec);

    // Search with identical vector should return element_id 42 with similarity ~1.0
    auto results = index.search(vec, 5);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].element_id, 42);
    EXPECT_NEAR(results[0].similarity_score, 1.0f, 1e-4f);
}

TEST(HnswSemanticIndexTest, MultipleVectorsExactNearestNeighbor) {
    HnswSemanticIndex index;
    // Normalized 3D vectors
    EXPECT_TRUE(index.add(101, {1.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(index.add(102, {0.0f, 1.0f, 0.0f}));
    EXPECT_TRUE(index.add(103, {0.0f, 0.0f, 1.0f}));
    EXPECT_TRUE(index.add(104, {0.7071f, 0.7071f, 0.0f}));

    EXPECT_EQ(index.size(), 4);

    // Query close to (1, 0, 0)
    std::vector<float> query = {0.99f, 0.1f, 0.0f};
    // Normalize query
    float norm = std::sqrt(query[0] * query[0] + query[1] * query[1]);
    query[0] /= norm;
    query[1] /= norm;

    auto results = index.search(query, 2);
    ASSERT_GE(results.size(), 2);
    EXPECT_EQ(results[0].element_id, 101);
    EXPECT_GT(results[0].similarity_score, results[1].similarity_score);
}

TEST(HnswSemanticIndexTest, TopKResultCountAndKLargerThanIndex) {
    HnswSemanticIndex index;
    for (uint32_t i = 1; i <= 5; ++i) {
        std::vector<float> vec(4, 0.0f);
        vec[i % 4] = 1.0f;
        index.add(i * 10, vec);
    }

    std::vector<float> query = {1.0f, 0.0f, 0.0f, 0.0f};

    // top_k = 2
    auto top2 = index.search(query, 2);
    EXPECT_EQ(top2.size(), 2);

    // top_k = 10 (larger than index size = 5)
    auto top10 = index.search(query, 10);
    EXPECT_EQ(top10.size(), 5);
}

TEST(HnswSemanticIndexTest, DeterministicIdMapping) {
    HnswSemanticIndex index;
    // Non-contiguous, out-of-order element IDs
    const std::vector<ElementId> ids = {9999, 42, 1000000, 7, 500};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        std::vector<float> vec(8, 0.0f);
        vec[i % 8] = 1.0f;
        EXPECT_TRUE(index.add(ids[i], vec));
    }

    auto all_ids = index.all_element_ids();
    std::vector<ElementId> expected_sorted = {7, 42, 500, 9999, 1000000};
    EXPECT_EQ(all_ids, expected_sorted);

    for (std::size_t i = 0; i < ids.size(); ++i) {
        std::vector<float> q(8, 0.0f);
        q[i % 8] = 1.0f;
        auto res = index.search(q, 1);
        ASSERT_FALSE(res.empty());
        EXPECT_EQ(res[0].element_id, ids[i]);
    }
}

TEST(HnswSemanticIndexTest, VectorDimensionalityValidation) {
    HnswSemanticIndex index;
    EXPECT_TRUE(index.add(1, {1.0f, 0.0f, 0.0f, 0.0f})); // 4-dim
    EXPECT_EQ(index.dimensions(), 4);

    // Adding 3-dim vector should fail
    EXPECT_FALSE(index.add(2, {1.0f, 0.0f, 0.0f}));
    EXPECT_EQ(index.size(), 1);

    // Searching with 3-dim query should return empty
    auto res = index.search(std::vector<float>{1.0f, 0.0f, 0.0f}, 5);
    EXPECT_TRUE(res.empty());
}

TEST(HnswSemanticIndexTest, DuplicateAndRepeatedInsertionBehavior) {
    HnswSemanticIndex index;
    EXPECT_TRUE(index.add(42, {1.0f, 0.0f}));
    // Duplicate add should return false
    EXPECT_FALSE(index.add(42, {0.0f, 1.0f}));
    EXPECT_EQ(index.size(), 1);

    // Replace should succeed
    EXPECT_TRUE(index.replace(Embedding{.element_id = 42, .values = {0.0f, 1.0f}}));
    EXPECT_EQ(index.size(), 1);

    const auto* emb = index.lookup(42);
    ASSERT_NE(emb, nullptr);
    EXPECT_EQ(emb->values, (std::vector<float>{0.0f, 1.0f}));
}

TEST(HnswSemanticIndexTest, RebuildAndClearBehavior) {
    HnswSemanticIndex index;
    index.add(1, {1.0f, 0.0f, 0.0f});
    index.add(2, {0.0f, 1.0f, 0.0f});
    index.add(3, {0.0f, 0.0f, 1.0f});

    EXPECT_EQ(index.size(), 3);
    EXPECT_TRUE(index.remove(2));
    EXPECT_EQ(index.size(), 2);
    EXPECT_FALSE(index.contains(2));
    EXPECT_TRUE(index.contains(1));
    EXPECT_TRUE(index.contains(3));

    // Clear
    index.clear();
    EXPECT_TRUE(index.empty());
    EXPECT_EQ(index.size(), 0);
    EXPECT_EQ(index.dimensions(), 0);

    // Re-use with different dimensions after clear
    EXPECT_TRUE(index.add(10, {1.0f, 0.0f, 0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(index.dimensions(), 5);
    EXPECT_EQ(index.size(), 1);
}

TEST(HnswSemanticIndexTest, ExactVsHnswResultCompatibility) {
    // Populate both Exact and HNSW indices with 50 deterministic 32-dim vectors
    ExactSemanticIndex exact_index;
    HnswConfig cfg{.m = 16, .ef_construction = 200, .ef_search = 64, .random_seed = 42};
    HnswSemanticIndex hnsw_index(cfg);

    DeterministicEmbeddingProvider provider;

    for (uint32_t i = 1; i <= 50; ++i) {
        std::string text = "symbol_document_representation_for_testing_" + std::to_string(i * 17);
        auto vec = provider.embed(text);
        exact_index.add(i, vec);
        hnsw_index.add(i, vec);
    }

    ASSERT_EQ(exact_index.size(), 50);
    ASSERT_EQ(hnsw_index.size(), 50);

    // Test multiple queries
    for (uint32_t q = 1; q <= 10; ++q) {
        std::string query_text = "query_search_target_text_" + std::to_string(q * 23);
        auto query_vec = provider.embed(query_text);

        auto exact_res = exact_index.search(query_vec, 10);
        auto hnsw_res = hnsw_index.search(query_vec, 10);

        ASSERT_FALSE(exact_res.empty());
        ASSERT_FALSE(hnsw_res.empty());

        // Top-1 should match on exact or near-identical score
        EXPECT_EQ(exact_res[0].element_id, hnsw_res[0].element_id);
        EXPECT_NEAR(exact_res[0].similarity_score, hnsw_res[0].similarity_score, 1e-4f);
    }
}

TEST(HnswSemanticIndexTest, FactoryCreatesBothImplementations) {
    auto exact = create_semantic_index(SemanticIndexType::Exact);
    EXPECT_NE(exact, nullptr);
    EXPECT_EQ(exact->size(), 0);

    auto hnsw = create_semantic_index(SemanticIndexType::Hnsw);
    EXPECT_NE(hnsw, nullptr);
    EXPECT_EQ(hnsw->size(), 0);
}

TEST(HnswSemanticIndexTest, ExactIndexSaveAndLoadRoundTrip) {
    ExactSemanticIndex original;
    original.add(10, {1.0f, 0.0f, 0.0f});
    original.add(20, {0.0f, 1.0f, 0.0f});
    original.add(30, {0.0f, 0.0f, 1.0f});

    const auto temp_path = std::filesystem::temp_directory_path() / "exact_index_test.bin";
    EXPECT_TRUE(original.save(temp_path));
    EXPECT_TRUE(std::filesystem::exists(temp_path));

    ExactSemanticIndex restored;
    EXPECT_TRUE(restored.load(temp_path));
    EXPECT_EQ(restored.size(), 3);
    EXPECT_EQ(restored.dimensions(), 3);
    EXPECT_TRUE(restored.contains(10));
    EXPECT_TRUE(restored.contains(20));
    EXPECT_TRUE(restored.contains(30));

    auto res = restored.search(std::vector<float>{1.0f, 0.0f, 0.0f}, 1);
    ASSERT_EQ(res.size(), 1);
    EXPECT_EQ(res[0].element_id, 10);

    std::filesystem::remove(temp_path);
}

TEST(HnswSemanticIndexTest, HnswIndexSaveAndLoadRoundTrip) {
    DeterministicEmbeddingProvider provider;
    HnswConfig cfg{.m = 16, .ef_construction = 200, .ef_search = 64, .random_seed = 42};
    HnswSemanticIndex original(cfg);

    for (uint32_t i = 1; i <= 30; ++i) {
        auto vec = provider.embed("test_document_content_" + std::to_string(i * 13));
        original.add(i * 100, vec);
    }
    ASSERT_EQ(original.size(), 30);

    const auto temp_path = std::filesystem::temp_directory_path() / "hnsw_index_test.bin";
    EXPECT_TRUE(original.save(temp_path));
    EXPECT_TRUE(std::filesystem::exists(temp_path));
    EXPECT_TRUE(std::filesystem::exists(temp_path.string() + ".graph"));

    HnswSemanticIndex restored;
    EXPECT_TRUE(restored.load(temp_path));
    EXPECT_EQ(restored.size(), 30);
    EXPECT_EQ(restored.dimensions(), provider.dimensions());

    // Verify search parity before vs after load
    auto query_vec = provider.embed("test_document_content_130"); // Target element_id 1000
    auto orig_res = original.search(query_vec, 5);
    auto rest_res = restored.search(query_vec, 5);

    ASSERT_EQ(orig_res.size(), rest_res.size());
    for (size_t i = 0; i < orig_res.size(); ++i) {
        EXPECT_EQ(orig_res[i].element_id, rest_res[i].element_id);
        EXPECT_NEAR(orig_res[i].similarity_score, rest_res[i].similarity_score, 1e-4f);
    }

    std::filesystem::remove(temp_path);
    std::filesystem::remove(temp_path.string() + ".graph");
}

TEST(HnswSemanticIndexTest, LoadReconstructsGraphWhenGraphFileMissing) {
    DeterministicEmbeddingProvider provider;
    HnswSemanticIndex original;
    for (uint32_t i = 1; i <= 10; ++i) {
        auto vec = provider.embed("fallback_test_" + std::to_string(i));
        original.add(i, vec);
    }

    const auto temp_path = std::filesystem::temp_directory_path() / "hnsw_fallback_test.bin";
    EXPECT_TRUE(original.save(temp_path));
    EXPECT_TRUE(std::filesystem::remove(temp_path.string() + ".graph")); // Delete graph file

    HnswSemanticIndex restored;
    EXPECT_TRUE(restored.load(temp_path)); // Should fall back to in-memory graph reconstruction
    EXPECT_EQ(restored.size(), 10);

    auto query_vec = provider.embed("fallback_test_5");
    auto res = restored.search(query_vec, 1);
    ASSERT_EQ(res.size(), 1);
    EXPECT_EQ(res[0].element_id, 5);

    std::filesystem::remove(temp_path);
}

TEST(HnswSemanticIndexTest, LoadRejectsCorruptedFile) {
    const auto temp_path = std::filesystem::temp_directory_path() / "corrupted_index_test.bin";
    {
        std::ofstream os(temp_path, std::ios::binary);
        os << "CORRUPTED_NON_MAGIC_HEADER_DATA";
    }

    HnswSemanticIndex hnsw;
    EXPECT_FALSE(hnsw.load(temp_path));

    ExactSemanticIndex exact;
    EXPECT_FALSE(exact.load(temp_path));

    // Non-existent file
    EXPECT_FALSE(hnsw.load("non_existent_file_path.bin"));
    EXPECT_FALSE(exact.load("non_existent_file_path.bin"));

    std::filesystem::remove(temp_path);
}

}  // namespace
}  // namespace amoeba::semantic
