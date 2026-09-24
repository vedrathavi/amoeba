#include "amoeba/parser/source_parser.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace {

using namespace std;
using namespace std::filesystem;
using namespace amoeba::parser;

class SourceParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto timestamp = chrono::steady_clock::now().time_since_epoch().count();
        test_dir = temp_directory_path() / ("amoeba_parser_test_" + to_string(timestamp));
        create_directories(test_dir);
    }

    void TearDown() override {
        error_code ec;
        remove_all(test_dir, ec);
    }

    path create_test_file(const string& filename, string_view content) {
        const path file_path = test_dir / filename;
        ofstream file(file_path, ios::binary);
        file << content;
        return file_path;
    }

    path test_dir;
    SourceParser parser;
};

TEST_F(SourceParserTest, BasicFunctionParsing) {
    const string source = R"(
int add(int a, int b) {
    return a + b;
}
)";

    const auto parsed = parser.parse_source(source, "C++");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);

    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 1U);
    EXPECT_EQ(functions[0].name, "add");
    EXPECT_EQ(functions[0].location.start.line, 2U);
}

TEST_F(SourceParserTest, ClassAndMethodParsing) {
    const string source = R"(
class User {
public:
    void login();
    void logout() {
        cleanup();
    }
};
)";

    const auto parsed = parser.parse_source(source, "C++");

    EXPECT_TRUE(parsed.success);
    const auto classes = parsed.get_elements_by_kind(ElementKind::Class);
    ASSERT_EQ(classes.size(), 1U);
    EXPECT_EQ(classes[0].name, "User");
    EXPECT_EQ(classes[0].location.start.line, 2U);

    const auto methods = parsed.get_elements_by_kind(ElementKind::Method);
    ASSERT_GE(methods.size(), 2U);
    EXPECT_EQ(methods[0].name, "login");
    EXPECT_EQ(methods[0].parent_context, "User");
    EXPECT_EQ(methods[1].name, "logout");
    EXPECT_EQ(methods[1].parent_context, "User");

    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_EQ(calls.size(), 1U);
    EXPECT_EQ(calls[0].name, "cleanup");
}

TEST_F(SourceParserTest, IncludeExtraction) {
    const string source = R"(
#include "database.h"
#include <iostream>

int main() {
    return 0;
}
)";

    const auto parsed = parser.parse_source(source, "C++");

    EXPECT_TRUE(parsed.success);
    const auto includes = parsed.get_elements_by_kind(ElementKind::Include);
    ASSERT_EQ(includes.size(), 2U);
    EXPECT_EQ(includes[0].name, "\"database.h\"");
    EXPECT_EQ(includes[0].location.start.line, 2U);
    EXPECT_EQ(includes[1].name, "<iostream>");
    EXPECT_EQ(includes[1].location.start.line, 3U);
}

TEST_F(SourceParserTest, FunctionCallExtraction) {
    const string source = R"(
void process() {
    authenticate(user);
    logger.log("done");
}
)";

    const auto parsed = parser.parse_source(source, "C++");

    EXPECT_TRUE(parsed.success);
    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_GE(calls.size(), 2U);
    EXPECT_EQ(calls[0].name, "authenticate");
    EXPECT_EQ(calls[0].location.start.line, 3U);
    EXPECT_EQ(calls[1].name, "log");
}

TEST_F(SourceParserTest, NestedStructureTraversal) {
    const string source = R"(
void processOrders(bool check) {
    if (check) {
        for (int i = 0; i < 10; ++i) {
            executeOrder(i);
        }
    }
}
)";

    const auto parsed = parser.parse_source(source, "C++");

    EXPECT_TRUE(parsed.success);
    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 1U);
    EXPECT_EQ(functions[0].name, "processOrders");

    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_EQ(calls.size(), 1U);
    EXPECT_EQ(calls[0].name, "executeOrder");
}

TEST_F(SourceParserTest, StructParsing) {
    const string source = R"(
struct Point {
    int x;
    int y;
};
)";

    const auto parsed = parser.parse_source(source, "C++");

    EXPECT_TRUE(parsed.success);
    const auto structs = parsed.get_elements_by_kind(ElementKind::Struct);
    ASSERT_EQ(structs.size(), 1U);
    EXPECT_EQ(structs[0].name, "Point");
    EXPECT_EQ(structs[0].location.start.line, 2U);
}

TEST_F(SourceParserTest, HandlesIncompleteOrInvalidSourceGracefully) {
    const string incomplete_source = R"(
class User {
public:
    void login(
};
)";

    const auto parsed = parser.parse_source(incomplete_source, "C++");

    EXPECT_TRUE(parsed.success);
    EXPECT_TRUE(parsed.has_syntax_errors);

    const auto classes = parsed.get_elements_by_kind(ElementKind::Class);
    ASSERT_EQ(classes.size(), 1U);
    EXPECT_EQ(classes[0].name, "User");
}

TEST_F(SourceParserTest, ParseFileFromDisk) {
    const string source = R"(
#include "header.h"
int run() {
    return 42;
}
)";
    const path file_path = create_test_file("sample.cpp", source);

    const auto parsed = parser.parse_file(file_path);

    EXPECT_TRUE(parsed.success);
    EXPECT_EQ(parsed.language, "C++");
    EXPECT_EQ(parsed.file_path, file_path);

    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 1U);
    EXPECT_EQ(functions[0].name, "run");
}

TEST_F(SourceParserTest, ParseFileThrowsOnInvalidPaths) {
    const path non_existent = test_dir / "missing.cpp";
    EXPECT_THROW((void)parser.parse_file(non_existent), invalid_argument);
    EXPECT_THROW((void)parser.parse_file(test_dir), invalid_argument);
}

TEST_F(SourceParserTest, CLanguageParsing) {
    const string source = R"(
#include <stdio.h>

struct Buffer {
    char* data;
    int size;
};

int compute_sum(int a, int b) {
    return a + b;
}

int main(void) {
    printf("hello\n");
    return compute_sum(1, 2);
}
)";

    const auto parsed = parser.parse_source(source, "C");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);
    EXPECT_EQ(parsed.language, "C");

    const auto structs = parsed.get_elements_by_kind(ElementKind::Struct);
    ASSERT_EQ(structs.size(), 1U);
    EXPECT_EQ(structs[0].name, "Buffer");

    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 2U);
    EXPECT_EQ(functions[0].name, "compute_sum");
    EXPECT_EQ(functions[1].name, "main");

    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_GE(calls.size(), 2U);
    EXPECT_EQ(calls[0].name, "printf");
    EXPECT_EQ(calls[1].name, "compute_sum");
}

TEST_F(SourceParserTest, UnsupportedLanguageRejection) {
    const string source = "def hello(): pass";
    const auto parsed = parser.parse_source(source, "Python");

    EXPECT_FALSE(parsed.success);
    EXPECT_TRUE(parsed.elements.empty());
    EXPECT_EQ(parsed.language, "Python");
}

TEST_F(SourceParserTest, DetectsLanguageFromExtension) {
    EXPECT_EQ(SourceParser::detect_language("src/main.c"), "C");
    EXPECT_EQ(SourceParser::detect_language("include/utils.h"), "C");
    EXPECT_EQ(SourceParser::detect_language("src/main.cpp"), "C++");
    EXPECT_EQ(SourceParser::detect_language("include/header.hpp"), "C++");
    EXPECT_EQ(SourceParser::detect_language("src/file.cc"), "C++");
    EXPECT_EQ(SourceParser::detect_language("src/file.cxx"), "C++");
    EXPECT_EQ(SourceParser::detect_language("include/file.hh"), "C++");
    EXPECT_EQ(SourceParser::detect_language("include/file.hxx"), "C++");
    EXPECT_EQ(SourceParser::detect_language("script.py"), "Unknown");
    EXPECT_EQ(SourceParser::detect_language("index.js"), "Unknown");
    EXPECT_EQ(SourceParser::detect_language("README.md"), "Unknown");
}

}  // namespace
