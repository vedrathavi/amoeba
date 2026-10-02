#include "amoeba/tools/tool_registry.hpp"
#include "amoeba/scanner/repository_scanner.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/graph/relationship_evidence_resolver.hpp"
#include "amoeba/source/source_snippet_reader.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/semantic/deterministic_embedding_provider.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <iostream>
#include <vector>

using namespace amoeba;
using namespace amoeba::tools;

class RealRepoToolsTest : public ::testing::Test {
protected:
    void SetUp() override {
        calendar_path = std::filesystem::current_path() / "demo_test_projects" / "calendar";
        if (!std::filesystem::exists(calendar_path)) {
            return;
        }

        scanner::RepositoryScanner scanner;
        auto scan_res = scanner.scan(calendar_path);

        parser::SourceParser parser;
        for (const auto& file_meta : scan_res.files) {
            auto pf = parser.parse_file(file_meta.path);
            if (pf.success) {
                index.add_parsed_file(pf);
                parsed_files.push_back(std::move(pf));
            }
        }

        resolver = std::make_unique<graph::RelationshipEvidenceResolver>(graph, index);
        pipeline = std::make_unique<retrieval::PrimaryRetrievalPipeline>(parsed_files, index, embedding_provider);
        has_calendar = true;
    }

    std::filesystem::path calendar_path;
    std::vector<parser::ParsedFile> parsed_files;
    index::InvertedIndex index;
    graph::RelationshipGraph graph;
    std::unique_ptr<graph::RelationshipEvidenceResolver> resolver;
    semantic::DeterministicEmbeddingProvider embedding_provider{32};
    std::unique_ptr<retrieval::PrimaryRetrievalPipeline> pipeline;
    source::SourceSnippetReader snippet_reader;
    RepositoryAccessPolicy access_policy;
    bool has_calendar{false};
};

TEST_F(RealRepoToolsTest, CalendarRepositoryExplorationBenchmark) {
    if (!has_calendar) {
        GTEST_SKIP() << "Calendar demo project not found at " << calendar_path;
    }

    auto registry = ToolRegistry::create_default_registry();

    ToolExecutionContext ctx;
    ctx.repository_root = calendar_path;
    ctx.retrieval_pipeline = pipeline.get();
    ctx.snippet_reader = &snippet_reader;
    ctx.index = &index;
    ctx.parsed_files = &parsed_files;
    ctx.relationship_graph = &graph;
    ctx.relationship_resolver = resolver.get();
    ctx.access_policy = &access_policy;

    // 1. Benchmark search_code
    {
        ToolRequest req{
            .tool_name = "search_code",
            .arguments = {{"query", "calendar navigation"}, {"mode", "hybrid"}, {"limit", "5"}}
        };
        auto t0 = std::chrono::high_resolution_clock::now();
        auto res = registry.execute(req, ctx);
        auto t1 = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        EXPECT_EQ(res.status, ToolResultStatus::Success);
        EXPECT_FALSE(res.content.empty());
        std::cout << "[Benchmark] search_code('calendar navigation'): " << duration_us << " us ("
                  << (duration_us / 1000.0) << " ms)\n";
    }

    // 2. Benchmark find_symbol
    {
        ToolRequest req{
            .tool_name = "find_symbol",
            .arguments = {{"name", "useCalendar"}}
        };
        auto t0 = std::chrono::high_resolution_clock::now();
        auto res = registry.execute(req, ctx);
        auto t1 = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        EXPECT_EQ(res.status, ToolResultStatus::Success);
        EXPECT_FALSE(res.content.empty());
        std::cout << "[Benchmark] find_symbol('useCalendar'): " << duration_us << " us ("
                  << (duration_us / 1000.0) << " ms)\n";
    }

    // 3. Benchmark read_file
    {
        ToolRequest req{
            .tool_name = "read_file",
            .arguments = {{"path", "src/hooks/useCalendar.ts"}, {"start_line", "1"}, {"end_line", "30"}}
        };
        auto t0 = std::chrono::high_resolution_clock::now();
        auto res = registry.execute(req, ctx);
        auto t1 = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        EXPECT_EQ(res.status, ToolResultStatus::Success);
        EXPECT_FALSE(res.content.empty());
        std::cout << "[Benchmark] read_file('src/hooks/useCalendar.ts', L1-30): " << duration_us << " us ("
                  << (duration_us / 1000.0) << " ms)\n";
    }

    // 4. Benchmark get_relationships
    {
        ToolRequest req{
            .tool_name = "get_relationships",
            .arguments = {{"symbol", "useCalendar"}}
        };
        auto t0 = std::chrono::high_resolution_clock::now();
        auto res = registry.execute(req, ctx);
        auto t1 = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        EXPECT_TRUE(res.status == ToolResultStatus::Success || res.status == ToolResultStatus::NotFound);
        std::cout << "[Benchmark] get_relationships('useCalendar'): " << duration_us << " us ("
                  << (duration_us / 1000.0) << " ms)\n";
    }
}
