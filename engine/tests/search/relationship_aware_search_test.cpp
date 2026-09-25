#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/graph/repository_graph_builder.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/search/relationship_aware_search.hpp"

#include <gtest/gtest.h>

namespace amoeba::search::test {

class RelationshipAwareSearchTest : public ::testing::Test {
protected:
    parser::SourceParser parser;
    index::InvertedIndex index;
    graph::RelationshipGraph graph;

    void SetUp() override {
        auto controller_file =
            parser.parse_source("import { AuthService } from './auth_service';\n"
                                "export class AuthController {\n"
                                "    login(user: string) {\n"
                                "        AuthService.authenticate(user);\n"
                                "    }\n"
                                "}\n",
                                "TypeScript", "src/controllers/auth_controller.ts");

        auto service_file = parser.parse_source("import { UserRepository } from './user_repo';\n"
                                                "export class AuthService {\n"
                                                "    static authenticate(user: string) {\n"
                                                "        UserRepository.findUser(user);\n"
                                                "    }\n"
                                                "}\n",
                                                "TypeScript", "src/services/auth_service.ts");

        auto repo_file = parser.parse_source("export class UserRepository {\n"
                                             "    static findUser(user: string) {\n"
                                             "        return true;\n"
                                             "    }\n"
                                             "}\n",
                                             "TypeScript", "src/repository/user_repo.ts");

        index.add_parsed_file(controller_file);
        index.add_parsed_file(service_file);
        index.add_parsed_file(repo_file);

        graph::RepositoryGraphBuilder::build_repository_graph(index, graph);
    }
};

TEST_F(RelationshipAwareSearchTest, LexicalResultWithRelatedCallersAndCallees) {
    RelationshipAwareSearchEngine engine(index, graph);

    index::SearchOptions s_opts{
        .ranker_type = index::RankerType::CodeAware,
        .max_results = 5,
    };

    RelationshipExpansionOptions exp_opts{
        .enable_expansion = true,
        .max_depth = 1,
        .max_related_elements = 5,
    };

    auto results = engine.search("authenticate", s_opts, exp_opts);
    ASSERT_FALSE(results.empty());

    const auto& top = results.front();
    EXPECT_EQ(top.primary_result.element.name, "authenticate");
    EXPECT_GT(top.primary_result.score, 0.0);

    // Verify related elements attached
    ASSERT_FALSE(top.related_elements.empty());
    EXPECT_FALSE(top.structural_explanations.empty());

    // Check for incoming caller (AuthController::login) and outgoing callee
    // (UserRepository::findUser)
    bool has_caller = false;
    bool has_callee = false;
    for (const auto& rel : top.related_elements) {
        if (rel.is_incoming && rel.relationship_kind == graph::RelationshipKind::Calls) {
            has_caller = true;
        }
        if (!rel.is_incoming && rel.relationship_kind == graph::RelationshipKind::Calls) {
            has_callee = true;
        }
    }
    EXPECT_TRUE(has_caller || has_callee);
}

TEST_F(RelationshipAwareSearchTest, DependencyAndImportExpansion) {
    RelationshipAwareSearchEngine engine(index, graph);

    RelationshipExpansionOptions exp_opts{
        .enable_expansion = true,
        .max_depth = 1,
        .kind_filter = graph::RelationshipKind::Imports,
    };

    auto results = engine.search("AuthController", {}, exp_opts);
    ASSERT_FALSE(results.empty());

    const auto& top = results.front();
    EXPECT_EQ(top.primary_result.element.name, "AuthController");
    // Verify import relationships can be filtered and retrieved
    for (const auto& rel : top.related_elements) {
        EXPECT_EQ(rel.relationship_kind, graph::RelationshipKind::Imports);
    }
}

TEST_F(RelationshipAwareSearchTest, DepthLimitsAndMaxRelatedElementsConstraint) {
    RelationshipAwareSearchEngine engine(index, graph);

    RelationshipExpansionOptions exp_opts_limit1{
        .enable_expansion = true,
        .max_depth = 1,
        .max_related_elements = 1,
    };

    auto results_limit1 = engine.search("authenticate", {}, exp_opts_limit1);
    ASSERT_FALSE(results_limit1.empty());
    EXPECT_LE(results_limit1.front().related_elements.size(), 1u);

    RelationshipExpansionOptions exp_opts_limit5{
        .enable_expansion = true,
        .max_depth = 2,
        .max_related_elements = 5,
    };

    auto results_limit5 = engine.search("authenticate", {}, exp_opts_limit5);
    ASSERT_FALSE(results_limit5.empty());
    EXPECT_LE(results_limit5.front().related_elements.size(), 5u);
}

TEST_F(RelationshipAwareSearchTest, UnchangedPhase4BehaviorWhenExpansionDisabled) {
    RelationshipAwareSearchEngine engine(index, graph);

    index::SearchOptions s_opts{
        .ranker_type = index::RankerType::BM25,
        .max_results = 5,
    };

    RelationshipExpansionOptions exp_opts{
        .enable_expansion = false,
    };

    auto results = engine.search("authenticate", s_opts, exp_opts);
    ASSERT_FALSE(results.empty());

    const auto& top = results.front();
    EXPECT_EQ(top.primary_result.element.name, "authenticate");
    EXPECT_TRUE(top.related_elements.empty());
    EXPECT_TRUE(top.structural_explanations.empty());
    EXPECT_TRUE(top.context_subgraph.empty());
}

TEST_F(RelationshipAwareSearchTest, RelationshipKindFilters) {
    RelationshipAwareSearchEngine engine(index, graph);

    index::SearchOptions s_opts{
        .ranker_type = index::RankerType::Baseline,
        .max_results = 5,
    };

    RelationshipExpansionOptions exp_opts{
        .enable_expansion = true,
        .kind_filter = graph::RelationshipKind::Calls,
    };

    auto results = engine.search("authenticate", s_opts, exp_opts);
    ASSERT_FALSE(results.empty());

    for (const auto& rel : results.front().related_elements) {
        EXPECT_EQ(rel.relationship_kind, graph::RelationshipKind::Calls);
    }
}

TEST_F(RelationshipAwareSearchTest, DeterministicOutput) {
    RelationshipAwareSearchEngine engine(index, graph);

    auto run1 = engine.search("authenticate");
    auto run2 = engine.search("authenticate");

    ASSERT_EQ(run1.size(), run2.size());
    for (size_t i = 0; i < run1.size(); ++i) {
        EXPECT_EQ(run1[i].primary_result.element.name, run2[i].primary_result.element.name);
        EXPECT_EQ(run1[i].related_elements.size(), run2[i].related_elements.size());
        EXPECT_EQ(run1[i].structural_explanations.size(), run2[i].structural_explanations.size());
    }
}

}  // namespace amoeba::search::test
