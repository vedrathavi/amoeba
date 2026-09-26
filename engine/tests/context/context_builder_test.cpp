#include "amoeba/context/context_builder.hpp"
#include "amoeba/context/context_package.hpp"
#include "amoeba/evidence/evidence_assembler.hpp"
#include "amoeba/evidence/evidence_bundle.hpp"
#include "amoeba/graph/relationship_evidence_resolver.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/code_element.hpp"
#include "amoeba/parser/parsed_file.hpp"
#include "amoeba/retrieval/primary_search_result.hpp"
#include "amoeba/retrieval/retrieval_unit.hpp"
#include "amoeba/retrieval/supporting_evidence_resolver.hpp"
#include "amoeba/source/source_excerpt.hpp"
#include "amoeba/source/source_snippet_reader.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

using namespace amoeba;
using namespace amoeba::context;
using namespace amoeba::evidence;
using namespace amoeba::retrieval;
using namespace amoeba::graph;
using namespace amoeba::parser;
using namespace amoeba::index;
using namespace amoeba::source;

class ContextBuilderTest : public ::testing::Test {
protected:
    std::filesystem::path test_dir_;

    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "amoeba_context_builder_test";
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    EvidenceItem create_dummy_item(std::string_view name, ElementKind kind,
                                   std::string_view file_path, uint32_t start_line = 10,
                                   uint32_t end_line = 20, double hybrid_score = 0.9) {
        EvidenceItem item;
        item.primary_result.unit.primary_element.name = std::string(name);
        item.primary_result.unit.primary_element.kind = kind;
        item.primary_result.unit.primary_element.location =
            SourceRange{.start = {.line = start_line, .column = 1, .byte_offset = 0},
                        .end = {.line = end_line, .column = 1, .byte_offset = 100}};
        item.primary_result.unit.file_path = std::filesystem::path(file_path);
        item.primary_result.unit.language = "cpp";
        item.primary_result.hybrid_score = hybrid_score;

        SourceExcerpt exc;
        exc.file_path = item.primary_result.unit.file_path;
        exc.start_line = start_line;
        exc.end_line = end_line;
        exc.text = "void " + std::string(name) + "() {\n    // Implementation\n}\n";
        item.source_excerpt = exc;

        return item;
    }
};

// A. Empty EvidenceBundle
TEST_F(ContextBuilderTest, EmptyEvidenceBundleProducesCleanPackage) {
    EvidenceBundle bundle;
    bundle.query = "find nothing";

    ContextBuilder builder;
    auto pkg = builder.build(bundle);

    EXPECT_EQ(pkg.query, "find nothing");
    EXPECT_TRUE(pkg.empty());
    EXPECT_EQ(pkg.size(), 0u);
    EXPECT_EQ(pkg.total_available_items, 0u);
    EXPECT_EQ(pkg.selected_item_count, 0u);
    EXPECT_FALSE(pkg.truncated);
    EXPECT_NE(pkg.rendered_markdown.find("No relevant code elements were retrieved"),
              std::string::npos);
}

// B. One primary item
TEST_F(ContextBuilderTest, SinglePrimaryItemRenderedCorrectly) {
    EvidenceBundle bundle;
    bundle.query = "auth handler";
    bundle.items.push_back(
        create_dummy_item("authenticateUser", ElementKind::Function, "src/auth.cpp", 10, 12));

    ContextBuilder builder;
    auto pkg = builder.build(bundle);

    EXPECT_EQ(pkg.query, "auth handler");
    ASSERT_EQ(pkg.size(), 1u);
    EXPECT_EQ(pkg.total_available_items, 1u);
    EXPECT_EQ(pkg.selected_item_count, 1u);
    EXPECT_FALSE(pkg.truncated);

    EXPECT_NE(pkg.rendered_markdown.find("## Result 1: `authenticateUser`"), std::string::npos);
    EXPECT_NE(pkg.rendered_markdown.find("- **Kind**: Function"), std::string::npos);
    EXPECT_NE(pkg.rendered_markdown.find("- **File**: src/auth.cpp"), std::string::npos);
    EXPECT_NE(pkg.rendered_markdown.find("### Source Excerpt (Lines 10-12):"), std::string::npos);
    EXPECT_NE(pkg.rendered_markdown.find("void authenticateUser()"), std::string::npos);
}

// C, D, E, F. Multiple primary items and max_primary_items configuration (1, 3, 5)
TEST_F(ContextBuilderTest, MaxPrimaryItemsEnforcementAndRankOrder) {
    EvidenceBundle bundle;
    bundle.query = "operations";
    bundle.items.push_back(
        create_dummy_item("op1", ElementKind::Function, "src/op1.cpp", 1, 5, 0.99));
    bundle.items.push_back(
        create_dummy_item("op2", ElementKind::Function, "src/op2.cpp", 1, 5, 0.88));
    bundle.items.push_back(
        create_dummy_item("op3", ElementKind::Function, "src/op3.cpp", 1, 5, 0.77));
    bundle.items.push_back(
        create_dummy_item("op4", ElementKind::Function, "src/op4.cpp", 1, 5, 0.66));
    bundle.items.push_back(
        create_dummy_item("op5", ElementKind::Function, "src/op5.cpp", 1, 5, 0.55));

    // max_primary_items = 1
    {
        ContextBuilderOptions opts;
        opts.max_primary_items = 1;
        ContextBuilder b(opts);
        auto pkg = b.build(bundle);
        EXPECT_EQ(pkg.size(), 1u);
        EXPECT_EQ(pkg.selected_items[0].primary_element().name, "op1");
        EXPECT_TRUE(pkg.truncated);
    }

    // max_primary_items = 3 (default)
    {
        ContextBuilderOptions opts;
        opts.max_primary_items = 3;
        ContextBuilder b(opts);
        auto pkg = b.build(bundle);
        EXPECT_EQ(pkg.size(), 3u);
        EXPECT_EQ(pkg.selected_items[0].primary_element().name, "op1");
        EXPECT_EQ(pkg.selected_items[1].primary_element().name, "op2");
        EXPECT_EQ(pkg.selected_items[2].primary_element().name, "op3");
        EXPECT_TRUE(pkg.truncated);
    }

    // max_primary_items = 5
    {
        ContextBuilderOptions opts;
        opts.max_primary_items = 5;
        ContextBuilder b(opts);
        auto pkg = b.build(bundle);
        EXPECT_EQ(pkg.size(), 5u);
        EXPECT_FALSE(pkg.truncated);
    }
}

// G. Source rendering and max line limits
TEST_F(ContextBuilderTest, SourceRenderingAndLineLimits) {
    EvidenceBundle bundle;
    bundle.query = "long function";

    auto item = create_dummy_item("longFunc", ElementKind::Function, "src/long.cpp", 1, 100);
    std::string long_src;
    for (int i = 1; i <= 20; ++i) {
        long_src += "    line_" + std::to_string(i) + "();\n";
    }
    item.source_excerpt->text = long_src;
    bundle.items.push_back(item);

    ContextBuilderOptions opts;
    opts.max_source_lines_per_item = 5;
    ContextBuilder b(opts);
    auto pkg = b.build(bundle);

    EXPECT_NE(pkg.rendered_markdown.find("line_1();"), std::string::npos);
    EXPECT_NE(pkg.rendered_markdown.find("line_5();"), std::string::npos);
    EXPECT_EQ(pkg.rendered_markdown.find("line_6();"), std::string::npos);
    EXPECT_NE(pkg.rendered_markdown.find("[truncated 15 remaining lines]"), std::string::npos);
}

// H. Supporting AST rendering
TEST_F(ContextBuilderTest, SupportingASTRendering) {
    EvidenceBundle bundle;
    bundle.query = "class with methods";

    auto item = create_dummy_item("Controller", ElementKind::Class, "src/ctrl.cpp", 1, 50);
    CodeElement m1{.kind = ElementKind::Method,
                   .name = "handleRequest",
                   .location = SourceRange{.start = {.line = 10, .column = 5},
                                           .end = {.line = 20, .column = 5}}};
    CodeElement c1{.kind = ElementKind::Call,
                   .name = "validate",
                   .location = SourceRange{.start = {.line = 12, .column = 9},
                                           .end = {.line = 12, .column = 17}}};
    item.primary_result.unit.supporting_elements = {m1, c1};
    bundle.items.push_back(item);

    ContextBuilder builder;
    auto pkg = builder.build(bundle);

    EXPECT_NE(pkg.rendered_markdown.find("### Supporting AST Elements:"), std::string::npos);
    EXPECT_NE(pkg.rendered_markdown.find("- Method `handleRequest` (L10:C5 - L20:C5)"),
              std::string::npos);
    EXPECT_NE(pkg.rendered_markdown.find("- Call `validate` (L12:C9 - L12:C17)"),
              std::string::npos);
}

// I, J, K. Relationship rendering, directions, multiple kinds
TEST_F(ContextBuilderTest, RelationshipRenderingDirectionsAndKinds) {
    EvidenceBundle bundle;
    bundle.query = "worker flow";

    auto item = create_dummy_item("Worker", ElementKind::Class, "src/worker.cpp", 1, 50);

    RelationshipEvidence rel_calls{
        .primary_element_id = 1,
        .related_element_id = 2,
        .kind = RelationshipKind::Calls,
        .direction = RelationshipDirection::Outgoing,
        .related_name = "doJob",
        .related_kind = ElementKind::Function,
        .related_file_path = "src/job.cpp",
        .related_location = SourceRange{.start = {.line = 5, .column = 1}},
    };

    RelationshipEvidence rel_called_by{
        .primary_element_id = 1,
        .related_element_id = 3,
        .kind = RelationshipKind::Calls,
        .direction = RelationshipDirection::Incoming,
        .related_name = "startServer",
        .related_kind = ElementKind::Function,
        .related_file_path = "src/server.cpp",
        .related_location = SourceRange{.start = {.line = 15, .column = 1}},
    };

    RelationshipEvidence rel_implements{
        .primary_element_id = 1,
        .related_element_id = 4,
        .kind = RelationshipKind::Implements,
        .direction = RelationshipDirection::Outgoing,
        .related_name = "IService",
        .related_kind = ElementKind::Interface,
        .related_file_path = "src/iservice.hpp",
        .related_location = SourceRange{.start = {.line = 1, .column = 1}},
    };

    item.direct_relationships = {rel_calls, rel_called_by, rel_implements};
    bundle.items.push_back(item);

    ContextBuilder builder;
    auto pkg = builder.build(bundle);

    EXPECT_NE(pkg.rendered_markdown.find("### Direct Relationships:"), std::string::npos);
    EXPECT_NE(pkg.rendered_markdown.find("Calls → `doJob` (src/job.cpp:L5)"), std::string::npos);
    EXPECT_NE(pkg.rendered_markdown.find("Called By ← `startServer` (src/server.cpp:L15)"),
              std::string::npos);
    EXPECT_NE(pkg.rendered_markdown.find("Implements → `IService` (src/iservice.hpp:L1)"),
              std::string::npos);
}

// L, M, N. Missing source, missing relationships, missing supporting evidence
TEST_F(ContextBuilderTest, MissingPartialEvidenceHandledGracefully) {
    EvidenceBundle bundle;
    bundle.query = "bare item";

    EvidenceItem item;
    item.primary_result.unit.primary_element.name = "BareSymbol";
    item.primary_result.unit.primary_element.kind = ElementKind::Function;
    item.primary_result.unit.file_path = "src/bare.cpp";
    item.source_excerpt = std::nullopt;  // Missing source
    item.direct_relationships.clear();   // Missing relationships
    item.primary_result.unit.supporting_elements.clear();

    bundle.items.push_back(item);

    ContextBuilder builder;
    auto pkg = builder.build(bundle);

    EXPECT_NE(pkg.rendered_markdown.find("## Result 1: `BareSymbol`"), std::string::npos);
    EXPECT_NE(pkg.rendered_markdown.find("*[Source code unavailable]*"), std::string::npos);
    EXPECT_EQ(pkg.rendered_markdown.find("### Supporting AST Elements:"), std::string::npos);
    EXPECT_EQ(pkg.rendered_markdown.find("### Direct Relationships:"), std::string::npos);
}

// O, P, Q, U. Character budget and UTF-8-safe truncation
TEST_F(ContextBuilderTest, CharacterBudgetAndUTF8SafeTruncation) {
    EvidenceBundle bundle;
    bundle.query = "budget test";

    EvidenceItem item1 =
        create_dummy_item("item1_large", ElementKind::Function, "src/item1.cpp", 1, 100);
    // Add multibyte UTF-8 characters (e.g., Euro sign € (3 bytes: E2 82 AC) and emoji 🚀 (4 bytes:
    // F0 9F 9A 80))
    if (item1.source_excerpt.has_value()) {
        item1.source_excerpt->text =
            "// Special UTF-8: €€€ 🚀🚀🚀\nvoid item1_large() {\n    return 42;\n}\n";
    }
    bundle.items.push_back(item1);
    bundle.items.push_back(
        create_dummy_item("item2_large", ElementKind::Function, "src/item2.cpp", 1, 100));

    // Test 1: Very tight budget (100 characters) where even item 1 must be truncated with UTF-8
    // safety
    {
        ContextBuilderOptions tight_opts;
        tight_opts.max_character_budget = 100;
        ContextBuilder b(tight_opts);
        auto pkg = b.build(bundle);

        EXPECT_TRUE(pkg.truncated);
        EXPECT_LE(pkg.used_characters, 100u);
        EXPECT_LE(pkg.rendered_markdown.size(), 100u);
        EXPECT_EQ(pkg.selected_item_count, 1u);
        EXPECT_NE(pkg.rendered_markdown.find("... [Context truncated to fit budget]"),
                  std::string::npos);
    }

    // Test 2: Moderate budget (250 characters) where item 1 fits completely and item 2 is omitted
    {
        ContextBuilderOptions mod_opts;
        mod_opts.max_character_budget = 250;
        ContextBuilder b(mod_opts);
        auto pkg = b.build(bundle);

        EXPECT_TRUE(pkg.truncated);
        EXPECT_LE(pkg.used_characters, 250u);
        EXPECT_EQ(pkg.selected_item_count, 1u);
        EXPECT_EQ(pkg.selected_items[0].primary_element().name, "item1_large");
    }
}

// R. Deterministic repeated builds
TEST_F(ContextBuilderTest, DeterministicRepeatedBuilds) {
    EvidenceBundle bundle;
    bundle.query = "deterministic test";
    bundle.items.push_back(create_dummy_item("funcA", ElementKind::Function, "src/a.cpp"));
    bundle.items.push_back(create_dummy_item("funcB", ElementKind::Function, "src/b.cpp"));

    ContextBuilder builder;
    auto pkg1 = builder.build(bundle);
    auto pkg2 = builder.build(bundle);

    EXPECT_EQ(pkg1, pkg2);
    EXPECT_EQ(pkg1.rendered_markdown, pkg2.rendered_markdown);
}

// S, T, V, W. Structured ContextPackage data matches rendered output, immutability, ranking
// preservation
TEST_F(ContextBuilderTest, InvariantsAndNoMutation) {
    EvidenceBundle bundle;
    bundle.query = "invariants test";
    bundle.items.push_back(
        create_dummy_item("highRank", ElementKind::Function, "src/high.cpp", 1, 10, 0.99));
    bundle.items.push_back(
        create_dummy_item("lowRank", ElementKind::Function, "src/low.cpp", 1, 10, 0.50));

    const EvidenceBundle original_copy = bundle;

    ContextBuilder builder;
    auto pkg = builder.build(bundle);

    // T. Bundle not mutated
    EXPECT_EQ(bundle, original_copy);

    // V. Rank order preserved: highRank before lowRank
    ASSERT_EQ(pkg.selected_items.size(), 2u);
    EXPECT_EQ(pkg.selected_items[0].primary_element().name, "highRank");
    EXPECT_EQ(pkg.selected_items[1].primary_element().name, "lowRank");

    // S. Structured data matches markdown
    EXPECT_NE(pkg.rendered_markdown.find("Result 1: `highRank`"), std::string::npos);
    EXPECT_NE(pkg.rendered_markdown.find("Result 2: `lowRank`"), std::string::npos);
}

// X. Full Pipeline Integration Test
TEST_F(ContextBuilderTest, FullPipelineEndToEndIntegration) {
    // 1. Create source file
    auto file_path = test_dir_ / "calculator.cpp";
    std::ofstream ofs(file_path, std::ios::binary);
    ofs << "// Calculator module\n"
        << "class Calculator {\n"
        << "public:\n"
        << "    int add(int a, int b) {\n"
        << "        logOperation();\n"
        << "        return a + b;\n"
        << "    }\n"
        << "};\n"
        << "void logOperation() {}\n";
    ofs.close();

    // 2. Build index & graph
    InvertedIndex index;
    RelationshipGraph graph;

    ParsedFile pf{
        .file_path = file_path,
        .language = "cpp",
        .success = true,
        .has_syntax_errors = false,
        .elements = {
            CodeElement{
                .kind = ElementKind::Class,
                .name = "Calculator",
                .location = SourceRange{.start = {.line = 2, .column = 1, .byte_offset = 21},
                                        .end = {.line = 8, .column = 2, .byte_offset = 120}},
            },
            CodeElement{
                .kind = ElementKind::Method,
                .name = "add",
                .location = SourceRange{.start = {.line = 4, .column = 5, .byte_offset = 51},
                                        .end = {.line = 7, .column = 6, .byte_offset = 117}},
                .parent_context = "Calculator",
            },
            CodeElement{
                .kind = ElementKind::Call,
                .name = "logOperation",
                .location = SourceRange{.start = {.line = 5, .column = 9, .byte_offset = 83},
                                        .end = {.line = 5, .column = 23, .byte_offset = 97}},
                .parent_context = "Calculator",
            },
            CodeElement{
                .kind = ElementKind::Function,
                .name = "logOperation",
                .location = SourceRange{.start = {.line = 9, .column = 1, .byte_offset = 122},
                                        .end = {.line = 9, .column = 24, .byte_offset = 145}},
            },
        }};
    index.add_parsed_file(pf);

    auto calc_id = 0u;
    auto log_id = 3u;
    graph.add_relationship(calc_id, log_id, RelationshipKind::Calls);

    // 3. Resolve retrieval unit
    auto units = SupportingEvidenceResolver::resolve_units(pf);
    ASSERT_EQ(units.size(), 3u);  // Calculator (Class), add (Method), logOperation (Function)

    PrimarySearchResult search_result;
    search_result.unit = units.front();  // Calculator unit
    search_result.hybrid_score = 0.92;
    search_result.provenance = RetrievalProvenance::HybridBoth;

    // 4. Assemble Evidence
    RelationshipEvidenceResolver rel_resolver(graph, index);
    EvidenceAssembler assembler(rel_resolver);

    EvidenceBundle bundle = assembler.assemble("calculator add", {search_result});
    ASSERT_EQ(bundle.size(), 1u);
    EXPECT_TRUE(bundle.items.front().has_source_excerpt());
    EXPECT_EQ(bundle.items.front().direct_relationships.size(), 1u);

    // 5. Build Context
    ContextBuilder context_builder;
    ContextPackage pkg = context_builder.build(bundle);

    EXPECT_EQ(pkg.query, "calculator add");
    ASSERT_EQ(pkg.selected_item_count, 1u);
    EXPECT_FALSE(pkg.truncated);

    // Verify rendered context details
    EXPECT_NE(pkg.rendered_markdown.find("# Context: calculator add"), std::string::npos);
    EXPECT_NE(pkg.rendered_markdown.find("Result 1: `Calculator`"), std::string::npos);
    EXPECT_NE(pkg.rendered_markdown.find("class Calculator"), std::string::npos);
    EXPECT_NE(pkg.rendered_markdown.find("Calls → `logOperation`"), std::string::npos);
}

}  // namespace
