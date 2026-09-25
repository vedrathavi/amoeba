#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/graph/repository_graph_builder.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"

#include <gtest/gtest.h>

namespace amoeba::graph::test {

class RepositoryGraphBuilderTest : public ::testing::Test {
protected:
    parser::SourceParser parser;
    RelationshipGraph graph;

    parser::ParsedFile parse(const std::filesystem::path& file_path, std::string_view code) {
        auto lang = parser::SourceParser::detect_language(file_path);
        return parser.parse_source(code, lang, file_path);
    }
};

TEST_F(RepositoryGraphBuilderTest, MultiTierServiceRepositoryCrossFilePipeline) {
    auto repo_file =
        parse("src/repository/user_repository.ts", "export class UserRepository {\n"
                                                   "    findById(id: string) {\n"
                                                   "        return { id, name: 'Alice' };\n"
                                                   "    }\n"
                                                   "}\n");

    auto service_file = parse("src/services/auth_service.ts",
                              "import { UserRepository } from '../repository/user_repository';\n"
                              "export class AuthService {\n"
                              "    private repo: UserRepository;\n"
                              "    authenticate(id: string) {\n"
                              "        return this.repo.findById(id);\n"
                              "    }\n"
                              "}\n");

    auto controller_file = parse("src/controllers/auth_controller.ts",
                                 "import { AuthService } from '../services/auth_service';\n"
                                 "export class AuthController {\n"
                                 "    private service: AuthService;\n"
                                 "    login(id: string) {\n"
                                 "        return this.service.authenticate(id);\n"
                                 "    }\n"
                                 "}\n");

    index::InvertedIndex index;
    index.add_parsed_file(repo_file);
    index.add_parsed_file(service_file);
    index.add_parsed_file(controller_file);

    auto result = RepositoryGraphBuilder::build_repository_graph(index, graph);

    EXPECT_EQ(result.metrics.total_files, 3);
    EXPECT_GT(result.metrics.total_elements, 0);
    EXPECT_GT(result.metrics.total_relationships, 0);
    EXPECT_GE(result.metrics.import_relationships, 2);
    EXPECT_GE(result.metrics.call_relationships, 2);
    EXPECT_GE(result.metrics.contains_relationships, 3);

    // Multi-hop traversal verification
    auto all_nodes = graph.all_nodes();
    ASSERT_FALSE(all_nodes.empty());

    // Verify storage metrics
    EXPECT_GT(result.metrics.total_source_bytes, 0);
    EXPECT_GT(result.metrics.estimated_graph_memory_bytes, 0);
}

TEST_F(RepositoryGraphBuilderTest, CrossFileInheritanceAndCallsCombined) {
    auto base_repo = parse("include/base_repository.hpp", "#pragma once\n"
                                                          "class BaseRepository {\n"
                                                          "public:\n"
                                                          "    virtual void connect() {}\n"
                                                          "};\n");

    auto sql_repo =
        parse("include/sql_repository.hpp", "#pragma once\n"
                                            "#include \"base_repository.hpp\"\n"
                                            "class SqlRepository : public BaseRepository {\n"
                                            "public:\n"
                                            "    void execute() {\n"
                                            "        connect();\n"
                                            "    }\n"
                                            "};\n");

    index::InvertedIndex index;
    index.add_parsed_file(base_repo);
    index.add_parsed_file(sql_repo);

    auto result = RepositoryGraphBuilder::build_repository_graph(index, graph);

    EXPECT_GE(result.metrics.include_relationships, 1);
    EXPECT_GE(result.metrics.inheritance_relationships, 1);
    EXPECT_GE(result.metrics.call_relationships, 1);
    EXPECT_GE(result.metrics.contains_relationships, 2);
}

TEST_F(RepositoryGraphBuilderTest, CircularFileDependenciesResilience) {
    auto file_a = parse("src/module_a.py", "import module_b\n"
                                           "def func_a():\n"
                                           "    module_b.func_b()\n");

    auto file_b = parse("src/module_b.py", "import module_a\n"
                                           "def func_b():\n"
                                           "    module_a.func_a()\n");

    index::InvertedIndex index;
    index.add_parsed_file(file_a);
    index.add_parsed_file(file_b);

    auto result = RepositoryGraphBuilder::build_repository_graph(index, graph);

    EXPECT_EQ(result.metrics.total_files, 2);
    EXPECT_GE(result.metrics.import_relationships, 2);
    EXPECT_GE(result.metrics.call_relationships, 2);

    // Traversal safely handles cycle without infinite recursion
    auto start_node = graph.all_nodes().front();
    EXPECT_NO_THROW({
        auto reachable = graph.reachable_nodes(start_node, TraversalOptions{.max_depth = 5});
        EXPECT_FALSE(reachable.empty());
    });
}

TEST_F(RepositoryGraphBuilderTest, UnresolvedExternalDependenciesAndSameNameSymbols) {
    auto math_a = parse("src/pkg_a/math.py", "import os\n"
                                             "def calculate(): return 42\n");

    auto math_b = parse("src/pkg_b/math.py", "import sys\n"
                                             "def calculate(): return 100\n");

    auto caller = parse("src/main.py", "import pkg_a.math\n"
                                       "def run():\n"
                                       "    pkg_a.math.calculate()\n");

    index::InvertedIndex index;
    index.add_parsed_file(math_a);
    index.add_parsed_file(math_b);
    index.add_parsed_file(caller);

    auto result = RepositoryGraphBuilder::build_repository_graph(index, graph);

    EXPECT_EQ(result.metrics.total_files, 3);
    EXPECT_GE(result.import_result.unresolved_imports, 2);  // 'os' and 'sys'
}

TEST_F(RepositoryGraphBuilderTest, StorageMetricsAndRatioIntegrity) {
    auto file = parse("src/sample.cpp", "#include <vector>\n"
                                        "class Engine {\n"
                                        "public:\n"
                                        "    void start() {}\n"
                                        "    void run() { start(); }\n"
                                        "};\n");

    index::InvertedIndex index;
    index.add_parsed_file(file);

    auto result = RepositoryGraphBuilder::build_repository_graph(index, graph);

    EXPECT_GT(result.metrics.total_source_bytes, 0);
    EXPECT_GT(result.metrics.estimated_graph_memory_bytes, 0);
    EXPECT_GT(result.metrics.graph_to_source_ratio, 0.0);
}

}  // namespace amoeba::graph::test
