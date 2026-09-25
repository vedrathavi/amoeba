#include "amoeba/index/code_tokenizer.hpp"

#include <gtest/gtest.h>

namespace {

using namespace std;
using namespace amoeba::index;

TEST(CodeTokenizerTest, NormalizeTerm) {
    EXPECT_EQ(CodeTokenizer::normalize_term(""), "");
    EXPECT_EQ(CodeTokenizer::normalize_term("   "), "");
    EXPECT_EQ(CodeTokenizer::normalize_term("  UserAuth  "), "userauth");
    EXPECT_EQ(CodeTokenizer::normalize_term("GET_USER"), "get_user");
}

TEST(CodeTokenizerTest, CamelCaseSplitting) {
    const auto tokens = CodeTokenizer::tokenize_identifier("getUserById");
    ASSERT_EQ(tokens.size(), 5U);
    EXPECT_EQ(tokens[0], "getuserbyid");
    EXPECT_EQ(tokens[1], "get");
    EXPECT_EQ(tokens[2], "user");
    EXPECT_EQ(tokens[3], "by");
    EXPECT_EQ(tokens[4], "id");
}

TEST(CodeTokenizerTest, PascalCaseSplitting) {
    const auto tokens = CodeTokenizer::tokenize_identifier("UserAuthenticationService");
    ASSERT_EQ(tokens.size(), 4U);
    EXPECT_EQ(tokens[0], "userauthenticationservice");
    EXPECT_EQ(tokens[1], "user");
    EXPECT_EQ(tokens[2], "authentication");
    EXPECT_EQ(tokens[3], "service");
}

TEST(CodeTokenizerTest, SnakeCaseSplitting) {
    const auto tokens = CodeTokenizer::tokenize_identifier("user_service_handler");
    ASSERT_EQ(tokens.size(), 4U);
    EXPECT_EQ(tokens[0], "user_service_handler");
    EXPECT_EQ(tokens[1], "user");
    EXPECT_EQ(tokens[2], "service");
    EXPECT_EQ(tokens[3], "handler");
}

TEST(CodeTokenizerTest, ScreamingSnakeCaseSplitting) {
    const auto tokens = CodeTokenizer::tokenize_identifier("MAX_BUFFER_SIZE");
    ASSERT_EQ(tokens.size(), 4U);
    EXPECT_EQ(tokens[0], "max_buffer_size");
    EXPECT_EQ(tokens[1], "max");
    EXPECT_EQ(tokens[2], "buffer");
    EXPECT_EQ(tokens[3], "size");
}

TEST(CodeTokenizerTest, KebabCaseSplitting) {
    const auto tokens = CodeTokenizer::tokenize_identifier("btn-primary-active");
    ASSERT_EQ(tokens.size(), 4U);
    EXPECT_EQ(tokens[0], "btn-primary-active");
    EXPECT_EQ(tokens[1], "btn");
    EXPECT_EQ(tokens[2], "primary");
    EXPECT_EQ(tokens[3], "active");
}

TEST(CodeTokenizerTest, AcronymSplitting) {
    const auto tokens = CodeTokenizer::tokenize_identifier("XMLReaderFactory");
    ASSERT_GE(tokens.size(), 4U);
    EXPECT_EQ(tokens[0], "xmlreaderfactory");
    EXPECT_EQ(tokens[1], "xml");
    EXPECT_EQ(tokens[2], "reader");
    EXPECT_EQ(tokens[3], "factory");
}

TEST(CodeTokenizerTest, NumbersAndAlphanumericSplitting) {
    const auto tokens = CodeTokenizer::tokenize_identifier("User123Service");
    ASSERT_GE(tokens.size(), 4U);
    EXPECT_EQ(tokens[0], "user123service");
    EXPECT_EQ(tokens[1], "user");
    EXPECT_EQ(tokens[2], "123");
    EXPECT_EQ(tokens[3], "service");
}

TEST(CodeTokenizerTest, SymbolStripping) {
    const auto tokens1 = CodeTokenizer::tokenize_identifier("~Storage()");
    ASSERT_GE(tokens1.size(), 1U);
    EXPECT_EQ(tokens1[0], "storage()");

    const auto tokens2 = CodeTokenizer::tokenize_identifier("\"amoeba/engine.hpp\"");
    ASSERT_GE(tokens2.size(), 1U);
    EXPECT_EQ(tokens2[0], "amoeba/engine.hpp");
}

TEST(CodeTokenizerTest, TokenizeQuery) {
    const auto tokens = CodeTokenizer::tokenize_query("getUserById repository");
    ASSERT_GE(tokens.size(), 6U);
    EXPECT_EQ(tokens[0], "getuserbyid");
    EXPECT_EQ(tokens[1], "get");
    EXPECT_EQ(tokens[2], "user");
    EXPECT_EQ(tokens[3], "by");
    EXPECT_EQ(tokens[4], "id");
    EXPECT_EQ(tokens[5], "repository");
}

TEST(CodeTokenizerTest, TokenizePath) {
    const auto tokens = CodeTokenizer::tokenize_path("src/auth/user_service.cpp");
    ASSERT_GE(tokens.size(), 4U);
    EXPECT_EQ(tokens[0], "src/auth/user_service.cpp");
    EXPECT_EQ(tokens[1], "src");
    EXPECT_EQ(tokens[2], "auth");
    EXPECT_EQ(tokens[3], "user_service");
}

}  // namespace
