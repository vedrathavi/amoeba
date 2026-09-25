#include "amoeba/graph/incremental_graph_updater.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"

#include <gtest/gtest.h>

namespace amoeba::graph::test {

class IncrementalGraphUpdaterTest : public ::testing::Test {
protected:
    index::InvertedIndex index;
    RelationshipGraph graph;
    parser::SourceParser parser;
    IncrementalGraphUpdater updater{index, graph, parser};
};

TEST_F(IncrementalGraphUpdaterTest, IncrementalAddFile) {
    auto res1 = updater.add_file("src/math.cpp", "int add(int a, int b) { return a + b; }\n"
                                                 "int compute(int x) { return add(x, 1); }\n");

    EXPECT_TRUE(res1.success);
    EXPECT_EQ(updater.file_count(), 1);
    EXPECT_TRUE(updater.has_file("src/math.cpp"));
    EXPECT_GE(res1.elements_added, 2);
    EXPECT_GE(res1.relationships_added, 1);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Calls), 1);
}

TEST_F(IncrementalGraphUpdaterTest, IncrementalModifyFileReplacesRelationships) {
    updater.add_file("src/service.cpp", "void helperA() {}\n"
                                        "void execute() { helperA(); }\n");

    EXPECT_EQ(graph.relationship_count(RelationshipKind::Calls), 1);

    // Modify file to call helperB instead of helperA
    auto res = updater.replace_file("src/service.cpp", "void helperB() {}\n"
                                                       "void execute() { helperB(); }\n");

    EXPECT_TRUE(res.success);
    EXPECT_EQ(updater.file_count(), 1);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Calls), 1);

    // Verify lookup of old symbol helperA returns empty in index
    auto old_lookup = index.lookup("helpera");
    EXPECT_TRUE(old_lookup == nullptr || old_lookup->empty());

    // Verify lookup of new symbol helperB succeeds
    auto new_lookup = index.lookup("helperb");
    ASSERT_TRUE(new_lookup != nullptr);
    EXPECT_FALSE(new_lookup->empty());
}

TEST_F(IncrementalGraphUpdaterTest, IncrementalRemoveFileEradicatesStaleEdges) {
    updater.add_file("src/file_a.cpp", "void funcA() {}\n");
    updater.add_file("src/file_b.cpp", "void funcB() { funcA(); }\n");

    EXPECT_EQ(updater.file_count(), 2);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Calls), 1);

    // Remove file_b
    auto res = updater.remove_file("src/file_b.cpp");
    EXPECT_TRUE(res.success);
    EXPECT_EQ(updater.file_count(), 1);
    EXPECT_FALSE(updater.has_file("src/file_b.cpp"));

    // Verify no stale call edges remain
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Calls), 0);
}

TEST_F(IncrementalGraphUpdaterTest, CrossFileReferencesUpdateAcrossReplacements) {
    updater.add_file("include/logger.hpp", "#pragma once\n"
                                           "void logInfo(const char* msg);\n");
    updater.add_file("src/main.cpp", "#include \"logger.hpp\"\n"
                                     "void run() { logInfo(\"started\"); }\n");

    EXPECT_EQ(updater.file_count(), 2);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Includes), 1);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Calls), 1);

    // Update main.cpp to remove call to logInfo
    auto res = updater.replace_file("src/main.cpp", "#include \"logger.hpp\"\n"
                                                    "void run() { /* no log call */ }\n");

    EXPECT_TRUE(res.success);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Includes), 1);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Calls), 0);
}

TEST_F(IncrementalGraphUpdaterTest, UnchangedFileNoOp) {
    updater.add_file("src/utils.py", "def format_name(first, last):\n"
                                     "    return f'{first} {last}'\n");

    auto res = updater.replace_file("src/utils.py", "def format_name(first, last):\n"
                                                    "    return f'{first} {last}'\n");

    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.message, "file_unchanged");
}

TEST_F(IncrementalGraphUpdaterTest, PreservesPreviousStateOnUnsupportedLanguage) {
    updater.add_file("src/valid.cpp", "void test() {}\n");

    std::size_t initial_elems = index.element_count();

    auto res = updater.replace_file("src/valid.unknown_ext", "xyz");
    EXPECT_FALSE(res.success);

    // Existing valid state remains unchanged
    EXPECT_EQ(index.element_count(), initial_elems);
    EXPECT_EQ(updater.file_count(), 1);
}

}  // namespace amoeba::graph::test
