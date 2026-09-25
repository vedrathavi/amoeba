#include "amoeba/graph/call_extractor.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"

#include <gtest/gtest.h>

namespace amoeba::graph::test {

class CallExtractorTest : public ::testing::Test {
protected:
    parser::SourceParser parser;
    RelationshipGraph graph;

    parser::ParsedFile parse(const std::filesystem::path& file_path, std::string_view code) {
        auto lang = parser::SourceParser::detect_language(file_path);
        return parser.parse_source(code, lang, file_path);
    }
};

TEST_F(CallExtractorTest, DirectFunctionCallInSameFile) {
    auto file =
        parse("src/auth.cpp", "bool authenticateUser(const std::string& token) { return true; }\n"
                              "bool loginUser(const std::string& token) {\n"
                              "    return authenticateUser(token);\n"
                              "}\n");

    index::InvertedIndex index;
    index.add_parsed_file(file);

    auto result = CallExtractor::extract_and_populate(index, graph);

    EXPECT_GE(result.total_call_candidates, 1);
    EXPECT_GE(result.resolved_calls, 1);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Calls), 1);
    EXPECT_EQ(graph.relationship_count(), 1);
}

TEST_F(CallExtractorTest, RecursiveAndMutuallyRecursiveCalls) {
    auto file = parse("src/recursion.cpp", "int factorial(int n) {\n"
                                           "    if (n <= 1) return 1;\n"
                                           "    return n * factorial(n - 1);\n"
                                           "}\n"
                                           "void ping(int n);\n"
                                           "void pong(int n) {\n"
                                           "    if (n > 0) ping(n - 1);\n"
                                           "}\n"
                                           "void ping(int n) {\n"
                                           "    if (n > 0) pong(n - 1);\n"
                                           "}\n");

    index::InvertedIndex index;
    index.add_parsed_file(file);

    auto result = CallExtractor::extract_and_populate(index, graph);

    EXPECT_GE(result.resolved_calls, 3);
    EXPECT_GE(graph.relationship_count(RelationshipKind::Calls), 3);
}

TEST_F(CallExtractorTest, ClassMethodCallScope) {
    auto file = parse("src/service.py", "class OrderService:\n"
                                        "    def validate(self):\n"
                                        "        pass\n"
                                        "    def process(self):\n"
                                        "        self.validate()\n");

    index::InvertedIndex index;
    index.add_parsed_file(file);

    auto result = CallExtractor::extract_and_populate(index, graph);

    EXPECT_GE(result.resolved_calls, 1);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Calls), 1);
}

TEST_F(CallExtractorTest, CrossFileCallViaIncludes) {
    auto header =
        parse("include/crypto.hpp", "#pragma once\n"
                                    "std::string hashPassword(const std::string& raw);\n");

    auto source = parse("src/user.cpp", "#include \"crypto.hpp\"\n"
                                        "void registerUser(const std::string& pwd) {\n"
                                        "    auto h = hashPassword(pwd);\n"
                                        "}\n");

    index::InvertedIndex index;
    index.add_parsed_file(header);
    index.add_parsed_file(source);

    auto result = CallExtractor::extract_and_populate(index, graph);

    EXPECT_GE(result.resolved_calls, 1);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Calls), 1);
}

TEST_F(CallExtractorTest, NamespaceQualifiedCallResolution) {
    auto file = parse("src/network.cpp", "namespace Net {\n"
                                         "    void sendPacket() {}\n"
                                         "    void broadcast() {\n"
                                         "        sendPacket();\n"
                                         "    }\n"
                                         "}\n"
                                         "namespace DB {\n"
                                         "    void executeQuery() {\n"
                                         "        Net::sendPacket();\n"
                                         "    }\n"
                                         "}\n");

    index::InvertedIndex index;
    index.add_parsed_file(file);

    auto result = CallExtractor::extract_and_populate(index, graph);

    EXPECT_GE(result.resolved_calls, 2);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Calls), 2);
}

TEST_F(CallExtractorTest, DisambiguatesSameNameInDifferentScopes) {
    auto file = parse("src/scopes.cpp", "namespace ModuleA {\n"
                                        "    void init() {}\n"
                                        "    void run() { init(); }\n"
                                        "}\n"
                                        "namespace ModuleB {\n"
                                        "    void init() {}\n"
                                        "    void run() { init(); }\n"
                                        "}\n");

    index::InvertedIndex index;
    index.add_parsed_file(file);

    auto result = CallExtractor::extract_and_populate(index, graph);

    EXPECT_GE(result.resolved_calls, 2);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Calls), 2);
}

TEST_F(CallExtractorTest, UnresolvedStdlibAndExternalCalls) {
    auto file = parse("src/main.cpp", "#include <iostream>\n"
                                      "int main() {\n"
                                      "    printf(\"Hello World\\n\");\n"
                                      "    return 0;\n"
                                      "}\n");

    index::InvertedIndex index;
    index.add_parsed_file(file);

    auto result = CallExtractor::extract_and_populate(index, graph);

    EXPECT_GE(result.unresolved_calls, 1);
    EXPECT_EQ(result.resolved_calls, 0);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Calls), 0);
}

TEST_F(CallExtractorTest, AmbiguousMultipleCandidatesPolicy) {
    auto file_a = parse("src/feature_a.cpp", "void helper() {}\n");

    auto file_b = parse("src/feature_b.cpp", "void helper() {}\n");

    auto caller_file = parse("src/main.cpp", "void run() {\n"
                                             "    helper();\n"
                                             "}\n");

    index::InvertedIndex index;
    index.add_parsed_file(file_a);
    index.add_parsed_file(file_b);
    index.add_parsed_file(caller_file);

    auto result = CallExtractor::extract_and_populate(index, graph);

    // With two distinct 'helper' candidates in unrelated un-imported files,
    // Amoeba must deterministically reject guessing and classify as ambiguous.
    EXPECT_GE(result.ambiguous_calls, 1);
    EXPECT_EQ(result.resolved_calls, 0);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::Calls), 0);
}

TEST_F(CallExtractorTest, JSXComponentReferences) {
    auto comp_file = parse("src/components/UserProfileCard.tsx",
                           "export function UserProfileCard() { return <div>User</div>; }\n");

    auto page_file = parse("src/pages/Dashboard.tsx",
                           "import { UserProfileCard } from '../components/UserProfileCard';\n"
                           "export function Dashboard() {\n"
                           "    return (\n"
                           "        <div>\n"
                           "            <UserProfileCard />\n"
                           "        </div>\n"
                           "    );\n"
                           "}\n");

    index::InvertedIndex index;
    index.add_parsed_file(comp_file);
    index.add_parsed_file(page_file);

    auto result = CallExtractor::extract_and_populate(index, graph);

    EXPECT_GE(result.resolved_references, 1);
    EXPECT_EQ(graph.relationship_count(RelationshipKind::References), 1);
}

TEST_F(CallExtractorTest, HandlesMalformedSourceGracefully) {
    auto malformed = parse("src/broken.cpp", "void test() { ()(*(; foo(;; } } }\n");

    index::InvertedIndex index;
    index.add_parsed_file(malformed);

    EXPECT_NO_THROW({
        auto result = CallExtractor::extract_and_populate(index, graph);
        EXPECT_GE(result.total_call_candidates, 0);
    });
}

}  // namespace amoeba::graph::test
