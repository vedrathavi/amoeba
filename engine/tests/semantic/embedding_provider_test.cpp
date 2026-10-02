#include "amoeba/semantic/deterministic_embedding_provider.hpp"
#include "amoeba/semantic/semantic_document.hpp"

#include <cmath>
#include <gtest/gtest.h>

namespace amoeba::semantic::test {

TEST(EmbeddingProviderTest, DimensionAndProviderName) {
    DeterministicEmbeddingProvider provider(128);
    EXPECT_EQ(provider.dimensions(), 128u);
    EXPECT_EQ(provider.provider_name(), "deterministic_test_double");
}

TEST(EmbeddingProviderTest, DeterministicOutputForSameInput) {
    DeterministicEmbeddingProvider provider(64);
    auto vec1 = provider.embed("validateCredentials authenticate");
    auto vec2 = provider.embed("validateCredentials authenticate");

    ASSERT_EQ(vec1.size(), 64u);
    ASSERT_EQ(vec2.size(), 64u);
    EXPECT_EQ(vec1, vec2);
}

TEST(EmbeddingProviderTest, EmptyInputYieldsZeroVector) {
    DeterministicEmbeddingProvider provider(64);
    auto vec = provider.embed("");
    ASSERT_EQ(vec.size(), 64u);
    for (float val : vec) {
        EXPECT_FLOAT_EQ(val, 0.0f);
    }
}

TEST(EmbeddingProviderTest, NormalizationToUnitLength) {
    DeterministicEmbeddingProvider provider(64);
    auto vec = provider.embed("class AuthService { void login(); }");

    double norm = 0.0;
    for (float v : vec) {
        norm += static_cast<double>(v) * static_cast<double>(v);
    }
    EXPECT_NEAR(std::sqrt(norm), 1.0, 1e-4);
}

TEST(EmbeddingProviderTest, BatchEmbeddingMatchesSingleEmbedding) {
    DeterministicEmbeddingProvider provider(32);
    std::vector<std::string> texts = {"token_one", "token_two", "token_three"};
    auto batch_results = provider.embed_batch(texts);

    ASSERT_EQ(batch_results.size(), 3u);
    for (std::size_t i = 0; i < texts.size(); ++i) {
        EXPECT_EQ(batch_results[i], provider.embed(texts[i]));
    }
}

TEST(EmbeddingProviderTest, SemanticTextFormatterStructuredOutput) {
    parser::CodeElement elem{
        .kind = parser::ElementKind::Method,
        .name = "authenticateUser",
        .location = {},
        .parent_context = "AuthService",
        .detail = "validates credentials",
    };

    std::string text = SemanticTextFormatter::format("C++", "src/auth/auth_service.cpp", elem);

    EXPECT_NE(text.find("Language: C++"), std::string::npos);
    EXPECT_NE(text.find("File: src/auth/auth_service.cpp"), std::string::npos);
    EXPECT_NE(text.find("Kind: Method"), std::string::npos);
    EXPECT_NE(text.find("Context: AuthService"), std::string::npos);
    EXPECT_NE(text.find("Name: authenticateUser"), std::string::npos);
    EXPECT_NE(text.find("Detail: validates credentials"), std::string::npos);
}

TEST(EmbeddingProviderTest, SemanticTextFormatterSignatureAndParameters) {
    parser::CodeElement elem{
        .kind = parser::ElementKind::Function,
        .name = "verifyJwtToken",
        .location = {},
        .parent_context = "auth",
        .detail = "",
        .signature = "verifyJwtToken(token: string, secret: string) -> boolean",
        .return_type = "boolean",
        .documentation = "Verifies HMAC-SHA256 signature on incoming Bearer JWT.",
    };

    std::string text = SemanticTextFormatter::format("TypeScript", "src/auth/jwt.ts", elem);

    EXPECT_NE(text.find("Language: TypeScript"), std::string::npos);
    EXPECT_NE(text.find("File: src/auth/jwt.ts"), std::string::npos);
    EXPECT_NE(text.find("Kind: Function"), std::string::npos);
    EXPECT_NE(text.find("Context: auth"), std::string::npos);
    EXPECT_NE(text.find("Name: verifyJwtToken"), std::string::npos);
    EXPECT_NE(text.find("Signature: verifyJwtToken(token: string, secret: string) -> boolean"), std::string::npos);
    EXPECT_NE(text.find("Parameters: (token: string, secret: string)"), std::string::npos);
    EXPECT_NE(text.find("ReturnType: boolean"), std::string::npos);
    EXPECT_NE(text.find("Documentation: Verifies HMAC-SHA256 signature on incoming Bearer JWT."), std::string::npos);
}

TEST(EmbeddingProviderTest, SemanticTextFormatterInheritanceAndUnitFormatting) {
    retrieval::RetrievalUnit unit{
        .primary_element_id = 42,
        .primary_element = parser::CodeElement{
            .kind = parser::ElementKind::Class,
            .name = "PostgresConnectionPool",
            .location = {},
            .parent_context = "db",
            .detail = "extends: BasePool; implements: IPool",
            .signature = "class PostgresConnectionPool",
            .return_type = "",
            .documentation = "Manages connection pooling and lifecycle for PostgreSQL backend.",
        },
        .file_path = "src/db/pool.cpp",
        .language = "C++",
        .role = retrieval::RetrievalUnitRole::Primary,
        .supporting_element_ids = {},
        .supporting_elements = {
            parser::CodeElement{
                .kind = parser::ElementKind::Call,
                .name = "acquireConnection",
                .location = {},
                .parent_context = "",
                .detail = "",
            },
            parser::CodeElement{
                .kind = parser::ElementKind::Call,
                .name = "releaseConnection",
                .location = {},
                .parent_context = "",
                .detail = "",
            },
        },
    };

    std::string unit_text = SemanticTextFormatter::format_unit(unit);

    EXPECT_NE(unit_text.find("Language: C++"), std::string::npos);
    EXPECT_NE(unit_text.find("File: src/db/pool.cpp"), std::string::npos);
    EXPECT_NE(unit_text.find("Kind: Class"), std::string::npos);
    EXPECT_NE(unit_text.find("Name: PostgresConnectionPool"), std::string::npos);
    EXPECT_NE(unit_text.find("Signature: class PostgresConnectionPool"), std::string::npos);
    EXPECT_NE(unit_text.find("Inheritance: extends: BasePool; implements: IPool"), std::string::npos);
    EXPECT_NE(unit_text.find("Documentation: Manages connection pooling and lifecycle for PostgreSQL backend."), std::string::npos);
    EXPECT_NE(unit_text.find("Calls: acquireConnection releaseConnection"), std::string::npos);
}

TEST(EmbeddingProviderTest, SemanticTextFormatterDeterministicOutput) {
    parser::CodeElement elem{
        .kind = parser::ElementKind::Function,
        .name = "executeMacro",
        .location = {},
        .parent_context = "macros",
        .detail = "",
        .signature = "executeMacro(name: string, args: list) -> table",
        .return_type = "table",
        .documentation = "Executes custom DuckLake SQL macro expansion.",
    };

    std::string run1 = SemanticTextFormatter::format("Python", "ducklake/macros.py", elem);
    std::string run2 = SemanticTextFormatter::format("Python", "ducklake/macros.py", elem);
    EXPECT_EQ(run1, run2);
    EXPECT_FALSE(run1.empty());
}

TEST(EmbeddingProviderTest, SemanticTextFormatterHandlesMissingMetadataSafely) {
    parser::CodeElement empty_elem{};
    std::string empty_text = SemanticTextFormatter::format("", "", empty_elem);
    EXPECT_TRUE(empty_text.empty() || empty_text.find("Kind: Unknown") == std::string::npos);

    retrieval::RetrievalUnit empty_unit{
        .primary_element_id = 1,
        .primary_element = parser::CodeElement{},
        .file_path = "",
        .language = "",
    };
    std::string empty_unit_text = SemanticTextFormatter::format_unit(empty_unit);
    EXPECT_TRUE(empty_unit_text.find("Signature:") == std::string::npos);
    EXPECT_TRUE(empty_unit_text.find("Documentation:") == std::string::npos);
    EXPECT_TRUE(empty_unit_text.find("Calls:") == std::string::npos);
}

}  // namespace amoeba::semantic::test
