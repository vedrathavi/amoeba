#include "amoeba/graph/relationship_expander.hpp"

#include "amoeba/evidence/evidence_assembler.hpp"
#include "amoeba/graph/relationship_evidence_resolver.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/source/source_snippet_reader.hpp"

#include <gtest/gtest.h>

namespace amoeba::graph {
namespace {

class RelationshipExpanderTest : public ::testing::Test {
protected:
    index::InvertedIndex index;
    RelationshipGraph graph;
    source::SourceSnippetReader snippet_reader;
    std::unique_ptr<RelationshipExpander> expander;

    void SetUp() override {
        // Create synthetic parsed files and elements in the InvertedIndex
        // File 0: main.cpp (Root caller, ID 0)
        // File 1: service.cpp (Service class ID 1, Service::handle ID 2)
        // File 2: repo.cpp (Repo class ID 3, Repo::query ID 4)
        // File 3: db.cpp (DbHelper ID 5, DbConnection ID 6)

        parser::ParsedFile pf0;
        pf0.file_path = "src/main.cpp";
        pf0.language = "C++";
        pf0.elements.push_back(parser::CodeElement{
            .kind = parser::ElementKind::Function,
            .name = "main",
            .location = {.start = {1, 1, 0}, .end = {10, 1, 150}},
        });
        index.add_parsed_file(pf0);

        parser::ParsedFile pf1;
        pf1.file_path = "src/service.cpp";
        pf1.language = "C++";
        pf1.elements.push_back(parser::CodeElement{
            .kind = parser::ElementKind::Class,
            .name = "Service",
            .location = {.start = {1, 1, 0}, .end = {30, 1, 400}},
        });
        pf1.elements.push_back(parser::CodeElement{
            .kind = parser::ElementKind::Method,
            .name = "handleRequest",
            .location = {.start = {10, 1, 120}, .end = {25, 1, 350}},
            .parent_context = "Service",
        });
        index.add_parsed_file(pf1);

        parser::ParsedFile pf2;
        pf2.file_path = "src/repo.cpp";
        pf2.language = "C++";
        pf2.elements.push_back(parser::CodeElement{
            .kind = parser::ElementKind::Class,
            .name = "Repository",
            .location = {.start = {1, 1, 0}, .end = {40, 1, 500}},
        });
        pf2.elements.push_back(parser::CodeElement{
            .kind = parser::ElementKind::Method,
            .name = "queryRecords",
            .location = {.start = {15, 1, 180}, .end = {35, 1, 450}},
            .parent_context = "Repository",
        });
        index.add_parsed_file(pf2);

        parser::ParsedFile pf3;
        pf3.file_path = "src/db.cpp";
        pf3.language = "C++";
        pf3.elements.push_back(parser::CodeElement{
            .kind = parser::ElementKind::Class,
            .name = "DbHelper",
            .location = {.start = {1, 1, 0}, .end = {20, 1, 250}},
        });
        pf3.elements.push_back(parser::CodeElement{
            .kind = parser::ElementKind::Struct,
            .name = "DbConnection",
            .location = {.start = {25, 1, 260}, .end = {40, 1, 420}},
        });
        index.add_parsed_file(pf3);

        expander = std::make_unique<RelationshipExpander>(graph, index, snippet_reader);
    }

    retrieval::PrimarySearchResult create_seed_result(index::ElementId id, std::string_view name,
                                                      std::string_view path, double hybrid_score = 1.0) {
        retrieval::PrimarySearchResult sr;
        sr.unit.primary_element_id = id;
        sr.unit.primary_element.name = std::string(name);
        sr.unit.primary_element.kind = parser::ElementKind::Function;
        sr.unit.file_path = path;
        sr.hybrid_score = hybrid_score;
        return sr;
    }
};

TEST_F(RelationshipExpanderTest, DepthZeroReturnsEmpty) {
    graph.add_relationship(0, 1, RelationshipKind::Calls);

    auto seed = create_seed_result(0, "main", "src/main.cpp");
    std::vector<retrieval::PrimarySearchResult> seeds = {seed};

    ExpansionConfig config;
    config.max_depth = 0;

    auto result = expander->expand("how does main run", seeds, config);
    EXPECT_TRUE(result.empty());
}

TEST_F(RelationshipExpanderTest, DepthOneDirectNeighbors) {
    // 0 (main) CALLS 1 (Service)
    // 0 (main) CONTAINS 2 (handleRequest)
    graph.add_relationship(0, 1, RelationshipKind::Calls);
    graph.add_relationship(0, 2, RelationshipKind::Contains);

    auto seed = create_seed_result(0, "main", "src/main.cpp");
    std::vector<retrieval::PrimarySearchResult> seeds = {seed};

    ExpansionConfig config;
    config.max_depth = 1;
    config.max_units = 5;

    auto result = expander->expand("service handleRequest", seeds, config);
    EXPECT_EQ(result.size(), 2u);

    // Verify depth and provenance
    for (const auto& cand : result) {
        EXPECT_EQ(cand.depth, 1u);
        EXPECT_EQ(cand.seed_element_id, 0u);
        EXPECT_EQ(cand.seed_symbol_name, "main");
    }
}

TEST_F(RelationshipExpanderTest, DepthTwoMultiHopTraversal) {
    // 0 (main) CALLS 2 (handleRequest) -> Depth 1
    // 2 (handleRequest) CALLS 4 (queryRecords) -> Depth 2
    // 4 (queryRecords) CALLS 5 (DbHelper) -> Depth 3 (should not be reached)
    graph.add_relationship(0, 2, RelationshipKind::Calls);
    graph.add_relationship(2, 4, RelationshipKind::Calls);
    graph.add_relationship(4, 5, RelationshipKind::Calls);

    auto seed = create_seed_result(0, "main", "src/main.cpp");
    std::vector<retrieval::PrimarySearchResult> seeds = {seed};

    ExpansionConfig config;
    config.max_depth = 2;
    config.max_units = 10;

    auto result = expander->expand("database query records", seeds, config);
    EXPECT_EQ(result.size(), 2u);

    // One depth-1 candidate and one depth-2 candidate
    bool has_depth_1 = false;
    bool has_depth_2 = false;
    for (const auto& cand : result) {
        if (cand.depth == 1 && cand.element_id == 2) has_depth_1 = true;
        if (cand.depth == 2 && cand.element_id == 4) has_depth_2 = true;
    }
    EXPECT_TRUE(has_depth_1);
    EXPECT_TRUE(has_depth_2);
}

TEST_F(RelationshipExpanderTest, CycleAndSelfRelationshipSafety) {
    // 0 CALLS 1
    // 1 CALLS 0 (Cycle)
    // 1 CALLS 1 (Self loop)
    graph.add_relationship(0, 1, RelationshipKind::Calls);
    graph.add_relationship(1, 0, RelationshipKind::Calls);
    graph.add_relationship(1, 1, RelationshipKind::Calls);

    auto seed = create_seed_result(0, "main", "src/main.cpp");
    std::vector<retrieval::PrimarySearchResult> seeds = {seed};

    ExpansionConfig config;
    config.max_depth = 2;
    config.max_units = 10;

    auto result = expander->expand("cycle test", seeds, config);
    // Element 0 is seed, so only Element 1 should be returned, exactly once.
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].element_id, 1u);
}

TEST_F(RelationshipExpanderTest, MultiplePathsDeduplicate) {
    // 0 CALLS 2
    // 0 CONTAINS 1
    // 1 CALLS 2 (Both depth 1 and depth 2 path lead to element 2)
    graph.add_relationship(0, 2, RelationshipKind::Calls);
    graph.add_relationship(0, 1, RelationshipKind::Contains);
    graph.add_relationship(1, 2, RelationshipKind::Calls);

    auto seed = create_seed_result(0, "main", "src/main.cpp");
    std::vector<retrieval::PrimarySearchResult> seeds = {seed};

    ExpansionConfig config;
    config.max_depth = 2;
    config.max_units = 10;

    auto result = expander->expand("multi path", seeds, config);
    EXPECT_EQ(result.size(), 2u);

    std::unordered_set<index::ElementId> ids;
    for (const auto& cand : result) {
        ids.insert(cand.element_id);
    }
    EXPECT_EQ(ids.size(), 2u);
    EXPECT_TRUE(ids.contains(1));
    EXPECT_TRUE(ids.contains(2));
}

TEST_F(RelationshipExpanderTest, AllRelationshipKindsSupported) {
    // Create edges for every relationship kind
    graph.add_relationship(0, 1, RelationshipKind::Contains);
    graph.add_relationship(0, 2, RelationshipKind::Calls);
    graph.add_relationship(0, 3, RelationshipKind::InheritsFrom);
    graph.add_relationship(0, 4, RelationshipKind::Implements);
    graph.add_relationship(0, 5, RelationshipKind::References);
    graph.add_relationship(0, 6, RelationshipKind::Imports);

    auto seed = create_seed_result(0, "main", "src/main.cpp");
    std::vector<retrieval::PrimarySearchResult> seeds = {seed};

    ExpansionConfig config;
    config.max_depth = 1;
    config.max_units = 10;

    auto result = expander->expand("all kinds", seeds, config);
    EXPECT_EQ(result.size(), 6u);
}

TEST_F(RelationshipExpanderTest, RespectsMaxUnitsBudget) {
    graph.add_relationship(0, 1, RelationshipKind::Calls);
    graph.add_relationship(0, 2, RelationshipKind::Calls);
    graph.add_relationship(0, 3, RelationshipKind::Calls);
    graph.add_relationship(0, 4, RelationshipKind::Calls);

    auto seed = create_seed_result(0, "main", "src/main.cpp");
    std::vector<retrieval::PrimarySearchResult> seeds = {seed};

    ExpansionConfig config;
    config.max_depth = 1;
    config.max_units = 2; // Strict budget: at most 2 units

    auto result = expander->expand("budget test", seeds, config);
    EXPECT_EQ(result.size(), 2u);
}

TEST_F(RelationshipExpanderTest, QuerySubjectBoostRanking) {
    // 0 CALLS 1 (Service)
    // 0 CALLS 4 (queryRecords)
    graph.add_relationship(0, 1, RelationshipKind::Calls);
    graph.add_relationship(0, 4, RelationshipKind::Calls);

    auto seed = create_seed_result(0, "main", "src/main.cpp");
    std::vector<retrieval::PrimarySearchResult> seeds = {seed};

    ExpansionConfig config;
    config.max_depth = 1;
    config.max_units = 2;
    config.prioritize_query_relevance = true;

    // Query specifically asks for records / query
    auto result = expander->expand("how to query records", seeds, config);
    ASSERT_EQ(result.size(), 2u);
    // Element 4 (queryRecords) must rank #1 due to query subject match
    EXPECT_EQ(result[0].element_id, 4u);
    EXPECT_EQ(result[1].element_id, 1u);
}

TEST_F(RelationshipExpanderTest, OutOfBoundsTargetIgnoredSafely) {
    // Add relationship to non-existent ElementId 9999
    graph.add_relationship(0, 9999, RelationshipKind::Calls);

    auto seed = create_seed_result(0, "main", "src/main.cpp");
    std::vector<retrieval::PrimarySearchResult> seeds = {seed};

    ExpansionConfig config;
    config.max_depth = 1;

    auto result = expander->expand("missing target test", seeds, config);
    EXPECT_TRUE(result.empty());
}

} // namespace
} // namespace amoeba::graph
