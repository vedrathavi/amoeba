#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/graph/relationship_kind.hpp"

#include <gtest/gtest.h>

namespace amoeba::graph::test {

class RelationshipGraphTest : public ::testing::Test {
protected:
    RelationshipGraph graph;
};

TEST_F(RelationshipGraphTest, EmptyGraphInitialState) {
    EXPECT_TRUE(graph.empty());
    EXPECT_EQ(graph.relationship_count(), 0);
    EXPECT_EQ(graph.node_count(), 0);
    EXPECT_FALSE(graph.has_node(1));
    EXPECT_TRUE(graph.outgoing_relationships(1).empty());
    EXPECT_TRUE(graph.incoming_relationships(1).empty());
    EXPECT_TRUE(graph.all_nodes().empty());
}

TEST_F(RelationshipGraphTest, AddSingleRelationship) {
    constexpr ElementId kSource = 101;
    constexpr ElementId kTarget = 202;

    EXPECT_TRUE(graph.add_relationship(kSource, kTarget, RelationshipKind::Calls));
    EXPECT_FALSE(graph.empty());
    EXPECT_EQ(graph.relationship_count(), 1);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Calls), 1);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::InheritsFrom), 0);
    EXPECT_EQ(graph.node_count(), 2);

    EXPECT_TRUE(graph.has_node(kSource));
    EXPECT_TRUE(graph.has_node(kTarget));
    EXPECT_TRUE(graph.has_relationship(kSource, kTarget, RelationshipKind::Calls));
    EXPECT_FALSE(graph.has_relationship(kTarget, kSource, RelationshipKind::Calls));
    EXPECT_FALSE(graph.has_relationship(kSource, kTarget, RelationshipKind::InheritsFrom));

    auto outgoing = graph.outgoing_relationships(kSource);
    ASSERT_EQ(outgoing.size(), 1);
    EXPECT_EQ(outgoing[0].source, kSource);
    EXPECT_EQ(outgoing[0].target, kTarget);
    EXPECT_EQ(outgoing[0].kind, RelationshipKind::Calls);

    auto incoming = graph.incoming_relationships(kTarget);
    ASSERT_EQ(incoming.size(), 1);
    EXPECT_EQ(incoming[0].source, kSource);
    EXPECT_EQ(incoming[0].target, kTarget);
    EXPECT_EQ(incoming[0].kind, RelationshipKind::Calls);
}

TEST_F(RelationshipGraphTest, DirectedEdgeSemantics) {
    constexpr ElementId kA = 1;
    constexpr ElementId kB = 2;

    // A --CALLS--> B does not imply B --CALLS--> A
    EXPECT_TRUE(graph.add_relationship(kA, kB, RelationshipKind::Calls));

    EXPECT_EQ(graph.outgoing_relationships(kA).size(), 1);
    EXPECT_EQ(graph.outgoing_relationships(kB).size(), 0);

    EXPECT_EQ(graph.incoming_relationships(kA).size(), 0);
    EXPECT_EQ(graph.incoming_relationships(kB).size(), 1);
}

TEST_F(RelationshipGraphTest, RejectDuplicateRelationships) {
    constexpr ElementId kSource = 10;
    constexpr ElementId kTarget = 20;

    EXPECT_TRUE(graph.add_relationship(kSource, kTarget, RelationshipKind::Imports));
    // Adding identical relationship must return false and not inflate count
    EXPECT_FALSE(graph.add_relationship(kSource, kTarget, RelationshipKind::Imports));

    EXPECT_EQ(graph.relationship_count(), 1);
    EXPECT_EQ(graph.outgoing_relationships(kSource).size(), 1);
    EXPECT_EQ(graph.incoming_relationships(kTarget).size(), 1);
}

TEST_F(RelationshipGraphTest, MultipleRelationshipKindsBetweenSameElements) {
    constexpr ElementId kClass = 100;
    constexpr ElementId kInterface = 200;

    // Element 100 both Implements and References Element 200
    EXPECT_TRUE(graph.add_relationship(kClass, kInterface, RelationshipKind::Implements));
    EXPECT_TRUE(graph.add_relationship(kClass, kInterface, RelationshipKind::References));

    EXPECT_EQ(graph.relationship_count(), 2);
    EXPECT_EQ(graph.node_count(), 2);

    auto between = graph.relationships_between(kClass, kInterface);
    EXPECT_EQ(between.size(), 2);

    EXPECT_EQ(graph.outgoing_relationships(kClass, RelationshipKind::Implements).size(), 1);
    EXPECT_EQ(graph.outgoing_relationships(kClass, RelationshipKind::References).size(), 1);
    EXPECT_EQ(graph.outgoing_relationships(kClass, RelationshipKind::Calls).size(), 0);
}

TEST_F(RelationshipGraphTest, SelfRelationship) {
    constexpr ElementId kRecursiveFunc = 42;

    // Recursive call: 42 --CALLS--> 42
    EXPECT_TRUE(graph.add_relationship(kRecursiveFunc, kRecursiveFunc, RelationshipKind::Calls));

    EXPECT_EQ(graph.relationship_count(), 1);
    EXPECT_EQ(graph.node_count(), 1);
    EXPECT_TRUE(graph.has_node(kRecursiveFunc));
    EXPECT_TRUE(graph.has_relationship(kRecursiveFunc, kRecursiveFunc, RelationshipKind::Calls));

    EXPECT_EQ(graph.outgoing_relationships(kRecursiveFunc).size(), 1);
    EXPECT_EQ(graph.incoming_relationships(kRecursiveFunc).size(), 1);

    // Remove self relationship
    EXPECT_TRUE(graph.remove_relationship(kRecursiveFunc, kRecursiveFunc, RelationshipKind::Calls));
    EXPECT_EQ(graph.relationship_count(), 0);
    EXPECT_EQ(graph.node_count(), 0);
    EXPECT_FALSE(graph.has_node(kRecursiveFunc));
}

TEST_F(RelationshipGraphTest, RemoveRelationship) {
    constexpr ElementId kA = 1;
    constexpr ElementId kB = 2;
    constexpr ElementId kC = 3;

    graph.add_relationship(kA, kB, RelationshipKind::Calls);
    graph.add_relationship(kA, kC, RelationshipKind::Contains);

    EXPECT_EQ(graph.relationship_count(), 2);
    EXPECT_EQ(graph.node_count(), 3);

    // Removing non-existent relationship returns false
    EXPECT_FALSE(graph.remove_relationship(kB, kC, RelationshipKind::Calls));
    EXPECT_FALSE(graph.remove_relationship(kA, kB, RelationshipKind::InheritsFrom));

    // Remove kA -> kB Calls
    EXPECT_TRUE(graph.remove_relationship(kA, kB, RelationshipKind::Calls));
    EXPECT_EQ(graph.relationship_count(), 1);
    EXPECT_FALSE(graph.has_relationship(kA, kB, RelationshipKind::Calls));
    EXPECT_TRUE(graph.has_relationship(kA, kC, RelationshipKind::Contains));

    // kB has no remaining relationships, so node_count decreases to 2 (kA, kC)
    EXPECT_FALSE(graph.has_node(kB));
    EXPECT_TRUE(graph.has_node(kA));
    EXPECT_TRUE(graph.has_node(kC));
    EXPECT_EQ(graph.node_count(), 2);

    // Remove kA -> kC Contains
    EXPECT_TRUE(graph.remove_relationship(kA, kC, RelationshipKind::Contains));
    EXPECT_TRUE(graph.empty());
    EXPECT_EQ(graph.node_count(), 0);
}

TEST_F(RelationshipGraphTest, QueryUnknownElementsIsSafe) {
    constexpr ElementId kUnknown = 999999;

    EXPECT_FALSE(graph.has_node(kUnknown));
    EXPECT_FALSE(graph.has_relationship(kUnknown, 1, RelationshipKind::Calls));
    EXPECT_TRUE(graph.outgoing_relationships(kUnknown).empty());
    EXPECT_TRUE(graph.outgoing_relationships(kUnknown, RelationshipKind::Calls).empty());
    EXPECT_TRUE(graph.incoming_relationships(kUnknown).empty());
    EXPECT_TRUE(graph.incoming_relationships(kUnknown, RelationshipKind::Calls).empty());
    EXPECT_TRUE(graph.relationships_between(kUnknown, 1).empty());
}

TEST_F(RelationshipGraphTest, AllNodesDeterministicOrdering) {
    graph.add_relationship(50, 10, RelationshipKind::Calls);
    graph.add_relationship(30, 20, RelationshipKind::Includes);
    graph.add_relationship(10, 40, RelationshipKind::Contains);

    auto nodes = graph.all_nodes();
    ASSERT_EQ(nodes.size(), 5);
    EXPECT_EQ(nodes[0], 10);
    EXPECT_EQ(nodes[1], 20);
    EXPECT_EQ(nodes[2], 30);
    EXPECT_EQ(nodes[3], 40);
    EXPECT_EQ(nodes[4], 50);
}

TEST_F(RelationshipGraphTest, ClearGraph) {
    graph.add_relationship(1, 2, RelationshipKind::Calls);
    graph.add_relationship(2, 3, RelationshipKind::Calls);
    graph.add_relationship(3, 4, RelationshipKind::Calls);

    EXPECT_EQ(graph.relationship_count(), 3);
    EXPECT_EQ(graph.node_count(), 4);

    graph.clear();

    EXPECT_TRUE(graph.empty());
    EXPECT_EQ(graph.relationship_count(), 0);
    EXPECT_EQ(graph.node_count(), 0);
    EXPECT_FALSE(graph.has_node(1));
}

TEST_F(RelationshipGraphTest, CompactRepresentationContract) {
    // Verify that Relationship struct is lightweight and contains no string/vector members
    static_assert(sizeof(Relationship) <= 16, "Relationship must remain compact");
    static_assert(std::is_trivially_copyable_v<Relationship>,
                  "Relationship must be trivially copyable");
}

TEST_F(RelationshipGraphTest, RelationshipKindConversion) {
    EXPECT_EQ(to_string(RelationshipKind::Contains), "CONTAINS");
    EXPECT_EQ(to_string(RelationshipKind::Imports), "IMPORTS");
    EXPECT_EQ(to_string(RelationshipKind::Includes), "INCLUDES");
    EXPECT_EQ(to_string(RelationshipKind::Calls), "CALLS");
    EXPECT_EQ(to_string(RelationshipKind::References), "REFERENCES");
    EXPECT_EQ(to_string(RelationshipKind::InheritsFrom), "INHERITS_FROM");
    EXPECT_EQ(to_string(RelationshipKind::Implements), "IMPLEMENTS");

    EXPECT_EQ(relationship_kind_from_string("CALLS"), RelationshipKind::Calls);
    EXPECT_EQ(relationship_kind_from_string("calls"), RelationshipKind::Calls);
    EXPECT_EQ(relationship_kind_from_string("INHERITS_FROM"), RelationshipKind::InheritsFrom);
    EXPECT_EQ(relationship_kind_from_string("NON_EXISTENT"), std::nullopt);
}

TEST_F(RelationshipGraphTest, ConceptualCallChainExample) {
    // AuthController (1) -> AuthService (2) -> UserRepository (3)
    constexpr ElementId kAuthController = 1;
    constexpr ElementId kAuthService = 2;
    constexpr ElementId kUserRepository = 3;

    EXPECT_TRUE(graph.add_relationship(kAuthController, kAuthService, RelationshipKind::Calls));
    EXPECT_TRUE(graph.add_relationship(kAuthService, kUserRepository, RelationshipKind::Calls));

    // Query callees of AuthController
    auto auth_callees = graph.outgoing_relationships(kAuthController, RelationshipKind::Calls);
    ASSERT_EQ(auth_callees.size(), 1);
    EXPECT_EQ(auth_callees[0].target, kAuthService);

    // Query callers of AuthService
    auto service_callers = graph.incoming_relationships(kAuthService, RelationshipKind::Calls);
    ASSERT_EQ(service_callers.size(), 1);
    EXPECT_EQ(service_callers[0].source, kAuthController);

    // Query callees of AuthService
    auto service_callees = graph.outgoing_relationships(kAuthService, RelationshipKind::Calls);
    ASSERT_EQ(service_callees.size(), 1);
    EXPECT_EQ(service_callees[0].target, kUserRepository);
}

TEST_F(RelationshipGraphTest, ImmediateNeighborsQueries) {
    constexpr ElementId kA = 1;
    constexpr ElementId kB = 2;
    constexpr ElementId kC = 3;

    graph.add_relationship(kA, kB, RelationshipKind::Calls);
    graph.add_relationship(kA, kC, RelationshipKind::References);
    // Duplicate logical target with different kind
    graph.add_relationship(kA, kB, RelationshipKind::Imports);

    auto out_neighbors = graph.outgoing_neighbors(kA);
    ASSERT_EQ(out_neighbors.size(), 2);
    // Neighbors are unique element IDs (kB and kC)
    EXPECT_TRUE((out_neighbors[0] == kB && out_neighbors[1] == kC) ||
                (out_neighbors[0] == kC && out_neighbors[1] == kB));

    auto calls_neighbors = graph.outgoing_neighbors(kA, RelationshipKind::Calls);
    ASSERT_EQ(calls_neighbors.size(), 1);
    EXPECT_EQ(calls_neighbors[0], kB);

    auto inc_neighbors = graph.incoming_neighbors(kB);
    ASSERT_EQ(inc_neighbors.size(), 1);
    EXPECT_EQ(inc_neighbors[0], kA);

    EXPECT_TRUE(graph.outgoing_neighbors(999).empty());
    EXPECT_TRUE(graph.incoming_neighbors(999).empty());
}

TEST_F(RelationshipGraphTest, TraversalSimpleChain) {
    // 1 -> 2 -> 3 -> 4
    graph.add_relationship(1, 2, RelationshipKind::Calls);
    graph.add_relationship(2, 3, RelationshipKind::Calls);
    graph.add_relationship(3, 4, RelationshipKind::Calls);

    // Depth 0: only start node
    {
        TraversalOptions opt{.direction = TraversalDirection::Outgoing, .max_depth = 0};
        auto steps = graph.traverse(1, opt);
        ASSERT_EQ(steps.size(), 1);
        EXPECT_EQ(steps[0].node_id, 1);
        EXPECT_EQ(steps[0].depth, 0);

        auto reachable = graph.reachable_nodes(1, opt);
        ASSERT_EQ(reachable.size(), 1);
        EXPECT_EQ(reachable[0], 1);
    }

    // Depth 1: 1 -> 2
    {
        TraversalOptions opt{.direction = TraversalDirection::Outgoing, .max_depth = 1};
        auto steps = graph.traverse(1, opt);
        ASSERT_EQ(steps.size(), 2);
        EXPECT_EQ(steps[0], (TraversalStep{1, 0}));
        EXPECT_EQ(steps[1], (TraversalStep{2, 1}));

        auto reachable = graph.reachable_nodes(1, opt);
        ASSERT_EQ(reachable.size(), 1);
        EXPECT_EQ(reachable[0], 2);
    }

    // Depth 2: 1 -> 2 -> 3
    {
        TraversalOptions opt{.direction = TraversalDirection::Outgoing, .max_depth = 2};
        auto steps = graph.traverse(1, opt);
        ASSERT_EQ(steps.size(), 3);
        EXPECT_EQ(steps[0], (TraversalStep{1, 0}));
        EXPECT_EQ(steps[1], (TraversalStep{2, 1}));
        EXPECT_EQ(steps[2], (TraversalStep{3, 2}));
    }

    // Depth 10 (exceeds chain length): reaches all 4
    {
        TraversalOptions opt{.direction = TraversalDirection::Outgoing, .max_depth = 10};
        auto steps = graph.traverse(1, opt);
        ASSERT_EQ(steps.size(), 4);
        EXPECT_EQ(steps[3], (TraversalStep{4, 3}));
    }
}

TEST_F(RelationshipGraphTest, TraversalBranchingTree) {
    // Root 1 -> (2, 3)
    // 2 -> (4, 5)
    // 3 -> (6)
    graph.add_relationship(1, 2, RelationshipKind::Contains);
    graph.add_relationship(1, 3, RelationshipKind::Contains);
    graph.add_relationship(2, 4, RelationshipKind::Contains);
    graph.add_relationship(2, 5, RelationshipKind::Contains);
    graph.add_relationship(3, 6, RelationshipKind::Contains);

    TraversalOptions opt{.direction = TraversalDirection::Outgoing, .max_depth = 2};
    auto steps = graph.traverse(1, opt);
    ASSERT_EQ(steps.size(), 6);  // 1 root + 2 children + 3 grandchildren

    EXPECT_EQ(steps[0], (TraversalStep{1, 0}));
    // BFS level 1
    EXPECT_EQ(steps[1].depth, 1);
    EXPECT_EQ(steps[2].depth, 1);
    // BFS level 2
    EXPECT_EQ(steps[3].depth, 2);
    EXPECT_EQ(steps[4].depth, 2);
    EXPECT_EQ(steps[5].depth, 2);
}

TEST_F(RelationshipGraphTest, TraversalCycleHandling) {
    // Cyclic graph: 1 -> 2 -> 3 -> 1
    graph.add_relationship(1, 2, RelationshipKind::Calls);
    graph.add_relationship(2, 3, RelationshipKind::Calls);
    graph.add_relationship(3, 1, RelationshipKind::Calls);

    // Large depth must terminate safely and visit each node exactly once
    TraversalOptions opt{.direction = TraversalDirection::Outgoing, .max_depth = 100};
    auto steps = graph.traverse(1, opt);
    ASSERT_EQ(steps.size(), 3);

    EXPECT_EQ(steps[0], (TraversalStep{1, 0}));
    EXPECT_EQ(steps[1], (TraversalStep{2, 1}));
    EXPECT_EQ(steps[2], (TraversalStep{3, 2}));
}

TEST_F(RelationshipGraphTest, TraversalSelfLoopHandling) {
    // Self loop: 1 -> 1 and 1 -> 2
    graph.add_relationship(1, 1, RelationshipKind::Calls);
    graph.add_relationship(1, 2, RelationshipKind::Calls);

    TraversalOptions opt{.direction = TraversalDirection::Outgoing, .max_depth = 10};
    auto steps = graph.traverse(1, opt);
    ASSERT_EQ(steps.size(), 2);

    EXPECT_EQ(steps[0], (TraversalStep{1, 0}));
    EXPECT_EQ(steps[1], (TraversalStep{2, 1}));
}

TEST_F(RelationshipGraphTest, TraversalDisconnectedComponents) {
    // Component A: 1 -> 2
    // Component B: 3 -> 4
    graph.add_relationship(1, 2, RelationshipKind::Calls);
    graph.add_relationship(3, 4, RelationshipKind::Calls);

    TraversalOptions opt{.direction = TraversalDirection::Outgoing, .max_depth = 5};
    auto steps = graph.traverse(1, opt);
    ASSERT_EQ(steps.size(), 2);
    EXPECT_EQ(steps[0].node_id, 1);
    EXPECT_EQ(steps[1].node_id, 2);
}

TEST_F(RelationshipGraphTest, TraversalIncomingDirection) {
    // A (1) -> B (2) -> C (3)
    graph.add_relationship(1, 2, RelationshipKind::Calls);
    graph.add_relationship(2, 3, RelationshipKind::Calls);

    // Incoming traversal starting at C (3)
    TraversalOptions opt{.direction = TraversalDirection::Incoming, .max_depth = 2};
    auto steps = graph.traverse(3, opt);
    ASSERT_EQ(steps.size(), 3);

    EXPECT_EQ(steps[0], (TraversalStep{3, 0}));
    EXPECT_EQ(steps[1], (TraversalStep{2, 1}));  // caller of C
    EXPECT_EQ(steps[2], (TraversalStep{1, 2}));  // caller of B
}

TEST_F(RelationshipGraphTest, TraversalKindFiltering) {
    // 1 --CALLS--> 2
    // 1 --IMPORTS--> 3
    // 2 --CALLS--> 4
    // 2 --REFERENCES--> 5
    graph.add_relationship(1, 2, RelationshipKind::Calls);
    graph.add_relationship(1, 3, RelationshipKind::Imports);
    graph.add_relationship(2, 4, RelationshipKind::Calls);
    graph.add_relationship(2, 5, RelationshipKind::References);

    // Traverse only CALLS edges
    TraversalOptions opt{.direction = TraversalDirection::Outgoing,
                         .max_depth = 3,
                         .kind_filter = RelationshipKind::Calls};
    auto steps = graph.traverse(1, opt);
    ASSERT_EQ(steps.size(), 3);

    EXPECT_EQ(steps[0], (TraversalStep{1, 0}));
    EXPECT_EQ(steps[1], (TraversalStep{2, 1}));
    EXPECT_EQ(steps[2], (TraversalStep{4, 2}));
}

TEST_F(RelationshipGraphTest, TraversalUnknownOrEmptyGraph) {
    TraversalOptions opt{.direction = TraversalDirection::Outgoing, .max_depth = 2};
    // Empty graph
    EXPECT_TRUE(graph.traverse(1, opt).empty());
    EXPECT_TRUE(graph.reachable_nodes(1, opt).empty());

    // Non-empty graph but unknown node ID
    graph.add_relationship(10, 20, RelationshipKind::Calls);
    EXPECT_TRUE(graph.traverse(999, opt).empty());
    EXPECT_TRUE(graph.reachable_nodes(999, opt).empty());
}

}  // namespace amoeba::graph::test
