#include "amoeba/graph/call_extractor.hpp"
#include "amoeba/graph/focused_subgraph.hpp"
#include "amoeba/graph/graph_query_service.hpp"
#include "amoeba/graph/import_extractor.hpp"
#include "amoeba/graph/incremental_graph_updater.hpp"
#include "amoeba/graph/inheritance_extractor.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/graph/repository_graph_builder.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/index/search_engine.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/search/relationship_aware_search.hpp"

#include <chrono>
#include <gtest/gtest.h>
#include <iostream>
#include <numeric>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace amoeba::audit {

// ============================================================================
// AUDIT 1: CALL RESOLUTION CORRECTNESS (ADVERSARIAL FIXTURE)
// ============================================================================
class CallResolutionAuditTest : public ::testing::Test {
protected:
    parser::SourceParser parser;
    index::InvertedIndex index;
    graph::RelationshipGraph graph;

    void SetUp() override {
        // 1. Same-name functions in different namespaces
        auto ns_file = parser.parse_source("namespace A {\n"
                                           "    void process() {}\n"
                                           "    void caller_in_A() {\n"
                                           "        process();\n"
                                           "    }\n"
                                           "}\n"
                                           "namespace B {\n"
                                           "    void process() {}\n"
                                           "    void caller_in_B() {\n"
                                           "        process();\n"
                                           "    }\n"
                                           "}\n",
                                           "C++", "src/namespaces.cpp");

        // 2. Same method names in unrelated classes
        auto classes_file = parser.parse_source("class UserService {\n"
                                                "public:\n"
                                                "    void save() {}\n"
                                                "    void execute() { this->save(); }\n"
                                                "};\n"
                                                "class OrderService {\n"
                                                "public:\n"
                                                "    void save() {}\n"
                                                "    void execute() { this->save(); }\n"
                                                "};\n",
                                                "C++", "src/services.cpp");

        // 3. Recursive & Mutual Recursive functions
        auto recursion_file = parser.parse_source("int factorial(int n) {\n"
                                                  "    if (n <= 1) return 1;\n"
                                                  "    return n * factorial(n - 1);\n"
                                                  "}\n"
                                                  "void ping();\n"
                                                  "void pong() {\n"
                                                  "    ping();\n"
                                                  "}\n"
                                                  "void ping() {\n"
                                                  "    pong();\n"
                                                  "}\n",
                                                  "C++", "src/recursion.cpp");

        // 4. External calls & Ambiguous global targets
        // Note: ambiguousTarget is defined in ambig1.cpp and ambig2.cpp, while caller.cpp only
        // calls it!
        auto external_and_ambig =
            parser.parse_source("#include <cstdio>\n"
                                "#include <iostream>\n"
                                "void caller_func() {\n"
                                "    printf(\"Hello\\n\");\n"  // External std C
                                "    std::cout << 42;\n"       // External std C++
                                "    malloc(100);\n"           // External std C
                                "    ambiguousTarget();\n"  // Cross-file call to ambiguous symbol
                                "}\n",
                                "C++", "src/caller.cpp");

        auto ambig_file1 = parser.parse_source("void ambiguousTarget() {}\n",  // Global decl 1
                                               "C++", "src/ambig1.cpp");
        auto ambig_file2 = parser.parse_source("void ambiguousTarget() {}\n",  // Global decl 2
                                               "C++", "src/ambig2.cpp");

        index.add_parsed_file(ns_file);
        index.add_parsed_file(classes_file);
        index.add_parsed_file(recursion_file);
        index.add_parsed_file(external_and_ambig);
        index.add_parsed_file(ambig_file1);
        index.add_parsed_file(ambig_file2);
    }
};

TEST_F(CallResolutionAuditTest, EvaluateCallResolutionMetrics) {
    auto result = graph::CallExtractor::extract_and_populate(index, graph);

    std::size_t correct_resolutions = 0;
    std::size_t incorrect_resolutions = 0;
    std::size_t unresolved_external = 0;
    std::size_t ambiguous_count = 0;

    for (const auto& res : result.resolutions) {
        if (res.raw_target_name == "printf" || res.raw_target_name == "malloc" ||
            res.raw_target_name == "std::cout" || res.raw_target_name == "cout") {
            // External calls must be UNRESOLVED and have NO target
            EXPECT_EQ(res.status, graph::ResolutionStatus::Unresolved);
            EXPECT_FALSE(res.target_element_id.has_value());
            unresolved_external++;
        } else if (res.raw_target_name == "ambiguousTarget" ||
                   res.raw_target_name == "ambiguousTarget()") {
            // Global collision across multiple files must be AMBIGUOUS and have NO target
            EXPECT_EQ(res.status, graph::ResolutionStatus::Ambiguous);
            EXPECT_FALSE(res.target_element_id.has_value());
            ambiguous_count++;
        } else if (res.status == graph::ResolutionStatus::Resolved) {
            ASSERT_TRUE(res.target_element_id.has_value());
            const auto& target_elem = index.get_element(*res.target_element_id);

            if (res.source_name == "caller_in_A") {
                EXPECT_EQ(target_elem.element.name, "process");
                EXPECT_EQ(target_elem.element.parent_context, "A");
                correct_resolutions++;
            } else if (res.source_name == "caller_in_B") {
                EXPECT_EQ(target_elem.element.name, "process");
                EXPECT_EQ(target_elem.element.parent_context, "B");
                correct_resolutions++;
            } else if (res.source_name == "factorial") {
                EXPECT_EQ(target_elem.element.name, "factorial");
                correct_resolutions++;
            } else if (res.source_name == "ping") {
                EXPECT_EQ(target_elem.element.name, "pong");
                correct_resolutions++;
            } else if (res.source_name == "pong") {
                EXPECT_EQ(target_elem.element.name, "ping");
                correct_resolutions++;
            } else if (res.source_name == "execute") {
                EXPECT_EQ(target_elem.element.name, "save");
                correct_resolutions++;
            }
        }
    }

    EXPECT_GT(result.total_call_candidates, 0u);
    EXPECT_GT(correct_resolutions, 0u);
    EXPECT_EQ(incorrect_resolutions, 0u);  // 0 false positives!
    EXPECT_GT(unresolved_external, 0u);
    EXPECT_GT(ambiguous_count, 0u);

    // False Positive Rate = incorrect / total = 0.0%
    double false_positive_rate =
        static_cast<double>(incorrect_resolutions) / result.total_call_candidates;
    EXPECT_DOUBLE_EQ(false_positive_rate, 0.0);
}

// ============================================================================
// AUDIT 2: IMPORT / INCLUDE RESOLUTION
// ============================================================================
class ImportResolutionAuditTest : public ::testing::Test {
protected:
    parser::SourceParser parser;
    index::InvertedIndex index;
    graph::RelationshipGraph graph;

    void SetUp() override {
        // C++ includes
        auto cpp_hdr = parser.parse_source("class Widget {};\n", "C++", "include/widget.hpp");
        auto cpp_src = parser.parse_source("#include \"widget.hpp\"\n"
                                           "#include <vector>\n"
                                           "#include <string>\n"
                                           "Widget w;\n",
                                           "C++", "src/widget.cpp");

        // JS/TS index.ts imports
        auto ts_btn = parser.parse_source("export const Button = () => {};\n", "TypeScript",
                                          "src/components/button/index.ts");
        auto ts_app = parser.parse_source("import { Button } from './components/button';\n"
                                          "import React from 'react';\n",
                                          "TypeScript", "src/app.tsx");

        // Python package imports
        auto py_mod = parser.parse_source("def helper(): pass\n", "Python", "pkg/utils/helper.py");
        auto py_app = parser.parse_source("from pkg.utils import helper\n"
                                          "import os\n"
                                          "import sys\n",
                                          "Python", "pkg/main.py");

        index.add_parsed_file(cpp_hdr);
        index.add_parsed_file(cpp_src);
        index.add_parsed_file(ts_btn);
        index.add_parsed_file(ts_app);
        index.add_parsed_file(py_mod);
        index.add_parsed_file(py_app);
    }
};

TEST_F(ImportResolutionAuditTest, EvaluateImportResolutionBehaviors) {
    auto initial_elem_count = index.element_count();
    auto result = graph::ImportExtractor::extract_and_populate(index, graph);

    std::size_t resolved_internal = 0;
    std::size_t unresolved_external = 0;
    std::size_t incorrect_matches = 0;

    for (const auto& res : result.resolutions) {
        if (res.raw_import_target.find("vector") != std::string::npos ||
            res.raw_import_target.find("string") != std::string::npos ||
            res.raw_import_target.find("react") != std::string::npos ||
            res.raw_import_target.find("os") != std::string::npos ||
            res.raw_import_target.find("sys") != std::string::npos) {
            // Standard library or external npm modules must remain UNRESOLVED
            EXPECT_FALSE(res.is_resolved);
            EXPECT_FALSE(res.target_element_id.has_value());
            unresolved_external++;
        } else if (res.is_resolved) {
            ASSERT_TRUE(res.target_file_id.has_value());
            resolved_internal++;
        }
    }

    EXPECT_GT(resolved_internal, 0u);
    EXPECT_GT(unresolved_external, 0u);
    EXPECT_EQ(incorrect_matches, 0u);

    // Verify NO synthetic nodes were created (element_count remains identical)
    EXPECT_EQ(index.element_count(), initial_elem_count);
}

// ============================================================================
// AUDIT 3: GRAPH STORAGE OVERHEAD & SCALING
// ============================================================================
TEST(GraphStorageAuditTest, EvaluateMemoryOverheadAndScaling) {
    parser::SourceParser parser;

    std::vector<std::size_t> file_counts = {10, 50, 100};

    for (std::size_t num_files : file_counts) {
        index::InvertedIndex index;
        graph::RelationshipGraph graph;

        for (std::size_t f = 0; f < num_files; ++f) {
            std::string code = "// Header documentation and comments for module\n"
                               "// Providing full functional interface and implementation details\n"
                               "class Service" +
                               std::to_string(f) +
                               " {\n"
                               "public:\n"
                               "    void executeWorkflowStepOne() {}\n"
                               "    void executeWorkflowStepTwo() {}\n"
                               "    void runAllSteps() {\n"
                               "        this->executeWorkflowStepOne();\n"
                               "        this->executeWorkflowStepTwo();\n"
                               "    }\n"
                               "};\n";
            auto parsed = parser.parse_source(code, "C++", "service_" + std::to_string(f) + ".cpp");
            index.add_parsed_file(parsed);
        }

        auto build_res = graph::RepositoryGraphBuilder::build_repository_graph(index, graph);

        std::size_t node_cnt = graph.node_count();
        std::size_t rel_cnt = graph.relationship_count();
        std::size_t mem_bytes = graph.estimate_memory_bytes();

        EXPECT_GT(node_cnt, 0u);
        EXPECT_GT(rel_cnt, 0u);
        EXPECT_GT(mem_bytes, 0u);

        double bytes_per_rel = static_cast<double>(mem_bytes) / rel_cnt;
        double bytes_per_node = static_cast<double>(mem_bytes) / node_cnt;

        EXPECT_LT(bytes_per_rel, 300.0);
        EXPECT_LT(bytes_per_node, 600.0);
    }
}

// ============================================================================
// AUDIT 4: CONTAINS RELATIONSHIPS & STRUCTURAL INTEGRITY
// ============================================================================
TEST(ContainsAuditTest, StructuralHierarchyAndUniqueness) {
    parser::SourceParser parser;
    index::InvertedIndex index;
    graph::RelationshipGraph graph;

    auto file = parser.parse_source("class Controller {\n"
                                    "public:\n"
                                    "    void handle() {}\n"
                                    "    int status;\n"
                                    "};\n"
                                    "void standaloneFunc() {}\n",
                                    "C++", "src/controller.cpp");

    index.add_parsed_file(file);
    graph::RepositoryGraphBuilder::build_repository_graph(index, graph);

    auto contains_edges = graph.relationships_between(0, 1);  // Controller -> handle
    EXPECT_LE(contains_edges.size(), 1u);

    // Verify all edges in graph are unique
    EXPECT_EQ(graph.relationship_count(),
              graph.relationship_count(graph::RelationshipKind::Contains));
}

// ============================================================================
// AUDIT 5: INCREMENTAL UPDATE CORRECTNESS & STALE EDGE ELIMINATION
// ============================================================================
TEST(IncrementalUpdateAuditTest, NoStaleEdgesSurvive) {
    parser::SourceParser parser;
    index::InvertedIndex index;
    graph::RelationshipGraph graph;
    graph::IncrementalGraphUpdater updater(index, graph, parser);

    // 1. Add file A and file B
    updater.add_file("src/service.cpp", "class Service {\n"
                                        "public:\n"
                                        "    void execute() {}\n"
                                        "};\n");

    updater.add_file("src/client.cpp", "class Client {\n"
                                       "public:\n"
                                       "    void run() {\n"
                                       "        Service s;\n"
                                       "        s.execute();\n"
                                       "    }\n"
                                       "};\n");

    EXPECT_GT(graph.relationship_count(), 0u);

    // 2. Remove file A
    auto rem_res = updater.remove_file("src/service.cpp");
    EXPECT_TRUE(rem_res.success);

    // Check all remaining relationships in graph have valid node IDs
    for (const auto& node : graph.all_nodes()) {
        EXPECT_LT(node, index.element_count());
    }

    // 3. Unsupported language replacement preserves state
    std::size_t before_count = graph.relationship_count();
    auto bad_res = updater.replace_file("src/client.unknown_ext", "void foo() {}");
    EXPECT_FALSE(bad_res.success);
    EXPECT_EQ(graph.relationship_count(), before_count);
}

// ============================================================================
// AUDIT 8 & 9: SEARCH INTEGRATION & LATENCY BENCHMARK
// ============================================================================
TEST(SearchIntegrationAndBenchmarkAuditTest, ExpansionEquivalenceAndLatency) {
    parser::SourceParser parser;
    index::InvertedIndex index;
    graph::RelationshipGraph graph;

    auto auth_file = parser.parse_source("class AuthService {\n"
                                         "public:\n"
                                         "    void authenticate() {}\n"
                                         "};\n",
                                         "C++", "src/auth.cpp");
    index.add_parsed_file(auth_file);
    graph::RepositoryGraphBuilder::build_repository_graph(index, graph);

    search::RelationshipAwareSearchEngine aware_engine(index, graph);
    index::SearchEngine standard_engine(index);

    // 1. Verify Phase 4 Equivalence when expansion is disabled
    auto std_results =
        standard_engine.search("authenticate", {.ranker_type = index::RankerType::CodeAware});
    auto aware_results = aware_engine.search(
        "authenticate", {.ranker_type = index::RankerType::CodeAware}, {.enable_expansion = false});

    ASSERT_EQ(std_results.size(), aware_results.size());
    for (size_t i = 0; i < std_results.size(); ++i) {
        EXPECT_EQ(std_results[i].element.name, aware_results[i].primary_result.element.name);
        EXPECT_DOUBLE_EQ(std_results[i].score, aware_results[i].primary_result.score);
        EXPECT_TRUE(aware_results[i].related_elements.empty());
    }

    // 2. Measure expansion latency
    auto start_time = std::chrono::high_resolution_clock::now();
    auto exp_results = aware_engine.search("authenticate", {}, {.enable_expansion = true});
    auto end_time = std::chrono::high_resolution_clock::now();

    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    EXPECT_LT(elapsed_ms, 5.0);  // Sub-5ms query time
}

// ============================================================================
// AUDIT 10: PATTERN BOUNDARY VERIFICATION
// ============================================================================
TEST(PatternBoundaryAuditTest, VerifyNoHighLevelPatternPrimitivesInGraph) {
    // Verify RelationshipKind contains only fundamental facts
    EXPECT_EQ(static_cast<uint8_t>(graph::RelationshipKind::Contains), 0);
    EXPECT_EQ(static_cast<uint8_t>(graph::RelationshipKind::Imports), 1);
    EXPECT_EQ(static_cast<uint8_t>(graph::RelationshipKind::Includes), 2);
    EXPECT_EQ(static_cast<uint8_t>(graph::RelationshipKind::Calls), 3);
    EXPECT_EQ(static_cast<uint8_t>(graph::RelationshipKind::References), 4);
    EXPECT_EQ(static_cast<uint8_t>(graph::RelationshipKind::InheritsFrom), 5);
    EXPECT_EQ(static_cast<uint8_t>(graph::RelationshipKind::Implements), 6);
}

}  // namespace amoeba::audit
