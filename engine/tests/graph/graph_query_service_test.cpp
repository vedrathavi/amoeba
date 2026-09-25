#include "amoeba/graph/graph_query_service.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/graph/repository_graph_builder.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"

#include <gtest/gtest.h>

namespace amoeba::graph::test {

class GraphQueryServiceTest : public ::testing::Test {
protected:
    RelationshipGraph graph;
    GraphQueryService query{graph};
};

TEST_F(GraphQueryServiceTest, ChainGraphDepthLimitedTraversal) {
    // 1 -> 2 -> 3 -> 4
    graph.add_relationship(1, 2, RelationshipKind::Calls);
    graph.add_relationship(2, 3, RelationshipKind::Calls);
    graph.add_relationship(3, 4, RelationshipKind::Calls);

    // Callers and Callees
    auto c2 = query.callees(2);
    ASSERT_EQ(c2.size(), 1);
    EXPECT_EQ(c2.front(), 3);

    auto in2 = query.callers(2);
    ASSERT_EQ(in2.size(), 1);
    EXPECT_EQ(in2.front(), 1);

    // Depth-1 subgraph from 1
    auto sub1 = query.focused_subgraph(1, TraversalOptions{.max_depth = 1});
    EXPECT_EQ(sub1.node_count(), 2);  // {1, 2}
    EXPECT_EQ(sub1.edge_count(), 1);

    // Depth-2 subgraph from 1
    auto sub2 = query.focused_subgraph(1, TraversalOptions{.max_depth = 2});
    EXPECT_EQ(sub2.node_count(), 3);  // {1, 2, 3}
    EXPECT_EQ(sub2.edge_count(), 2);

    // Depth-3 subgraph from 1
    auto sub3 = query.focused_subgraph(1, TraversalOptions{.max_depth = 3});
    EXPECT_EQ(sub3.node_count(), 4);  // {1, 2, 3, 4}
    EXPECT_EQ(sub3.edge_count(), 3);
}

TEST_F(GraphQueryServiceTest, BranchingGraphNeighborhood) {
    // Root 1 calls 2, 3, 4
    // 5 calls 1
    graph.add_relationship(1, 2, RelationshipKind::Calls);
    graph.add_relationship(1, 3, RelationshipKind::Calls);
    graph.add_relationship(1, 4, RelationshipKind::Calls);
    graph.add_relationship(5, 1, RelationshipKind::Calls);

    auto callees = query.callees(1);
    EXPECT_EQ(callees.size(), 3);

    auto callers = query.callers(1);
    ASSERT_EQ(callers.size(), 1);
    EXPECT_EQ(callers.front(), 5);

    auto neighborhood = query.neighborhood_subgraph(1, 1);
    EXPECT_EQ(neighborhood.node_count(), 5);  // {1, 2, 3, 4, 5}
    EXPECT_EQ(neighborhood.edge_count(), 4);
}

TEST_F(GraphQueryServiceTest, CycleGraphTerminationAndSafety) {
    // 1 -> 2 -> 3 -> 1
    graph.add_relationship(1, 2, RelationshipKind::Calls);
    graph.add_relationship(2, 3, RelationshipKind::Calls);
    graph.add_relationship(3, 1, RelationshipKind::Calls);

    auto sub = query.focused_subgraph(1, TraversalOptions{.max_depth = 10});
    EXPECT_EQ(sub.node_count(), 3);  // {1, 2, 3}
    EXPECT_EQ(sub.edge_count(), 3);  // All 3 cycle edges captured safely
}

TEST_F(GraphQueryServiceTest, RelationshipKindFilters) {
    // 1 CONTAINS 2, 1 CALLS 3, 1 INHERITS_FROM 4
    graph.add_relationship(1, 2, RelationshipKind::Contains);
    graph.add_relationship(1, 3, RelationshipKind::Calls);
    graph.add_relationship(1, 4, RelationshipKind::InheritsFrom);

    auto children = query.children(1);
    ASSERT_EQ(children.size(), 1);
    EXPECT_EQ(children.front(), 2);

    auto callees = query.callees(1);
    ASSERT_EQ(callees.size(), 1);
    EXPECT_EQ(callees.front(), 3);

    auto bases = query.base_types(1);
    ASSERT_EQ(bases.size(), 1);
    EXPECT_EQ(bases.front(), 4);

    // Subgraph filtered by Calls
    auto calls_sub = query.call_graph_subgraph(1, 1);
    EXPECT_EQ(calls_sub.node_count(), 2);  // {1, 3}
    EXPECT_EQ(calls_sub.edge_count(), 1);
}

TEST_F(GraphQueryServiceTest, TypeHierarchySubgraphs) {
    // Interface 10
    // BaseClass 20 IMPLEMENTS 10
    // DerivedClass 30 INHERITS_FROM 20
    graph.add_relationship(20, 10, RelationshipKind::Implements);
    graph.add_relationship(30, 20, RelationshipKind::InheritsFrom);

    auto bases30 = query.base_types(30);
    ASSERT_EQ(bases30.size(), 1);
    EXPECT_EQ(bases30.front(), 20);

    auto derived20 = query.derived_types(20);
    ASSERT_EQ(derived20.size(), 1);
    EXPECT_EQ(derived20.front(), 30);

    auto type_sub = query.type_hierarchy_subgraph(20, 2);
    EXPECT_EQ(type_sub.node_count(), 3);  // {10, 20, 30}
    EXPECT_EQ(type_sub.edge_count(), 2);
}

TEST_F(GraphQueryServiceTest, RealisticCrossFileRepositoryQueries) {
    parser::SourceParser parser;
    auto repo_file = parser.parse_source("export class UserRepository { findUser() {} }",
                                         "TypeScript", "src/repository.ts");
    auto service_file = parser.parse_source("import { UserRepository } from './repository';\n"
                                            "export class AuthService {\n"
                                            "    auth() { this.findUser(); }\n"
                                            "}",
                                            "TypeScript", "src/service.ts");

    index::InvertedIndex index;
    index.add_parsed_file(repo_file);
    index.add_parsed_file(service_file);

    RelationshipGraph repo_graph;
    RepositoryGraphBuilder::build_repository_graph(index, repo_graph);

    GraphQueryService repo_query(repo_graph);
    auto all_nodes = repo_graph.all_nodes();
    ASSERT_FALSE(all_nodes.empty());

    for (ElementId node : all_nodes) {
        auto neighbors = repo_graph.outgoing_neighbors(node);
        auto callers = repo_query.callers(node);
        auto callees = repo_query.callees(node);
        auto sub = repo_query.neighborhood_subgraph(node, 1);
        EXPECT_GE(sub.node_count(), 1);
    }
}

}  // namespace amoeba::graph::test
