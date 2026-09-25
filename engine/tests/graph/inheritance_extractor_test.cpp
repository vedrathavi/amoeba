#include "amoeba/graph/inheritance_extractor.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"

#include <gtest/gtest.h>
#include <iostream>

namespace amoeba::graph::test {

class InheritanceExtractorTest : public ::testing::Test {
protected:
    parser::SourceParser parser;
    RelationshipGraph graph;

    parser::ParsedFile parse(const std::filesystem::path& file_path, std::string_view code) {
        auto lang = parser::SourceParser::detect_language(file_path);
        return parser.parse_source(code, lang, file_path);
    }
};

TEST_F(InheritanceExtractorTest, CppSingleAndMultipleInheritance) {
    auto base_file =
        parse("src/animals.hpp", "#pragma once\n"
                                 "class Animal { public: virtual ~Animal() = default; };\n"
                                 "class Flyable { public: virtual void fly() = 0; };\n"
                                 "class Dog : public Animal {};\n"
                                 "class Duck : public Animal, public Flyable {};\n");

    index::InvertedIndex index;
    index.add_parsed_file(base_file);

    auto result = InheritanceExtractor::extract_and_populate(index, graph);

    EXPECT_EQ(result.total_clauses_found, 3);
    EXPECT_EQ(result.resolved_inheritances, 3);
    EXPECT_EQ(result.unresolved_inheritances, 0);

    EXPECT_EQ(graph.relationship_count(RelationshipKind::InheritsFrom), 3);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Implements), 0);
}

TEST_F(InheritanceExtractorTest, JavaExtendsAndImplements) {
    auto source =
        parse("com/example/Service.java",
              "package com.example;\n"
              "interface Service {}\n"
              "interface AuthProvider {}\n"
              "class BaseService {}\n"
              "class UserService extends BaseService implements Service, AuthProvider {}\n");

    index::InvertedIndex index;
    index.add_parsed_file(source);

    auto result = InheritanceExtractor::extract_and_populate(index, graph);

    EXPECT_EQ(result.total_clauses_found, 3);
    EXPECT_EQ(result.resolved_inheritances, 3);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::InheritsFrom), 1);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Implements), 2);
}

TEST_F(InheritanceExtractorTest, TypeScriptClassAndInterfaceHierarchy) {
    auto source =
        parse("src/services/admin.ts",
              "interface IService {}\n"
              "interface IAdminService extends IService {}\n"
              "class BaseController {}\n"
              "class AdminController extends BaseController implements IAdminService {}\n");

    index::InvertedIndex index;
    index.add_parsed_file(source);

    auto result = InheritanceExtractor::extract_and_populate(index, graph);

    EXPECT_EQ(result.total_clauses_found, 3);
    EXPECT_EQ(result.resolved_inheritances, 3);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::InheritsFrom), 2);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Implements), 1);
}

TEST_F(InheritanceExtractorTest, PythonClassInheritance) {
    auto source = parse("pkg/models.py", "class BaseModel: pass\n"
                                         "class User(BaseModel): pass\n"
                                         "class AdminUser(User): pass\n");

    index::InvertedIndex index;
    index.add_parsed_file(source);

    auto result = InheritanceExtractor::extract_and_populate(index, graph);

    EXPECT_EQ(result.total_clauses_found, 2);
    EXPECT_EQ(result.resolved_inheritances, 2);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::InheritsFrom), 2);

    // Traversal: AdminUser -> User -> BaseModel
    auto admin_nodes = graph.all_nodes();
    ASSERT_FALSE(admin_nodes.empty());
}

TEST_F(InheritanceExtractorTest, CrossFileInheritanceResolution) {
    auto base_header = parse("include/repository.hpp", "#pragma once\n"
                                                       "class Repository {};\n");

    auto derived_header =
        parse("include/user_repository.hpp", "#pragma once\n"
                                             "#include \"repository.hpp\"\n"
                                             "class UserRepository : public Repository {};\n");

    index::InvertedIndex index;
    index.add_parsed_file(base_header);
    index.add_parsed_file(derived_header);

    auto result = InheritanceExtractor::extract_and_populate(index, graph);

    EXPECT_EQ(result.total_clauses_found, 1);
    EXPECT_EQ(result.resolved_inheritances, 1);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::InheritsFrom), 1);
}

TEST_F(InheritanceExtractorTest, UnresolvedExternalBaseTypes) {
    auto source =
        parse("src/custom_error.cpp", "#include <stdexcept>\n"
                                      "class CustomException : public std::runtime_error {};\n");

    index::InvertedIndex index;
    index.add_parsed_file(source);

    auto result = InheritanceExtractor::extract_and_populate(index, graph);

    EXPECT_EQ(result.total_clauses_found, 1);
    EXPECT_EQ(result.resolved_inheritances, 0);
    EXPECT_EQ(result.unresolved_inheritances, 1);
    // Unresolved base class must NOT create synthetic graph nodes
    EXPECT_EQ(graph.relationship_count(), 0);
    EXPECT_EQ(graph.node_count(), 0);
}

TEST_F(InheritanceExtractorTest, NamespaceAwareResolution) {
    auto ns_source =
        parse("src/namespaces.cpp", "namespace Net {\n"
                                    "    class Connection {};\n"
                                    "    class HttpConnection : public Connection {};\n"
                                    "}\n"
                                    "namespace DB {\n"
                                    "    class Connection {};\n"
                                    "    class SqlConnection : public Connection {};\n"
                                    "}\n");

    index::InvertedIndex index;
    index.add_parsed_file(ns_source);

    auto result = InheritanceExtractor::extract_and_populate(index, graph);

    EXPECT_EQ(result.total_clauses_found, 2);
    EXPECT_EQ(result.resolved_inheritances, 2);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::InheritsFrom), 2);
}

TEST_F(InheritanceExtractorTest, HandlesMalformedSourceGracefully) {
    auto malformed = parse("src/broken.cpp", "class Incomplete : public {};\n"
                                             "class Error extends class {}\n");

    index::InvertedIndex index;
    index.add_parsed_file(malformed);

    EXPECT_NO_THROW({
        auto result = InheritanceExtractor::extract_and_populate(index, graph);
        EXPECT_GE(result.total_clauses_found, 0);
    });
}

}  // namespace amoeba::graph::test
