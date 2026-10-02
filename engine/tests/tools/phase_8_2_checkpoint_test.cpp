#include "amoeba/tools/tool_registry.hpp"
#include "amoeba/tools/tool_evidence_adapter.hpp"
#include "amoeba/tools/repository_access_policy.hpp"
#include "amoeba/evidence/evidence_sufficiency.hpp"
#include "amoeba/scanner/repository_scanner.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/graph/relationship_evidence_resolver.hpp"
#include "amoeba/source/source_snippet_reader.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/semantic/deterministic_embedding_provider.hpp"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace amoeba;
using namespace amoeba::tools;

class Phase82CheckpointTest : public ::testing::Test {
protected:
    void SetUp() override {
        calendar_path = std::filesystem::current_path() / "demo_test_projects" / "calendar";
        if (std::filesystem::exists(calendar_path)) {
            scanner::RepositoryScanner scanner;
            auto scan_res = scanner.scan(calendar_path);

            parser::SourceParser parser;
            for (const auto& file_meta : scan_res.files) {
                auto pf = parser.parse_file(file_meta.path);
                if (pf.success) {
                    index.add_parsed_file(pf);
                    parsed_files.push_back(std::move(pf));
                }
            }

            resolver = std::make_unique<graph::RelationshipEvidenceResolver>(graph, index);
            pipeline = std::make_unique<retrieval::PrimaryRetrievalPipeline>(parsed_files, index, embedding_provider);
            has_calendar = true;
        }

        registry = std::make_unique<ToolRegistry>(ToolRegistry::create_default_registry());
    }

    std::filesystem::path calendar_path;
    std::vector<parser::ParsedFile> parsed_files;
    index::InvertedIndex index;
    graph::RelationshipGraph graph;
    std::unique_ptr<graph::RelationshipEvidenceResolver> resolver;
    semantic::DeterministicEmbeddingProvider embedding_provider{32};
    std::unique_ptr<retrieval::PrimaryRetrievalPipeline> pipeline;
    source::SourceSnippetReader snippet_reader;
    RepositoryAccessPolicy access_policy;
    std::unique_ptr<ToolRegistry> registry;
    bool has_calendar{false};

    [[nodiscard]] ToolExecutionContext make_context() const {
        ToolExecutionContext ctx;
        ctx.repository_root = calendar_path;
        ctx.retrieval_pipeline = pipeline.get();
        ctx.snippet_reader = &snippet_reader;
        ctx.index = &index;
        ctx.parsed_files = &parsed_files;
        ctx.relationship_graph = &graph;
        ctx.relationship_resolver = resolver.get();
        ctx.access_policy = &access_policy;
        return ctx;
    }
};

// =============================================================================
// 1. Tool Evidence Identity Preservation Tests
// =============================================================================

TEST_F(Phase82CheckpointTest, ReadFilePreservesFullIdentity) {
    if (!has_calendar) GTEST_SKIP();

    auto ctx = make_context();
    ToolRequest req{
        .tool_name = "read_file",
        .arguments = {{"path", "src/hooks/useCalendar.ts"}, {"start_line", "1"}, {"end_line", "25"}}
    };

    auto res = registry->execute(req, ctx);
    ASSERT_EQ(res.status, ToolResultStatus::Success);

    auto opt_item = ToolEvidenceAdapter::to_evidence_item(res);
    ASSERT_TRUE(opt_item.has_value());
    EXPECT_EQ(opt_item->file_path(), "src/hooks/useCalendar.ts");
    ASSERT_TRUE(opt_item->has_source_excerpt());
    EXPECT_EQ(opt_item->source_excerpt->start_line, 1u);
    EXPECT_EQ(opt_item->source_excerpt->end_line, 25u);
    EXPECT_NE(opt_item->source_excerpt->text.find("useCalendar"), std::string::npos);
}

TEST_F(Phase82CheckpointTest, FindSymbolPreservesFullIdentity) {
    if (!has_calendar) GTEST_SKIP();

    auto ctx = make_context();
    ToolRequest req{
        .tool_name = "find_symbol",
        .arguments = {{"name", "useCalendar"}}
    };

    auto res = registry->execute(req, ctx);
    ASSERT_EQ(res.status, ToolResultStatus::Success);

    auto items = ToolEvidenceAdapter::to_evidence_items(res);
    ASSERT_FALSE(items.empty());
    bool found_use_calendar_hook = false;
    for (const auto& item : items) {
        if (item.primary_element().name == "useCalendar" &&
            item.file_path().generic_string().find("useCalendar") != std::string::npos) {
            found_use_calendar_hook = true;
            EXPECT_TRUE(item.primary_element().kind == parser::ElementKind::Hook ||
                        item.primary_element().kind == parser::ElementKind::Function);
            break;
        }
    }
    EXPECT_TRUE(found_use_calendar_hook);
}

// =============================================================================
// 2. Real Repository Positive Cases
// =============================================================================

TEST_F(Phase82CheckpointTest, PositiveCase_CalendarNavigation) {
    if (!has_calendar) GTEST_SKIP();

    auto ctx = make_context();
    // In calendar app, calendar navigation is handled in useCalendar hook or Calendar navigation buttons
    ToolRequest req{
        .tool_name = "search_code",
        .arguments = {{"query", "calendar navigation"}, {"mode", "hybrid"}}
    };
    auto res = registry->execute(req, ctx);
    EXPECT_EQ(res.status, ToolResultStatus::Success);

    evidence::EvidenceBundle bundle;
    bundle.query = "How does the calendar navigate between months?";
    bool integrated = ToolEvidenceAdapter::integrate_into_bundle(bundle, res);
    EXPECT_TRUE(integrated);
    EXPECT_FALSE(bundle.empty());
}

TEST_F(Phase82CheckpointTest, PositiveCase_CalendarState) {
    if (!has_calendar) GTEST_SKIP();

    auto ctx = make_context();
    ToolRequest req{
        .tool_name = "find_symbol",
        .arguments = {{"name", "useCalendar"}}
    };
    auto res = registry->execute(req, ctx);
    EXPECT_EQ(res.status, ToolResultStatus::Success);

    evidence::EvidenceBundle bundle;
    bundle.query = "Where is calendar state managed?";
    ToolEvidenceAdapter::integrate_into_bundle(bundle, res);

    ASSERT_FALSE(bundle.empty());
    EXPECT_EQ(bundle.items[0].primary_element().name, "useCalendar");
}

TEST_F(Phase82CheckpointTest, PositiveCase_NotesPersistence) {
    if (!has_calendar) GTEST_SKIP();

    auto ctx = make_context();
    ToolRequest req{
        .tool_name = "search_code",
        .arguments = {{"query", "notes storage localStorage"}, {"mode", "hybrid"}}
    };
    auto res = registry->execute(req, ctx);
    EXPECT_EQ(res.status, ToolResultStatus::Success);

    evidence::EvidenceBundle bundle;
    bundle.query = "Where are notes saved?";
    ToolEvidenceAdapter::integrate_into_bundle(bundle, res);
    EXPECT_FALSE(bundle.empty());
}

// =============================================================================
// 3. Negative Cases & Grounding Invariants
// =============================================================================

TEST_F(Phase82CheckpointTest, NegativeCase_RBACRemainsInsufficient) {
    // RBAC does not exist in the calendar repo or Amoeba.
    // Ensure that arbitrary tool evidence from unrelated components cannot make an RBAC query sufficient.
    evidence::EvidenceBundle bundle;
    bundle.query = "Where is RBAC implemented?";

    // Simulate adding unrelated tool evidence (e.g. read_file on calendar header)
    ToolResult dummy_res;
    dummy_res.status = ToolResultStatus::Success;
    dummy_res.tool_name = "read_file";
    dummy_res.summary = "File: src/components/Header.tsx [lines 1-20]";
    dummy_res.content = "export const Header = () => <header>Calendar</header>;";

    ToolEvidenceAdapter::integrate_into_bundle(bundle, dummy_res);

    auto sufficiency = evidence::EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_FALSE(sufficiency.is_sufficient);
}

TEST_F(Phase82CheckpointTest, NegativeCase_JWTRemainsInsufficient) {
    evidence::EvidenceBundle bundle;
    bundle.query = "Where is JWT authentication implemented?";

    // Add unrelated code snippet
    ToolResult dummy_res;
    dummy_res.status = ToolResultStatus::Success;
    dummy_res.tool_name = "read_file";
    dummy_res.summary = "File: src/utils/format.ts [lines 1-10]";
    dummy_res.content = "export function formatDate(d: Date) { return d.toISOString(); }";

    ToolEvidenceAdapter::integrate_into_bundle(bundle, dummy_res);

    auto sufficiency = evidence::EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_FALSE(sufficiency.is_sufficient);
}

TEST_F(Phase82CheckpointTest, NegativeCase_AdversarialGraphQLResolver) {
    // Regression check for Phase 7.6.1: GraphQL resolver query must NOT be satisfied by SupportingEvidenceResolver
    evidence::EvidenceBundle bundle;
    bundle.query = "Where is the GraphQL resolver implemented?";

    ToolResult dummy_res;
    dummy_res.status = ToolResultStatus::Success;
    dummy_res.tool_name = "find_symbol";
    dummy_res.summary = "Found 1 matching symbol";
    dummy_res.content = "[1] Class `SupportingEvidenceResolver`\n    File: engine/src/retrieval/supporting_evidence_resolver.cpp:1:1";

    ToolEvidenceAdapter::integrate_into_bundle(bundle, dummy_res);

    auto sufficiency = evidence::EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_FALSE(sufficiency.is_sufficient);
}

// =============================================================================
// 4. Vocabulary-Gap Recovery Test
// =============================================================================

TEST_F(Phase82CheckpointTest, VocabularyGapRecovery) {
    if (!has_calendar) GTEST_SKIP();

    auto ctx = make_context();

    // Query: "date picker hook logic" (vocabulary gap vs "useCalendar")
    evidence::EvidenceBundle initial_bundle;
    initial_bundle.query = "date picker hook logic";

    // Initial retrieval without tools is empty / insufficient
    auto initial_suff = evidence::EvidenceSufficiencyChecker::check(initial_bundle.query, initial_bundle);
    EXPECT_FALSE(initial_suff.is_sufficient);

    // Targeted tool recovery: LLM requests find_symbol("useCalendar")
    ToolRequest req{
        .tool_name = "find_symbol",
        .arguments = {{"name", "useCalendar"}}
    };
    auto tool_res = registry->execute(req, ctx);
    EXPECT_EQ(tool_res.status, ToolResultStatus::Success);

    // Read exact source excerpt
    ToolRequest read_req{
        .tool_name = "read_file",
        .arguments = {{"path", "src/hooks/useCalendar.ts"}, {"start_line", "1"}, {"end_line", "35"}}
    };
    auto read_res = registry->execute(read_req, ctx);
    EXPECT_EQ(read_res.status, ToolResultStatus::Success);

    // Add recovered tool evidence to bundle
    ToolEvidenceAdapter::integrate_into_bundle(initial_bundle, tool_res);
    ToolEvidenceAdapter::integrate_into_bundle(initial_bundle, read_res);

    EXPECT_FALSE(initial_bundle.empty());
    bool has_use_calendar_file = false;
    for (const auto& it : initial_bundle.items) {
        if (it.file_path().generic_string().find("useCalendar.ts") != std::string::npos) {
            has_use_calendar_file = true;
            break;
        }
    }
    EXPECT_TRUE(has_use_calendar_file);
}

// =============================================================================
// 5. Ambiguous find_symbol Tests
// =============================================================================

TEST_F(Phase82CheckpointTest, AmbiguousFindSymbolDeterministic) {
    // Create multiple symbols with same name in a test index
    index::InvertedIndex test_index;
    parser::ParsedFile pf1;
    pf1.file_path = "src/a.cpp";
    parser::CodeElement e1;
    e1.name = "process";
    e1.kind = parser::ElementKind::Function;
    e1.location.start.line = 10;
    pf1.elements.push_back(e1);

    parser::ParsedFile pf2;
    pf2.file_path = "src/b.cpp";
    parser::CodeElement e2;
    e2.name = "process";
    e2.kind = parser::ElementKind::Method;
    e2.location.start.line = 20;
    pf2.elements.push_back(e2);

    std::vector<parser::ParsedFile> files = {pf1, pf2};
    test_index.add_parsed_file(pf1);
    test_index.add_parsed_file(pf2);

    ToolExecutionContext ctx;
    ctx.repository_root = "/fake/repo";
    ctx.index = &test_index;
    ctx.parsed_files = &files;

    ToolRequest req{
        .tool_name = "find_symbol",
        .arguments = {{"name", "process"}}
    };

    auto res = registry->execute(req, ctx);
    EXPECT_EQ(res.status, ToolResultStatus::Success);
    EXPECT_NE(res.content.find("Function `process`"), std::string::npos);
    EXPECT_NE(res.content.find("Method `process`"), std::string::npos);
}

// =============================================================================
// 6. Security & Adversarial Path Traversal Tests
// =============================================================================

TEST_F(Phase82CheckpointTest, ReadFileAdversarialSecurityAudit) {
    RepositoryAccessPolicy policy;
    const std::filesystem::path root{"/fake/repo"};

    // Traversal attempts
    EXPECT_EQ(policy.check_read(root, "../../secret.txt"), AccessDecision::PathOutsideRepository);
    EXPECT_EQ(policy.check_read(root, "../../../secret.txt"), AccessDecision::PathOutsideRepository);
    EXPECT_EQ(policy.check_read(root, "..\\..\\secret.txt"), AccessDecision::PathOutsideRepository);
    EXPECT_EQ(policy.check_read(root, "src/../../secret.txt"), AccessDecision::PathOutsideRepository);
    EXPECT_EQ(policy.check_read(root, "src/../.env"), AccessDecision::SensitiveFile);

    // Absolute paths
    EXPECT_EQ(policy.check_read(root, "C:/Users/Admin/passwords.txt"), AccessDecision::PathOutsideRepository);
    EXPECT_EQ(policy.check_read(root, "C:\\Windows\\System32\\cmd.exe"), AccessDecision::PathOutsideRepository);
    EXPECT_EQ(policy.check_read(root, "/etc/passwd"), AccessDecision::PathOutsideRepository);

    // Sensitive files
    EXPECT_EQ(policy.check_read(root, ".env"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(root, ".env.production"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(root, "id_rsa"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(root, "id_ed25519"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(root, "private.key"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(root, "certificate.pem"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(root, ".git/config"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(root, "credentials/aws.json"), AccessDecision::SensitiveFile);

    // Legitimate source files MUST be allowed
    EXPECT_EQ(policy.check_read(root, "src/auth_service.cpp"), AccessDecision::Allowed);
    EXPECT_EQ(policy.check_read(root, "src/token_manager.cpp"), AccessDecision::Allowed);
    EXPECT_EQ(policy.check_read(root, "src/configuration_service.cpp"), AccessDecision::Allowed);
}

// =============================================================================
// 7. Output Size Limits & Determinism
// =============================================================================

TEST_F(Phase82CheckpointTest, ReadFileRejectsExcessiveLineRange) {
    ToolRequest req{
        .tool_name = "read_file",
        .arguments = {{"path", "src/file.cpp"}, {"start_line", "1"}, {"end_line", "200"}}
    };

    ToolExecutionContext ctx;
    ctx.repository_root = "/fake/repo";

    auto res = registry->execute(req, ctx);
    EXPECT_EQ(res.status, ToolResultStatus::InvalidRequest);
    EXPECT_EQ(res.error_code, "LINE_RANGE_TOO_LARGE");
}

TEST_F(Phase82CheckpointTest, RepeatedExecutionsAreDeterministic) {
    if (!has_calendar) GTEST_SKIP();

    auto ctx = make_context();
    ToolRequest req{
        .tool_name = "find_symbol",
        .arguments = {{"name", "useCalendar"}}
    };

    auto res1 = registry->execute(req, ctx);
    for (int i = 0; i < 5; ++i) {
        auto res2 = registry->execute(req, ctx);
        EXPECT_EQ(res1.status, res2.status);
        EXPECT_EQ(res1.summary, res2.summary);
        EXPECT_EQ(res1.content, res2.content);
        EXPECT_EQ(res1.metadata, res2.metadata);
    }
}
