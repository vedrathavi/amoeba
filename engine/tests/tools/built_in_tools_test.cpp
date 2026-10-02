#include "amoeba/tools/find_symbol_tool.hpp"
#include "amoeba/tools/get_relationships_tool.hpp"
#include "amoeba/tools/read_file_tool.hpp"
#include "amoeba/tools/search_code_tool.hpp"
#include "amoeba/tools/tool_execution_context.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/graph/relationship_evidence_resolver.hpp"
#include "amoeba/source/source_snippet_reader.hpp"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace amoeba;
using namespace amoeba::tools;

class BuiltInToolsTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = std::filesystem::temp_directory_path() / "amoeba_tool_test";
        std::filesystem::create_directories(temp_dir / "src");

        // Write a dummy test source file
        std::ofstream out(temp_dir / "src" / "sample.cpp");
        for (int i = 1; i <= 200; ++i) {
            out << "int line_" << i << " = " << i << ";\n";
        }
        out.close();

        // Write a sensitive file to test security in temp_dir
        std::ofstream env_out(temp_dir / ".env");
        env_out << "SECRET_KEY=12345\n";
        env_out.close();

        // Setup context
        policy = std::make_unique<RepositoryAccessPolicy>();
        snippet_reader = std::make_unique<source::SourceSnippetReader>();

        context.repository_root = temp_dir;
        context.access_policy = policy.get();
        context.snippet_reader = snippet_reader.get();

        // Setup inverted index with sample symbols
        index = std::make_unique<index::InvertedIndex>();
        parser::ParsedFile pf;
        pf.file_path = temp_dir / "src" / "sample.cpp";
        pf.language = "cpp";

        parser::CodeElement elem1;
        elem1.name = "calculate_total";
        elem1.kind = parser::ElementKind::Function;
        elem1.location.start = parser::SourceLocation{.line = 10, .column = 1, .byte_offset = 0};
        elem1.location.end = parser::SourceLocation{.line = 20, .column = 1, .byte_offset = 0};
        pf.elements.push_back(elem1);

        parser::CodeElement elem2;
        elem2.name = "calculate_total";
        elem2.kind = parser::ElementKind::Method;
        elem2.location.start = parser::SourceLocation{.line = 50, .column = 1, .byte_offset = 0};
        elem2.location.end = parser::SourceLocation{.line = 60, .column = 1, .byte_offset = 0};
        pf.elements.push_back(elem2);

        parser::CodeElement elem3;
        elem3.name = "OrderManager";
        elem3.kind = parser::ElementKind::Class;
        elem3.location.start = parser::SourceLocation{.line = 1, .column = 1, .byte_offset = 0};
        elem3.location.end = parser::SourceLocation{.line = 100, .column = 1, .byte_offset = 0};
        pf.elements.push_back(elem3);

        parsed_files_storage.push_back(pf);
        index->add_parsed_file(pf);
        context.index = index.get();
        context.parsed_files = &parsed_files_storage;

        // Setup relationship graph
        graph = std::make_unique<graph::RelationshipGraph>();
        // Element IDs in InvertedIndex are 0-indexed (0: calculate_total, 1: calculate_total, 2: OrderManager)
        graph->add_relationship(2, 0, graph::RelationshipKind::Calls);
        resolver = std::make_unique<graph::RelationshipEvidenceResolver>(*graph, *index);

        context.relationship_graph = graph.get();
        context.relationship_resolver = resolver.get();
    }

    void TearDown() override {
        std::filesystem::remove_all(temp_dir);
    }

    std::filesystem::path temp_dir;
    std::unique_ptr<RepositoryAccessPolicy> policy;
    std::unique_ptr<source::SourceSnippetReader> snippet_reader;
    std::unique_ptr<index::InvertedIndex> index;
    std::vector<parser::ParsedFile> parsed_files_storage;
    std::unique_ptr<graph::RelationshipGraph> graph;
    std::unique_ptr<graph::RelationshipEvidenceResolver> resolver;
    ToolExecutionContext context;
};

// -----------------------------------------------------------------------------
// ReadFileTool Tests
// -----------------------------------------------------------------------------

TEST_F(BuiltInToolsTest, ReadFileValid) {
    ReadFileTool tool;
    ToolRequest req{
        .tool_name = "read_file",
        .arguments = {
            {"path", "src/sample.cpp"},
            {"start_line", "1"},
            {"end_line", "5"}
        }
    };

    auto res = tool.execute(req, context);
    EXPECT_EQ(res.status, ToolResultStatus::Success);
    EXPECT_NE(res.content.find("int line_1 = 1;"), std::string::npos);
    EXPECT_NE(res.content.find("int line_5 = 5;"), std::string::npos);
}

TEST_F(BuiltInToolsTest, ReadFileEnforcesLineLimits) {
    ReadFileTool tool;
    ToolRequest req{
        .tool_name = "read_file",
        .arguments = {
            {"path", "src/sample.cpp"},
            {"start_line", "1"},
            {"end_line", "180"} // > 150 lines
        }
    };

    auto res = tool.execute(req, context);
    EXPECT_EQ(res.status, ToolResultStatus::InvalidRequest);
    EXPECT_NE(res.summary.find("exceeds"), std::string::npos);
}

TEST_F(BuiltInToolsTest, ReadFileRejectsInvalidRange) {
    ReadFileTool tool;
    ToolRequest req{
        .tool_name = "read_file",
        .arguments = {
            {"path", "src/sample.cpp"},
            {"start_line", "10"},
            {"end_line", "5"}
        }
    };

    auto res = tool.execute(req, context);
    EXPECT_EQ(res.status, ToolResultStatus::InvalidRequest);
}

TEST_F(BuiltInToolsTest, ReadFileBlocksSensitiveFile) {
    ReadFileTool tool;
    ToolRequest req{
        .tool_name = "read_file",
        .arguments = {
            {"path", ".env"}
        }
    };

    auto res = tool.execute(req, context);
    EXPECT_EQ(res.status, ToolResultStatus::PermissionDenied);
}

TEST_F(BuiltInToolsTest, ReadFileBlocksPathTraversal) {
    ReadFileTool tool;
    ToolRequest req{
        .tool_name = "read_file",
        .arguments = {
            {"path", "../../etc/passwd"}
        }
    };

    auto res = tool.execute(req, context);
    EXPECT_EQ(res.status, ToolResultStatus::PermissionDenied);
}

// -----------------------------------------------------------------------------
// FindSymbolTool Tests
// -----------------------------------------------------------------------------

TEST_F(BuiltInToolsTest, FindSymbolExact) {
    FindSymbolTool tool;
    ToolRequest req{
        .tool_name = "find_symbol",
        .arguments = {{"name", "OrderManager"}}
    };

    auto res = tool.execute(req, context);
    EXPECT_EQ(res.status, ToolResultStatus::Success);
    EXPECT_NE(res.content.find("Class `OrderManager`"), std::string::npos);
}

TEST_F(BuiltInToolsTest, FindSymbolMultipleMatches) {
    FindSymbolTool tool;
    ToolRequest req{
        .tool_name = "find_symbol",
        .arguments = {{"name", "calculate_total"}}
    };

    auto res = tool.execute(req, context);
    EXPECT_EQ(res.status, ToolResultStatus::Success);
    EXPECT_NE(res.content.find("Function `calculate_total`"), std::string::npos);
    EXPECT_NE(res.content.find("Method `calculate_total`"), std::string::npos);
}

TEST_F(BuiltInToolsTest, FindSymbolKindFilter) {
    FindSymbolTool tool;
    ToolRequest req{
        .tool_name = "find_symbol",
        .arguments = {{"name", "calculate_total"}, {"kind", "method"}}
    };

    auto res = tool.execute(req, context);
    EXPECT_EQ(res.status, ToolResultStatus::Success);
    EXPECT_EQ(res.content.find("Function `calculate_total`"), std::string::npos);
    EXPECT_NE(res.content.find("Method `calculate_total`"), std::string::npos);
}

TEST_F(BuiltInToolsTest, FindSymbolNotFound) {
    FindSymbolTool tool;
    ToolRequest req{
        .tool_name = "find_symbol",
        .arguments = {{"name", "NonExistentSymbol"}}
    };

    auto res = tool.execute(req, context);
    EXPECT_EQ(res.status, ToolResultStatus::NotFound);
}

// -----------------------------------------------------------------------------
// GetRelationshipsTool Tests
// -----------------------------------------------------------------------------

TEST_F(BuiltInToolsTest, GetRelationshipsOutgoing) {
    GetRelationshipsTool tool;
    ToolRequest req{
        .tool_name = "get_relationships",
        .arguments = {{"symbol", "OrderManager"}}
    };

    auto res = tool.execute(req, context);
    EXPECT_EQ(res.status, ToolResultStatus::Success);
    EXPECT_NE(res.content.find("Calls"), std::string::npos);
    EXPECT_NE(res.content.find("calculate_total"), std::string::npos);
}

TEST_F(BuiltInToolsTest, GetRelationshipsNotFound) {
    GetRelationshipsTool tool;
    ToolRequest req{
        .tool_name = "get_relationships",
        .arguments = {{"symbol", "UnknownSymbol"}}
    };

    auto res = tool.execute(req, context);
    EXPECT_EQ(res.status, ToolResultStatus::NotFound);
}

// -----------------------------------------------------------------------------
// SearchCodeTool Tests
// -----------------------------------------------------------------------------

TEST_F(BuiltInToolsTest, SearchCodeEmptyQuery) {
    SearchCodeTool tool;
    ToolRequest req{
        .tool_name = "search_code",
        .arguments = {{"query", ""}}
    };

    auto res = tool.execute(req, context);
    EXPECT_EQ(res.status, ToolResultStatus::InvalidRequest);
}
