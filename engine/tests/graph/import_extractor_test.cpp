#include "amoeba/graph/import_extractor.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"

#include <gtest/gtest.h>
#include <iostream>

namespace amoeba::graph::test {

class ImportExtractorTest : public ::testing::Test {
protected:
    parser::SourceParser parser;
    RelationshipGraph graph;

    parser::ParsedFile parse(const std::filesystem::path& file_path, std::string_view code) {
        auto lang = parser::SourceParser::detect_language(file_path);
        return parser.parse_source(code, lang, file_path);
    }
};

TEST_F(ImportExtractorTest, CppIncludeExtractionAndResolution) {
    auto header = parse("src/utils.hpp", "#pragma once\n"
                                         "inline int add(int a, int b) { return a + b; }\n");

    auto main_cpp = parse("src/main.cpp", "#include \"utils.hpp\"\n"
                                          "#include <vector>\n"
                                          "#include <iostream>\n"
                                          "int main() { return add(1, 2); }\n");

    index::InvertedIndex index;
    index.add_parsed_file(header);
    index.add_parsed_file(main_cpp);

    auto result = ImportExtractor::extract_and_populate(index, graph);

    EXPECT_EQ(result.total_imports_found, 3);
    EXPECT_EQ(result.resolved_imports, 1);    // "utils.hpp" resolved
    EXPECT_EQ(result.unresolved_imports, 2);  // <vector>, <iostream> unresolved
    EXPECT_EQ(graph.relationship_count(), 1);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Includes), 1);
}

TEST_F(ImportExtractorTest, TypeScriptRelativeImports) {
    auto types = parse("src/types/user.ts", "export interface User {\n"
                                            "    id: string;\n"
                                            "    name: string;\n"
                                            "}\n");

    auto service = parse("src/services/user_service.ts",
                         "import { User } from '../types/user';\n"
                         "import React from 'react';\n"
                         "export class UserService {\n"
                         "    getUser(): User { return { id: '1', name: 'Alice' }; }\n"
                         "}\n");

    index::InvertedIndex index;
    index.add_parsed_file(types);
    index.add_parsed_file(service);

    auto result = ImportExtractor::extract_and_populate(index, graph);

    EXPECT_EQ(result.total_imports_found, 2);
    EXPECT_EQ(result.resolved_imports, 1);    // '../types/user' resolved to 'src/types/user.ts'
    EXPECT_EQ(result.unresolved_imports, 1);  // 'react' unresolved external
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Imports), 1);
}

TEST_F(ImportExtractorTest, PythonImports) {
    auto math_mod = parse("pkg/math_utils.py", "def compute_square(x):\n"
                                               "    return x * x\n");

    auto app_mod = parse("pkg/app.py", "import os\n"
                                       "import sys\n"
                                       "from math_utils import compute_square\n"
                                       "def main():\n"
                                       "    print(compute_square(5))\n");

    index::InvertedIndex index;
    index.add_parsed_file(math_mod);
    index.add_parsed_file(app_mod);

    auto result = ImportExtractor::extract_and_populate(index, graph);

    EXPECT_EQ(result.total_imports_found, 3);
    EXPECT_EQ(result.resolved_imports, 1);    // 'math_utils' resolved
    EXPECT_EQ(result.unresolved_imports, 2);  // 'os', 'sys' unresolved standard library
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Imports), 1);
}

TEST_F(ImportExtractorTest, JavaPackageImports) {
    auto model = parse("com/example/model/Account.java", "package com.example.model;\n"
                                                         "public class Account {\n"
                                                         "    private String id;\n"
                                                         "}\n");

    auto controller =
        parse("com/example/controller/AccountController.java", "package com.example.controller;\n"
                                                               "import java.util.List;\n"
                                                               "import com.example.model.Account;\n"
                                                               "public class AccountController {\n"
                                                               "    private Account account;\n"
                                                               "}\n");

    index::InvertedIndex index;
    index.add_parsed_file(model);
    index.add_parsed_file(controller);

    auto result = ImportExtractor::extract_and_populate(index, graph);

    EXPECT_EQ(result.total_imports_found, 2);
    EXPECT_EQ(result.resolved_imports, 1);    // Account resolved
    EXPECT_EQ(result.unresolved_imports, 1);  // java.util.List unresolved
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Imports), 1);
}

TEST_F(ImportExtractorTest, GoPackageImports) {
    auto logger = parse("pkg/logger/logger.go", "package logger\n"
                                                "func Log(msg string) {}\n");

    auto server = parse("cmd/server/main.go", "package main\n"
                                              "import (\n"
                                              "    \"fmt\"\n"
                                              "    \"pkg/logger\"\n"
                                              ")\n"
                                              "func main() { logger.Log(\"start\") }\n");

    index::InvertedIndex index;
    index.add_parsed_file(logger);
    index.add_parsed_file(server);

    auto result = ImportExtractor::extract_and_populate(index, graph);

    EXPECT_GE(result.total_imports_found, 1);
    EXPECT_GE(result.resolved_imports, 1);
}

TEST_F(ImportExtractorTest, MixedLanguageRepositoryFixtureAndMemoryBenchmark) {
    std::vector<parser::ParsedFile> files;

    // 1. C++
    files.push_back(parse("engine/core.hpp", "#pragma once\n"
                                             "class CoreEngine { public: void run(); };\n"));
    files.push_back(parse("engine/core.cpp", "#include \"core.hpp\"\n"
                                             "#include <memory>\n"
                                             "void CoreEngine::run() {}\n"));

    // 2. Python
    files.push_back(parse("scripts/helper.py", "def run_helper(): pass\n"));
    files.push_back(parse("scripts/pipeline.py", "import json\n"
                                                 "from helper import run_helper\n"
                                                 "run_helper()\n"));

    // 3. TypeScript / React
    files.push_back(parse("web/components/Button.tsx",
                          "export const Button = () => <button>Click</button>;\n"));
    files.push_back(parse("web/App.tsx", "import React from 'react';\n"
                                         "import { Button } from './components/Button';\n"
                                         "export const App = () => <div><Button /></div>;\n"));

    // 4. HTML / CSS
    files.push_back(parse("web/styles/global.css", "body { margin: 0; }\n"));
    files.push_back(parse("web/index.html",
                          "<!DOCTYPE html>\n"
                          "<html><head><link rel=\"stylesheet\" "
                          "href=\"styles/global.css\"></head><body></body></html>\n"));

    index::InvertedIndex index;
    for (const auto& file : files) {
        index.add_parsed_file(file);
    }

    auto result = ImportExtractor::extract_and_populate(index, graph);

    EXPECT_EQ(index.file_count(), 8);
    EXPECT_GT(result.total_imports_found, 0);
    EXPECT_GT(result.resolved_imports, 0);
    EXPECT_GT(result.unresolved_imports, 0);
    EXPECT_GT(graph.relationship_count(), 0);

    // Compute memory footprint estimate
    size_t est_edge_bytes = graph.relationship_count() * sizeof(Relationship);
    size_t est_node_bytes = graph.node_count() * sizeof(ElementId) * 2;
    size_t total_graph_est_bytes = est_edge_bytes + est_node_bytes + 256;

    std::cout << "\n[=== Phase 5.2 Import Graph Metrics ===]\n"
              << "  Total Files Indexed:       " << index.file_count() << "\n"
              << "  Total CodeElements:        " << index.element_count() << "\n"
              << "  Total Import Declarations: " << result.total_imports_found << "\n"
              << "  Resolved Relationships:    " << result.resolved_imports << "\n"
              << "  Unresolved Dependencies:   " << result.unresolved_imports << "\n"
              << "  Graph Edge Count:          " << graph.relationship_count() << "\n"
              << "  Graph Node Count:          " << graph.node_count() << "\n"
              << "  Est. Graph Memory (bytes): " << total_graph_est_bytes << " bytes\n"
              << "[======================================]\n";

    // Verify traversal across extracted imports
    auto app_nodes = graph.all_nodes();
    EXPECT_FALSE(app_nodes.empty());
}

TEST_F(ImportExtractorTest, HandlesMalformedSourceGracefully) {
    auto malformed = parse("src/broken.cpp", "#include \n"
                                             "#include \"\" \n"
                                             "import \n"
                                             "void foo( { \n");

    index::InvertedIndex index;
    index.add_parsed_file(malformed);

    EXPECT_NO_THROW({
        auto result = ImportExtractor::extract_and_populate(index, graph);
        EXPECT_EQ(result.resolved_imports, 0);
    });
}

}  // namespace amoeba::graph::test
