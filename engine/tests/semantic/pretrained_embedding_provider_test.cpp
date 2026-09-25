#include "amoeba/semantic/pretrained_embedding_provider.hpp"
#include "amoeba/semantic/semantic_document.hpp"
#include "amoeba/semantic/similarity.hpp"

#include <cmath>
#include <gtest/gtest.h>

namespace amoeba::semantic::test {

TEST(PretrainedEmbeddingProviderTest, DimensionAndMetadata) {
    PretrainedEmbeddingProvider provider;
    EXPECT_EQ(provider.dimensions(), 384u);
    EXPECT_EQ(provider.provider_name(), "all-MiniLM-L6-v2");
    EXPECT_TRUE(provider.is_model_loaded());
}

TEST(PretrainedEmbeddingProviderTest, NormalizationAndDeterminism) {
    PretrainedEmbeddingProvider provider;
    auto vec1 = provider.embed("validateCredentials authenticate login");
    auto vec2 = provider.embed("validateCredentials authenticate login");

    ASSERT_EQ(vec1.size(), 384u);
    ASSERT_EQ(vec2.size(), 384u);
    EXPECT_EQ(vec1, vec2);

    double norm = 0.0;
    for (float v : vec1) {
        norm += static_cast<double>(v) * static_cast<double>(v);
    }
    EXPECT_NEAR(std::sqrt(norm), 1.0, 1e-4);
}

TEST(PretrainedEmbeddingProviderTest, ConceptualSynonymSimilarity) {
    PretrainedEmbeddingProvider provider;

    // Code element text
    auto code_auth = provider.embed(
        "Language: C++\nFile: src/auth.cpp\nKind: Method\nContext: AuthService\nName: "
        "validateCredentials\nDetail: validates user login credentials");

    // Queries
    auto q_synonym = provider.embed("where do we check user login credentials?");
    auto q_identity = provider.embed("verify user identity and authentication");
    auto q_unrelated = provider.embed("render user profile ui component");

    float sim_synonym = cosine_similarity(code_auth, q_synonym);
    float sim_identity = cosine_similarity(code_auth, q_identity);
    float sim_unrelated = cosine_similarity(code_auth, q_unrelated);

    EXPECT_GT(sim_synonym, 0.60f);
    EXPECT_GT(sim_identity, 0.50f);
    EXPECT_LT(sim_unrelated, 0.40f);
    EXPECT_GT(sim_synonym, sim_unrelated);
}

TEST(PretrainedEmbeddingProviderTest, BatchEmbeddingEquivalence) {
    PretrainedEmbeddingProvider provider;
    std::vector<std::string> inputs = {
        "authenticateUser",
        "UserRepository::findUser",
        "PaymentProcessor::chargeInvoice",
    };

    auto batch = provider.embed_batch(inputs);
    ASSERT_EQ(batch.size(), 3u);

    for (std::size_t i = 0; i < inputs.size(); ++i) {
        EXPECT_EQ(batch[i], provider.embed(inputs[i]));
    }
}

TEST(PretrainedEmbeddingProviderTest, RepresentationModeComparison) {
    parser::CodeElement elem{
        .kind = parser::ElementKind::Method,
        .name = "authenticateUser",
        .location = {},
        .parent_context = "AuthService",
        .detail = "validates credentials and creates session",
    };

    std::string snippet = "bool authenticateUser(string u, string p) { return verify(u, p); }";

    std::string text_a = SemanticTextFormatter::format("C++", "src/auth.cpp", elem, snippet,
                                                       SemanticRepresentationMode::MetadataOnly);
    std::string text_b = SemanticTextFormatter::format(
        "C++", "src/auth.cpp", elem, snippet, SemanticRepresentationMode::MetadataWithSnippet);

    EXPECT_EQ(text_a.find("Code:"), std::string::npos);
    EXPECT_NE(text_b.find("Code: bool authenticateUser"), std::string::npos);

    PretrainedEmbeddingProvider provider;
    auto vec_a = provider.embed(text_a);
    auto vec_b = provider.embed(text_b);

    EXPECT_EQ(vec_a.size(), 384u);
    EXPECT_EQ(vec_b.size(), 384u);
    // Both are valid normalized vectors
    EXPECT_GT(cosine_similarity(vec_a, vec_b), 0.70f);
}

}  // namespace amoeba::semantic::test
