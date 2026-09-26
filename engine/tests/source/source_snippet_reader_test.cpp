// Phase 7.1 — SourceSnippetReader Tests
//
// Tests cover:
//   A. C++ class/function extraction
//   B. TypeScript function/method extraction
//   C. Python function/class extraction
//   D. CSS selector/property extraction
//   E. Exact single-line extraction
//   F. Multi-line extraction
//   G. Context-line extraction (leading and trailing)
//   H. First-line range
//   I. Last-line range
//   J. Missing file handling (throwing and non-throwing)
//   K. Invalid range handling (start > end, out of bounds)
//   L. Empty file handling
//   M. File without trailing newline
//   N. UTF-8 multibyte characters before requested range
//   O. Deterministic repeated extraction
//   P. Extraction from disk via SourceParser round-trip

#include "amoeba/parser/source_parser.hpp"
#include "amoeba/source/source_snippet_reader.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace amoeba::source {

namespace {

class SourceSnippetReaderTest : public ::testing::Test {
protected:
    SourceSnippetReader reader_;
    parser::SourceParser parser_;
    std::filesystem::path temp_dir_;

    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / "amoeba_source_test";
        std::filesystem::create_directories(temp_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
    }

    std::filesystem::path create_temp_file(std::string_view filename, std::string_view content) {
        const auto path = temp_dir_ / filename;
        std::ofstream out(path, std::ios::binary);
        out << content;
        return path;
    }
};

// A. C++ class and function extraction
TEST_F(SourceSnippetReaderTest, CppClassAndFunctionExtraction) {
    const std::string source = "#include <iostream>\n\n"
                               "class Calculator {\n"
                               "public:\n"
                               "    int add(int a, int b) {\n"
                               "        return a + b;\n"
                               "    }\n"
                               "};\n\n"
                               "int main() {\n"
                               "    return 0;\n"
                               "}\n";

    const auto parsed = parser_.parse_source(source, "C++", "calc.cpp");
    ASSERT_TRUE(parsed.success);

    // Find Calculator class
    const auto classes = parsed.get_elements_by_kind(parser::ElementKind::Class);
    ASSERT_EQ(classes.size(), 1u);
    const auto excerpt_class = reader_.read_range_from_text(source, classes[0].location);
    EXPECT_TRUE(excerpt_class.text.starts_with("class Calculator"));
    EXPECT_TRUE(excerpt_class.text.ends_with("}"));

    // Find add method
    const auto methods = parsed.get_elements_by_kind(parser::ElementKind::Method);
    ASSERT_EQ(methods.size(), 1u);
    const auto excerpt_method = reader_.read_range_from_text(source, methods[0].location);
    EXPECT_TRUE(excerpt_method.text.starts_with("int add(int a, int b) {"));
    EXPECT_TRUE(excerpt_method.text.ends_with("}"));
}

// B. TypeScript function and method extraction
TEST_F(SourceSnippetReaderTest, TypeScriptFunctionExtraction) {
    const std::string source = "export function useCalendar(initialDate: Date) {\n"
                               "    const [date, setDate] = useState(initialDate);\n"
                               "    return { date, setDate };\n"
                               "}\n";

    const auto parsed = parser_.parse_source(source, "TypeScript", "useCalendar.ts");
    ASSERT_TRUE(parsed.success);

    const auto functions = parsed.get_elements_by_kind(parser::ElementKind::Function);
    ASSERT_GE(functions.size(), 1u);

    const auto excerpt = reader_.read_range_from_text(source, functions[0].location);
    EXPECT_TRUE(excerpt.text.starts_with("function useCalendar"));
    EXPECT_TRUE(excerpt.text.ends_with("}"));
    EXPECT_EQ(excerpt.start_line, 1u);
    EXPECT_EQ(excerpt.end_line, 4u);
}

// C. Python function and class extraction
TEST_F(SourceSnippetReaderTest, PythonFunctionAndClassExtraction) {
    const std::string source = "class RepositoryScanner:\n"
                               "    def __init__(self, root: str):\n"
                               "        self.root = root\n\n"
                               "    def scan(self):\n"
                               "        return []\n";

    const auto parsed = parser_.parse_source(source, "Python", "scanner.py");
    ASSERT_TRUE(parsed.success);

    const auto classes = parsed.get_elements_by_kind(parser::ElementKind::Class);
    ASSERT_EQ(classes.size(), 1u);

    const auto excerpt = reader_.read_range_from_text(source, classes[0].location);
    EXPECT_TRUE(excerpt.text.starts_with("class RepositoryScanner:"));
    EXPECT_TRUE(excerpt.text.ends_with("return []"));
}

// D. CSS selector extraction
TEST_F(SourceSnippetReaderTest, CSSSelectorExtraction) {
    const std::string source = ".btn-primary {\n"
                               "    background-color: #007bff;\n"
                               "    color: #ffffff;\n"
                               "}\n";

    const auto parsed = parser_.parse_source(source, "CSS", "styles.css");
    ASSERT_TRUE(parsed.success);

    const auto selectors = parsed.get_elements_by_kind(parser::ElementKind::Selector);
    ASSERT_GE(selectors.size(), 1u);

    const auto excerpt = reader_.read_range_from_text(source, selectors[0].location);
    EXPECT_TRUE(excerpt.text.starts_with(".btn-primary"));
    EXPECT_TRUE(excerpt.text.ends_with("}"));
}

// E. Exact single-line extraction
TEST_F(SourceSnippetReaderTest, ExactSingleLineExtraction) {
    const std::string source = "const API_KEY = \"secret_123\";\n";
    const parser::SourceRange range{
        .start = {.line = 1, .column = 1, .byte_offset = 0},
        .end = {.line = 1, .column = 30, .byte_offset = 29},
    };

    const auto excerpt = reader_.read_range_from_text(source, range);
    EXPECT_EQ(excerpt.text, "const API_KEY = \"secret_123\";");
    EXPECT_EQ(excerpt.start_line, 1u);
    EXPECT_EQ(excerpt.end_line, 1u);
    EXPECT_FALSE(excerpt.has_context_lines);
}

// F. Multi-line extraction
TEST_F(SourceSnippetReaderTest, MultiLineExtractionPreservesWhitespace) {
    const std::string source = "line 1\n"
                               "    line 2 (indented)\n"
                               "\tline 3 (tabbed)\n"
                               "line 4\n";

    const parser::SourceRange range{
        .start = {.line = 2, .column = 1, .byte_offset = 7},
        .end = {.line = 3, .column = 18, .byte_offset = 46},
    };

    const auto excerpt = reader_.read_range_from_text(source, range);
    EXPECT_EQ(excerpt.text, "    line 2 (indented)\n\tline 3 (tabbed)\n");
}

// G. Context-line extraction
TEST_F(SourceSnippetReaderTest, ContextLineExtractionBeforeAndAfter) {
    const std::string source = "line 1\n"
                               "line 2\n"
                               "line 3 (target)\n"
                               "line 4 (target)\n"
                               "line 5\n"
                               "line 6\n";

    const parser::SourceRange range{
        .start = {.line = 3, .column = 1, .byte_offset = 14},
        .end = {.line = 4, .column = 16, .byte_offset = 45},
    };

    const auto excerpt = reader_.read_range_from_text(source, range, /*context_lines=*/1);
    EXPECT_TRUE(excerpt.has_context_lines);
    EXPECT_EQ(excerpt.start_line, 2u);
    EXPECT_EQ(excerpt.end_line, 5u);
    EXPECT_EQ(excerpt.context_lines_before, 1u);
    EXPECT_EQ(excerpt.context_lines_after, 1u);
    EXPECT_EQ(excerpt.text, "line 2\n"
                            "line 3 (target)\n"
                            "line 4 (target)\n"
                            "line 5\n");
}

// H. First-line range with context clamps cleanly
TEST_F(SourceSnippetReaderTest, FirstLineRangeContextClampsToBeginning) {
    const std::string source = "line 1 (target)\n"
                               "line 2\n"
                               "line 3\n";

    const parser::SourceRange range{
        .start = {.line = 1, .column = 1, .byte_offset = 0},
        .end = {.line = 1, .column = 16, .byte_offset = 15},
    };

    const auto excerpt = reader_.read_range_from_text(source, range, /*context_lines=*/2);
    EXPECT_EQ(excerpt.start_line, 1u);
    EXPECT_EQ(excerpt.end_line, 3u);
    EXPECT_EQ(excerpt.context_lines_before, 0u);
    EXPECT_EQ(excerpt.context_lines_after, 2u);
    EXPECT_EQ(excerpt.text, source);
}

// I. Last-line range with context clamps cleanly
TEST_F(SourceSnippetReaderTest, LastLineRangeContextClampsToEnd) {
    const std::string source = "line 1\n"
                               "line 2\n"
                               "line 3 (target)\n";

    const parser::SourceRange range{
        .start = {.line = 3, .column = 1, .byte_offset = 14},
        .end = {.line = 3, .column = 16, .byte_offset = 29},
    };

    const auto excerpt = reader_.read_range_from_text(source, range, /*context_lines=*/2);
    EXPECT_EQ(excerpt.start_line, 1u);
    EXPECT_EQ(excerpt.end_line, 3u);
    EXPECT_EQ(excerpt.context_lines_before, 2u);
    EXPECT_EQ(excerpt.context_lines_after, 0u);
    EXPECT_EQ(excerpt.text, source);
}

// J. Missing file handling
TEST_F(SourceSnippetReaderTest, MissingFileThrowsAndTryReturnsNullopt) {
    const std::filesystem::path bad_path = temp_dir_ / "does_not_exist.cpp";
    const parser::SourceRange range{
        .start = {.line = 1, .column = 1, .byte_offset = 0},
        .end = {.line = 1, .column = 10, .byte_offset = 10},
    };

    EXPECT_THROW((void)reader_.read_range(bad_path, range), std::invalid_argument);
    EXPECT_FALSE(reader_.try_read_range(bad_path, range).has_value());
}

// K. Invalid range handling
TEST_F(SourceSnippetReaderTest, InvertedRangeThrowsAndTryReturnsNullopt) {
    const std::string source = "hello world\n";
    const parser::SourceRange inverted{
        .start = {.line = 1, .column = 10, .byte_offset = 10},
        .end = {.line = 1, .column = 2, .byte_offset = 2},
    };

    EXPECT_THROW((void)reader_.read_range_from_text(source, inverted), std::invalid_argument);
    EXPECT_FALSE(reader_.try_read_range_from_text(source, inverted).has_value());
}

TEST_F(SourceSnippetReaderTest, OutOfBoundsRangeThrowsAndTryReturnsNullopt) {
    const std::string source = "hello world\n";
    const parser::SourceRange out_of_bounds{
        .start = {.line = 1, .column = 1, .byte_offset = 0},
        .end = {.line = 1, .column = 50, .byte_offset = 500},
    };

    EXPECT_THROW((void)reader_.read_range_from_text(source, out_of_bounds), std::out_of_range);
    EXPECT_FALSE(reader_.try_read_range_from_text(source, out_of_bounds).has_value());
}

// L. Empty file handling
TEST_F(SourceSnippetReaderTest, EmptySourceReturnsEmptyExcerptSafely) {
    const std::string source = "";
    const parser::SourceRange empty_range{
        .start = {.line = 1, .column = 1, .byte_offset = 0},
        .end = {.line = 1, .column = 1, .byte_offset = 0},
    };

    const auto excerpt = reader_.read_range_from_text(source, empty_range);
    EXPECT_EQ(excerpt.text, "");
    EXPECT_EQ(excerpt.start_line, 1u);
    EXPECT_EQ(excerpt.end_line, 1u);
}

// M. File without trailing newline
TEST_F(SourceSnippetReaderTest, FileWithoutTrailingNewline) {
    const std::string source = "line 1\nline 2 without newline";
    const parser::SourceRange range{
        .start = {.line = 2, .column = 1, .byte_offset = 7},
        .end = {.line = 2, .column = 29, .byte_offset = 29},
    };

    const auto excerpt = reader_.read_range_from_text(source, range);
    EXPECT_EQ(excerpt.text, "line 2 without newline");
    EXPECT_EQ(excerpt.start_line, 2u);
    EXPECT_EQ(excerpt.end_line, 2u);
}

// N. UTF-8 multibyte text before requested range
TEST_F(SourceSnippetReaderTest, MultibyteUtf8CharactersBeforeTargetRange) {
    // Contains Hindi, Japanese, and Greek comments with multibyte UTF-8 sequences
    const std::string source = "// टिप्पणी (Hindi - 21 bytes for 7 chars)\n"
                               "// 日本語コメント (Japanese - 21 bytes for 7 chars)\n"
                               "// Ελληνικά (Greek - 16 bytes for 8 chars)\n"
                               "int computeSquare(int n) {\n"
                               "    return n * n;\n"
                               "}\n";

    const auto parsed = parser_.parse_source(source, "C++", "utf8_test.cpp");
    ASSERT_TRUE(parsed.success);

    const auto functions = parsed.get_elements_by_kind(parser::ElementKind::Function);
    ASSERT_EQ(functions.size(), 1u);

    const auto excerpt = reader_.read_range_from_text(source, functions[0].location);
    EXPECT_EQ(excerpt.text, "int computeSquare(int n) {\n    return n * n;\n}");
    EXPECT_EQ(excerpt.start_line, 4u);
    EXPECT_EQ(excerpt.end_line, 6u);
}

// O. Deterministic repeated extraction
TEST_F(SourceSnippetReaderTest, DeterministicRepeatedExtraction) {
    const std::string source = "template <typename T>\n"
                               "T identity(T val) {\n"
                               "    return val;\n"
                               "}\n";

    const auto parsed = parser_.parse_source(source, "C++", "id.hpp");
    ASSERT_TRUE(parsed.success);
    const auto funcs = parsed.get_elements_by_kind(parser::ElementKind::Function);
    ASSERT_EQ(funcs.size(), 1u);

    const auto excerpt1 = reader_.read_range_from_text(source, funcs[0].location);
    const auto excerpt2 = reader_.read_range_from_text(source, funcs[0].location);

    EXPECT_EQ(excerpt1, excerpt2);
    EXPECT_EQ(excerpt1.text, excerpt2.text);
}

// P. File on disk round-trip extraction
TEST_F(SourceSnippetReaderTest, DiskFileExtractionRoundTrip) {
    const std::string content = "class TokenManager {\n"
                                "public:\n"
                                "    bool isValid() const { return true; }\n"
                                "};\n";

    const auto file_path = create_temp_file("token_manager.cpp", content);
    const auto parsed = parser_.parse_file(file_path);
    ASSERT_TRUE(parsed.success);

    const auto classes = parsed.get_elements_by_kind(parser::ElementKind::Class);
    ASSERT_EQ(classes.size(), 1u);

    const auto excerpt = reader_.read_range(file_path, classes[0].location);
    EXPECT_TRUE(excerpt.text.starts_with("class TokenManager"));
    EXPECT_TRUE(excerpt.text.ends_with("}"));
    EXPECT_EQ(excerpt.file_path, file_path);
}

// Micro-benchmark observations
TEST_F(SourceSnippetReaderTest, ExtractionBenchmarkObservation) {
    const std::string content = "void fn1() { int a = 1; }\n"
                                "void fn2() { int b = 2; }\n"
                                "void fn3() { int c = 3; }\n"
                                "void fn4() { int d = 4; }\n"
                                "void fn5() { int e = 5; }\n"
                                "void fn6() { int f = 6; }\n"
                                "void fn7() { int g = 7; }\n"
                                "void fn8() { int h = 8; }\n"
                                "void fn9() { int i = 9; }\n"
                                "void fn10() { int j = 10; }\n";

    const auto file_path = create_temp_file("bench.cpp", content);
    const auto parsed = parser_.parse_file(file_path);
    ASSERT_TRUE(parsed.success);

    const auto funcs = parsed.get_elements_by_kind(parser::ElementKind::Function);
    ASSERT_EQ(funcs.size(), 10u);

    // Measure 1 snippet
    const auto t0 = std::chrono::high_resolution_clock::now();
    const auto ex1 = reader_.read_range(file_path, funcs[0].location);
    const auto t1 = std::chrono::high_resolution_clock::now();
    const auto lat_1_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    // Measure 5 snippets
    const auto t2 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < 5; ++i) {
        (void)reader_.read_range(file_path, funcs[i].location);
    }
    const auto t3 = std::chrono::high_resolution_clock::now();
    const auto lat_5_us = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();

    // Measure 10 snippets
    const auto t4 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < 10; ++i) {
        (void)reader_.read_range(file_path, funcs[i].location);
    }
    const auto t5 = std::chrono::high_resolution_clock::now();
    const auto lat_10_us = std::chrono::duration_cast<std::chrono::microseconds>(t5 - t4).count();

    EXPECT_FALSE(ex1.text.empty());
    std::cout << "[Benchmark Observation] 1 snippet: " << lat_1_us
              << " us, 5 snippets: " << lat_5_us << " us, 10 snippets: " << lat_10_us << " us\n";
}

}  // namespace
}  // namespace amoeba::source
