#include "amoeba/tools/tool_evidence_adapter.hpp"
#include "amoeba/evidence/evidence_sufficiency.hpp"

#include <gtest/gtest.h>

using namespace amoeba;
using namespace amoeba::tools;

TEST(ToolEvidenceAdapterTest, ConvertReadFileResultToEvidenceItem) {
    ToolResult result;
    result.status = ToolResultStatus::Success;
    result.tool_name = "read_file";
    result.summary = "File: src/auth_service.cpp [lines 1-20]";
    result.content = "1: class AuthService {\n2: public:\n3:   bool login();\n4: };";

    auto opt_item = ToolEvidenceAdapter::to_evidence_item(result);
    ASSERT_TRUE(opt_item.has_value());
    EXPECT_EQ(opt_item->file_path(), "src/auth_service.cpp");
    EXPECT_TRUE(opt_item->has_source_excerpt());
    EXPECT_EQ(opt_item->source_excerpt->start_line, 1u);
    EXPECT_EQ(opt_item->source_excerpt->end_line, 20u);
    EXPECT_EQ(opt_item->source_excerpt->text, result.content);
}

TEST(ToolEvidenceAdapterTest, ConvertFindSymbolResultToEvidenceItems) {
    ToolResult result;
    result.status = ToolResultStatus::Success;
    result.tool_name = "find_symbol";
    result.summary = "Found 2 matching symbol(s) for 'useCalendar'";
    result.content = "Symbol: useCalendar | Kind: Hook | Location: hooks/useCalendar.ts:1-50\n"
                     "Symbol: useCalendar | Kind: Function | Location: src/legacy/calendar.ts:10-30";

    auto items = ToolEvidenceAdapter::to_evidence_items(result);
    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0].primary_element().name, "useCalendar");
    EXPECT_EQ(items[0].primary_element().kind, parser::ElementKind::Hook);
    EXPECT_EQ(items[0].file_path(), "hooks/useCalendar.ts");

    EXPECT_EQ(items[1].primary_element().name, "useCalendar");
    EXPECT_EQ(items[1].primary_element().kind, parser::ElementKind::Function);
    EXPECT_EQ(items[1].file_path(), "src/legacy/calendar.ts");
}

TEST(ToolEvidenceAdapterTest, ConvertGetRelationshipsResultToEvidenceItems) {
    ToolResult result;
    result.status = ToolResultStatus::Success;
    result.tool_name = "get_relationships";
    result.summary = "Relationships for 'AuthService': 2 outgoing, 0 incoming";
    result.content = "Outgoing relationships:\n"
                     "- [Calls] AuthService (Class, src/auth.cpp) -> TokenValidator (Class, src/token.cpp)\n"
                     "- [Imports] AuthService (Class, src/auth.cpp) -> UserDb (Class, src/db.cpp)";

    auto items = ToolEvidenceAdapter::to_evidence_items(result);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].direct_relationships.size(), 2u);
    EXPECT_EQ(items[0].direct_relationships[0].kind, graph::RelationshipKind::Calls);
    EXPECT_EQ(items[0].direct_relationships[1].kind, graph::RelationshipKind::Imports);
}

TEST(ToolEvidenceAdapterTest, IntegrateIntoBundleAndCheckSufficiency) {
    evidence::EvidenceBundle bundle;
    bundle.query = "Where is the auth service?";

    EXPECT_TRUE(bundle.empty());

    ToolResult result;
    result.status = ToolResultStatus::Success;
    result.tool_name = "read_file";
    result.summary = "File: src/auth_service.cpp [lines 1-10]";
    result.content = "class AuthService {\n  void authenticate();\n};";

    bool added = ToolEvidenceAdapter::integrate_into_bundle(bundle, result);
    EXPECT_TRUE(added);
    EXPECT_EQ(bundle.size(), 1u);

    // Evaluate sufficiency with EvidenceSufficiencyChecker
    auto sufficiency = evidence::EvidenceSufficiencyChecker::check(bundle.query, bundle);
    // Bundle now has a valid evidence item with source excerpt
    EXPECT_FALSE(bundle.empty());
    EXPECT_EQ(bundle.items[0].file_path(), "src/auth_service.cpp");
}

TEST(ToolEvidenceAdapterTest, IgnoreFailedToolResults) {
    ToolResult failed_result;
    failed_result.status = ToolResultStatus::PermissionDenied;
    failed_result.tool_name = "read_file";
    failed_result.summary = "Access denied";

    auto opt_item = ToolEvidenceAdapter::to_evidence_item(failed_result);
    EXPECT_FALSE(opt_item.has_value());

    evidence::EvidenceBundle bundle;
    bool added = ToolEvidenceAdapter::integrate_into_bundle(bundle, failed_result);
    EXPECT_FALSE(added);
    EXPECT_TRUE(bundle.empty());
}
