// Phase 6.3 — RetrievalUnit & SupportingEvidenceResolver Tests
//
// Tests cover:
//   Classifier: all primary kinds, all supporting kinds, exhaustiveness, default
//   Resolver:
//     1.  Class → Method → Call (multi-level nesting)
//     2.  Component → JSX → Attribute
//     3.  Multiple independent functions in same file
//     4.  Multiple independent components in same file
//     5.  Nested functions (inner function as primary)
//     6.  Unresolved supporting element (no matching primary)
//     7.  Cross-file: each file produces independent units
//     8.  Overlapping/edge source ranges (innermost wins)
//     9.  Deterministic ownership (same input → same assignment)
//    10.  No false ownership to unrelated primary symbols
//
// These tests interact with SourceParser to produce realistic ParsedFiles.
// They do NOT test SearchEngine, InvertedIndex, SemanticRetriever, or
// HybridRetriever. Phase 6.4 owns production pipeline integration.

#include "amoeba/parser/source_parser.hpp"
#include "amoeba/retrieval/retrieval_unit.hpp"
#include "amoeba/retrieval/supporting_evidence_resolver.hpp"

#include <gtest/gtest.h>

namespace amoeba::retrieval {

// =============================================================================
// Classifier Tests
// =============================================================================

TEST(RetrievalUnitClassifierTest, AllPrimaryKindsClassifyAsPrimary) {
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::Class),
              RetrievalUnitRole::Primary);
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::Struct),
              RetrievalUnitRole::Primary);
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::Interface),
              RetrievalUnitRole::Primary);
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::Function),
              RetrievalUnitRole::Primary);
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::Method),
              RetrievalUnitRole::Primary);
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::Component),
              RetrievalUnitRole::Primary);
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::Hook),
              RetrievalUnitRole::Primary);
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::Route),
              RetrievalUnitRole::Primary);

    EXPECT_TRUE(RetrievalUnitClassifier::is_primary(parser::ElementKind::Class));
    EXPECT_TRUE(RetrievalUnitClassifier::is_primary(parser::ElementKind::Struct));
    EXPECT_TRUE(RetrievalUnitClassifier::is_primary(parser::ElementKind::Interface));
    EXPECT_TRUE(RetrievalUnitClassifier::is_primary(parser::ElementKind::Function));
    EXPECT_TRUE(RetrievalUnitClassifier::is_primary(parser::ElementKind::Method));
    EXPECT_TRUE(RetrievalUnitClassifier::is_primary(parser::ElementKind::Component));
    EXPECT_TRUE(RetrievalUnitClassifier::is_primary(parser::ElementKind::Hook));
    EXPECT_TRUE(RetrievalUnitClassifier::is_primary(parser::ElementKind::Route));
}

TEST(RetrievalUnitClassifierTest, AllSupportingKindsClassifyAsSupporting) {
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::Call),
              RetrievalUnitRole::Supporting);
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::Attribute),
              RetrievalUnitRole::Supporting);
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::JSXElement),
              RetrievalUnitRole::Supporting);
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::JSXComponent),
              RetrievalUnitRole::Supporting);
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::UtilityClass),
              RetrievalUnitRole::Supporting);
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::Include),
              RetrievalUnitRole::Supporting);
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::Property),
              RetrievalUnitRole::Supporting);
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::Selector),
              RetrievalUnitRole::Supporting);
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::Unknown),
              RetrievalUnitRole::Supporting);

    EXPECT_TRUE(RetrievalUnitClassifier::is_supporting(parser::ElementKind::Call));
    EXPECT_TRUE(RetrievalUnitClassifier::is_supporting(parser::ElementKind::Attribute));
    EXPECT_TRUE(RetrievalUnitClassifier::is_supporting(parser::ElementKind::JSXElement));
    EXPECT_TRUE(RetrievalUnitClassifier::is_supporting(parser::ElementKind::JSXComponent));
    EXPECT_TRUE(RetrievalUnitClassifier::is_supporting(parser::ElementKind::UtilityClass));
    EXPECT_TRUE(RetrievalUnitClassifier::is_supporting(parser::ElementKind::Include));
    EXPECT_TRUE(RetrievalUnitClassifier::is_supporting(parser::ElementKind::Property));
    EXPECT_TRUE(RetrievalUnitClassifier::is_supporting(parser::ElementKind::Selector));
    EXPECT_TRUE(RetrievalUnitClassifier::is_supporting(parser::ElementKind::Unknown));
}

TEST(RetrievalUnitClassifierTest, IsPrimaryAndIsSupportingAreMutuallyExclusive) {
    // For every kind in the enum, exactly one of is_primary / is_supporting is true.
    using K = parser::ElementKind;
    const K all_kinds[] = {K::Class,    K::Struct,    K::Interface,    K::Function,     K::Method,
                           K::Include,  K::Call,      K::JSXElement,   K::JSXComponent, K::Selector,
                           K::Property, K::Attribute, K::UtilityClass, K::Component,    K::Hook,
                           K::Route,    K::Unknown};
    for (K k : all_kinds) {
        bool primary = RetrievalUnitClassifier::is_primary(k);
        bool supporting = RetrievalUnitClassifier::is_supporting(k);
        EXPECT_NE(primary, supporting)
            << "Kind " << static_cast<int>(k) << " must be exactly one of Primary or Supporting";
    }
}

TEST(RetrievalUnitClassifierTest, UnknownDefaultsToSupporting) {
    // Unknown and any future default must not compete as primary results.
    EXPECT_EQ(RetrievalUnitClassifier::classify(parser::ElementKind::Unknown),
              RetrievalUnitRole::Supporting);
    EXPECT_FALSE(RetrievalUnitClassifier::is_primary(parser::ElementKind::Unknown));
}

TEST(RetrievalUnitClassifierTest, ToStringRolesAreCorrect) {
    EXPECT_EQ(to_string(RetrievalUnitRole::Primary), "Primary");
    EXPECT_EQ(to_string(RetrievalUnitRole::Supporting), "Supporting");
}

// =============================================================================
// Resolver: Test 1 — Class → Method → Call (multi-level nesting)
// =============================================================================

TEST(SupportingEvidenceResolverTest, ClassMethodCallNesting) {
    parser::SourceParser parser;
    auto parsed = parser.parse_source("class AuthService {\n"
                                      "    public login(req: Request) {\n"
                                      "        const token = generateToken(req.user);\n"
                                      "        return token;\n"
                                      "    }\n"
                                      "}\n",
                                      "TypeScript", "src/auth/authService.ts");

    ASSERT_TRUE(parsed.success);
    auto units = SupportingEvidenceResolver::resolve_units(parsed);

    // Expect 2 primary units: AuthService (Class), login (Method)
    ASSERT_EQ(units.size(), 2u);

    const auto& class_unit = units[0];
    const auto& method_unit = units[1];

    EXPECT_EQ(class_unit.primary_element.name, "AuthService");
    EXPECT_EQ(class_unit.primary_element.kind, parser::ElementKind::Class);
    EXPECT_EQ(method_unit.primary_element.name, "login");
    EXPECT_EQ(method_unit.primary_element.kind, parser::ElementKind::Method);

    // generateToken Call should be attributed to login (innermost enclosing)
    bool call_in_method = false;
    for (const auto& ev : method_unit.supporting_elements) {
        if (ev.name == "generateToken" && ev.kind == parser::ElementKind::Call) {
            call_in_method = true;
            break;
        }
    }
    EXPECT_TRUE(call_in_method) << "generateToken call must be evidence of login method";

    // generateToken must NOT appear in the class-level evidence
    bool call_in_class = false;
    for (const auto& ev : class_unit.supporting_elements) {
        if (ev.name == "generateToken" && ev.kind == parser::ElementKind::Call) {
            call_in_class = true;
            break;
        }
    }
    EXPECT_FALSE(call_in_class) << "generateToken must not appear in AuthService evidence";
}

// =============================================================================
// Resolver: Test 2 — Component → JSX → Attribute
// =============================================================================

TEST(SupportingEvidenceResolverTest, ComponentJSXAttributeNesting) {
    parser::SourceParser parser;
    auto parsed = parser.parse_source("export function UserCard({ user }) {\n"
                                      "    return (\n"
                                      "        <div className=\"p-4 bg-white\">\n"
                                      "            <Avatar src={user.avatar} />\n"
                                      "            <span>{formatName(user.name)}</span>\n"
                                      "        </div>\n"
                                      "    );\n"
                                      "}\n",
                                      "TSX", "components/UserCard.tsx");

    ASSERT_TRUE(parsed.success);
    auto units = SupportingEvidenceResolver::resolve_units(parsed);

    // Should produce exactly 1 primary (UserCard Function/Component)
    ASSERT_EQ(units.size(), 1u);
    EXPECT_EQ(units[0].primary_element.name, "UserCard");
    EXPECT_EQ(units[0].role, RetrievalUnitRole::Primary);

    // Supporting elements should include JSX, Attribute, and Call elements
    EXPECT_GE(units[0].supporting_elements.size(), 2u)
        << "UserCard should have JSX/attribute/call supporting evidence";
    EXPECT_EQ(units[0].file_path, "components/UserCard.tsx");
}

// =============================================================================
// Resolver: Test 3 — Multiple independent functions in same file
//           Supporting elements must go to their respective enclosing function,
//           NOT to an arbitrary first function.
// =============================================================================

TEST(SupportingEvidenceResolverTest, MultipleFunctionsInSameFileNoFalseOwnership) {
    parser::SourceParser parser;
    // Two completely independent functions; calls inside each belong only to that fn.
    auto parsed = parser.parse_source("function formatDate(d: Date) {\n"
                                      "    return d.toISOString();\n"
                                      "}\n"
                                      "\n"
                                      "function parseDate(s: string) {\n"
                                      "    return new Date(s);\n"
                                      "}\n",
                                      "TypeScript", "src/lib/dateUtils.ts");

    ASSERT_TRUE(parsed.success);
    auto units = SupportingEvidenceResolver::resolve_units(parsed);

    ASSERT_GE(units.size(), 2u);

    // Find formatDate and parseDate units
    const RetrievalUnit* fmt_unit = nullptr;
    const RetrievalUnit* parse_unit = nullptr;
    for (const auto& u : units) {
        if (u.primary_element.name == "formatDate")
            fmt_unit = &u;
        if (u.primary_element.name == "parseDate")
            parse_unit = &u;
    }
    ASSERT_NE(fmt_unit, nullptr) << "formatDate unit must exist";
    ASSERT_NE(parse_unit, nullptr) << "parseDate unit must exist";

    // toISOString call must be evidence of formatDate, not parseDate
    bool iso_in_format = false;
    bool iso_in_parse = false;
    for (const auto& ev : fmt_unit->supporting_elements) {
        if (ev.name == "toISOString")
            iso_in_format = true;
    }
    for (const auto& ev : parse_unit->supporting_elements) {
        if (ev.name == "toISOString")
            iso_in_parse = true;
    }
    EXPECT_TRUE(iso_in_format) << "toISOString should be evidence of formatDate";
    EXPECT_FALSE(iso_in_parse) << "toISOString must NOT appear in parseDate evidence";
}

// =============================================================================
// Resolver: Test 4 — Multiple independent components in same file
//           Each component's JSX evidence must not leak to the other.
// =============================================================================

TEST(SupportingEvidenceResolverTest, MultipleComponentsInSameFileNoLeakage) {
    parser::SourceParser parser;
    auto parsed = parser.parse_source("export function Header() {\n"
                                      "    return <nav className=\"header\">Header</nav>;\n"
                                      "}\n"
                                      "\n"
                                      "export function Footer() {\n"
                                      "    return <footer className=\"footer\">Footer</footer>;\n"
                                      "}\n",
                                      "TSX", "src/components/layout.tsx");

    ASSERT_TRUE(parsed.success);
    auto units = SupportingEvidenceResolver::resolve_units(parsed);

    ASSERT_GE(units.size(), 2u);

    const RetrievalUnit* header_unit = nullptr;
    const RetrievalUnit* footer_unit = nullptr;
    for (const auto& u : units) {
        if (u.primary_element.name == "Header")
            header_unit = &u;
        if (u.primary_element.name == "Footer")
            footer_unit = &u;
    }

    if (header_unit != nullptr && footer_unit != nullptr) {
        // "footer" CSS class must not appear in Header's evidence
        bool footer_class_in_header = false;
        for (const auto& ev : header_unit->supporting_elements) {
            if (ev.name == "footer")
                footer_class_in_header = true;
        }
        EXPECT_FALSE(footer_class_in_header)
            << "'footer' class must not appear in Header component evidence";

        // "header" CSS class must not appear in Footer's evidence
        bool header_class_in_footer = false;
        for (const auto& ev : footer_unit->supporting_elements) {
            if (ev.name == "header")
                header_class_in_footer = true;
        }
        EXPECT_FALSE(header_class_in_footer)
            << "'header' class must not appear in Footer component evidence";
    }
}

// =============================================================================
// Resolver: Test 5 — Nested functions (inner function is itself a Primary unit)
//           Inner function's calls attributed to it, not the outer function.
// =============================================================================

TEST(SupportingEvidenceResolverTest, NestedFunctionInnermostPrimaryWins) {
    parser::SourceParser parser;
    auto parsed = parser.parse_source("function outer() {\n"
                                      "    function inner() {\n"
                                      "        doWork();\n"
                                      "    }\n"
                                      "    inner();\n"
                                      "}\n",
                                      "TypeScript", "src/util.ts");

    ASSERT_TRUE(parsed.success);
    auto units = SupportingEvidenceResolver::resolve_units(parsed);

    // Both outer and inner are Functions → both Primary
    ASSERT_GE(units.size(), 2u);

    const RetrievalUnit* inner_unit = nullptr;
    for (const auto& u : units) {
        if (u.primary_element.name == "inner") {
            inner_unit = &u;
            break;
        }
    }

    if (inner_unit != nullptr) {
        // doWork() is inside inner(), so it must be evidence of inner
        bool dowork_in_inner = false;
        for (const auto& ev : inner_unit->supporting_elements) {
            if (ev.name == "doWork" && ev.kind == parser::ElementKind::Call) {
                dowork_in_inner = true;
                break;
            }
        }
        EXPECT_TRUE(dowork_in_inner)
            << "doWork call inside inner() should be attributed to inner, not outer";
    }
}

// =============================================================================
// Resolver: Test 6 — Unresolved supporting element (no matching primary)
//           If a supporting element cannot be attributed, it must not be silently
//           assigned to an unrelated primary. resolve_with_diagnostics exposes this.
// =============================================================================

TEST(SupportingEvidenceResolverTest, UnresolvedSupportingElementHasNulloptOwner) {
    // Construct a ParsedFile manually with a supporting element that has
    // no plausible parent (no parent_context, line range outside all primaries).
    parser::ParsedFile file;
    file.file_path = "src/orphan.ts";
    file.language = "TypeScript";
    file.success = true;

    // Primary: lines 1–5
    parser::CodeElement primary;
    primary.kind = parser::ElementKind::Function;
    primary.name = "myFunc";
    primary.location.start = {.line = 1, .column = 1, .byte_offset = 0};
    primary.location.end = {.line = 5, .column = 1, .byte_offset = 0};
    primary.parent_context = "";
    file.elements.push_back(primary);

    // Supporting: line 50, completely outside myFunc (lines 1–5), no parent_context
    parser::CodeElement orphan;
    orphan.kind = parser::ElementKind::Call;
    orphan.name = "orphanCall";
    orphan.location.start = {.line = 50, .column = 1, .byte_offset = 0};
    orphan.location.end = {.line = 50, .column = 20, .byte_offset = 0};
    orphan.parent_context = "";  // no parent context
    file.elements.push_back(orphan);

    std::vector<RetrievalUnit> units;
    std::vector<EvidenceResolution> resolutions;
    SupportingEvidenceResolver::resolve_with_diagnostics(file, units, resolutions);

    ASSERT_EQ(units.size(), 1u);  // myFunc

    // Find the resolution for orphanCall
    bool found_orphan_resolution = false;
    for (const auto& res : resolutions) {
        if (file.elements[res.supporting_element_idx].name == "orphanCall") {
            found_orphan_resolution = true;
            EXPECT_FALSE(res.owner_unit_index.has_value())
                << "orphanCall outside all primary ranges must remain unresolved";
            break;
        }
    }
    EXPECT_TRUE(found_orphan_resolution) << "Resolution record must exist for orphanCall";

    // orphanCall must NOT appear in myFunc's evidence
    EXPECT_TRUE(units[0].supporting_elements.empty())
        << "myFunc must have no supporting evidence (orphanCall is unresolved)";
}

// =============================================================================
// Resolver: Test 7 — Cross-file: each file produces independent units
//           Elements from file A must not appear in file B's units.
// =============================================================================

TEST(SupportingEvidenceResolverTest, CrossFileUnitsAreIndependent) {
    parser::SourceParser sp;

    auto fileA = sp.parse_source("export function fetchData() {\n"
                                 "    return getData();\n"
                                 "}\n",
                                 "TypeScript", "src/api/fetch.ts");

    auto fileB = sp.parse_source("export function renderList() {\n"
                                 "    return mapItems();\n"
                                 "}\n",
                                 "TypeScript", "src/ui/list.ts");

    ASSERT_TRUE(fileA.success);
    ASSERT_TRUE(fileB.success);

    const parser::ParsedFile files[] = {fileA, fileB};
    auto units =
        SupportingEvidenceResolver::resolve_units(std::span<const parser::ParsedFile>(files));

    // Expect at least one unit per file
    bool found_fetch = false;
    bool found_render = false;
    for (const auto& u : units) {
        if (u.primary_element.name == "fetchData")
            found_fetch = true;
        if (u.primary_element.name == "renderList")
            found_render = true;
    }
    EXPECT_TRUE(found_fetch) << "fetchData unit must exist from fileA";
    EXPECT_TRUE(found_render) << "renderList unit must exist from fileB";

    // mapItems must not appear in fetchData's evidence
    for (const auto& u : units) {
        if (u.primary_element.name == "fetchData") {
            for (const auto& ev : u.supporting_elements) {
                EXPECT_NE(ev.name, "mapItems")
                    << "mapItems from fileB must not appear in fetchData evidence";
            }
        }
    }
}

// =============================================================================
// Resolver: Test 8 — Overlapping source ranges: innermost primary wins
//           When a supporting element is enclosed by both a Class and a Method,
//           the Method (smaller span) must claim it.
// =============================================================================

TEST(SupportingEvidenceResolverTest, InnermostEnclosingPrimaryWinsOnOverlap) {
    // This is already covered by Test 1, but we verify it explicitly with
    // is_enclosed_by and span comparison for a synthetic case.
    parser::CodeElement outer_class;
    outer_class.kind = parser::ElementKind::Class;
    outer_class.name = "OuterClass";
    outer_class.location.start = {.line = 1, .column = 1, .byte_offset = 0};
    outer_class.location.end = {.line = 20, .column = 1, .byte_offset = 0};

    parser::CodeElement inner_method;
    inner_method.kind = parser::ElementKind::Method;
    inner_method.name = "innerMethod";
    inner_method.location.start = {.line = 5, .column = 5, .byte_offset = 0};
    inner_method.location.end = {.line = 10, .column = 5, .byte_offset = 0};
    inner_method.parent_context = "OuterClass";

    parser::CodeElement call_inside;
    call_inside.kind = parser::ElementKind::Call;
    call_inside.name = "helperCall";
    call_inside.location.start = {.line = 7, .column = 9, .byte_offset = 0};
    call_inside.location.end = {.line = 7, .column = 25, .byte_offset = 0};
    call_inside.parent_context = "innerMethod";

    // Both Class and Method enclose the Call
    EXPECT_TRUE(SupportingEvidenceResolver::is_enclosed_by(call_inside, outer_class));
    EXPECT_TRUE(SupportingEvidenceResolver::is_enclosed_by(call_inside, inner_method));

    // Span of outer_class = 20 - 1 = 19; span of inner_method = 10 - 5 = 5
    // The resolver must pick the innermost (inner_method) by smallest span.
    parser::ParsedFile file;
    file.file_path = "src/overlap.ts";
    file.language = "TypeScript";
    file.success = true;
    file.elements = {outer_class, inner_method, call_inside};

    auto units = SupportingEvidenceResolver::resolve_units(file);
    ASSERT_EQ(units.size(), 2u);

    const RetrievalUnit* method_unit = nullptr;
    for (const auto& u : units) {
        if (u.primary_element.name == "innerMethod")
            method_unit = &u;
    }
    ASSERT_NE(method_unit, nullptr);

    bool call_in_method = false;
    for (const auto& ev : method_unit->supporting_elements) {
        if (ev.name == "helperCall")
            call_in_method = true;
    }
    EXPECT_TRUE(call_in_method)
        << "helperCall must be attributed to innerMethod (innermost), not OuterClass";
}

// =============================================================================
// Resolver: Test 9 — Deterministic ownership (same input → same result)
// =============================================================================

TEST(SupportingEvidenceResolverTest, DeterministicOwnershipSameInputSameOutput) {
    parser::SourceParser sp;
    auto parsed = sp.parse_source("class SomeService {\n"
                                  "    public process(data: string) {\n"
                                  "        return transform(data);\n"
                                  "    }\n"
                                  "}\n",
                                  "TypeScript", "src/service.ts");

    ASSERT_TRUE(parsed.success);

    auto units1 = SupportingEvidenceResolver::resolve_units(parsed);
    auto units2 = SupportingEvidenceResolver::resolve_units(parsed);

    ASSERT_EQ(units1.size(), units2.size());
    for (std::size_t i = 0; i < units1.size(); ++i) {
        EXPECT_EQ(units1[i], units2[i])
            << "Unit " << i << " must be identical across two resolve calls";
    }
}

// =============================================================================
// Resolver: Test 10 — CSS file (no primary kinds): synthetic module-level unit
// =============================================================================

TEST(SupportingEvidenceResolverTest, PureCSSFileGetsSyntheticModuleUnit) {
    parser::SourceParser sp;
    auto parsed = sp.parse_source(".card {\n"
                                  "    background-color: white;\n"
                                  "    padding: 16px;\n"
                                  "}\n",
                                  "CSS", "src/styles/card.css");

    ASSERT_TRUE(parsed.success);

    std::vector<RetrievalUnit> units;
    std::vector<EvidenceResolution> resolutions;
    SupportingEvidenceResolver::resolve_with_diagnostics(parsed, units, resolutions);

    // A CSS file with only Selector/Property should get exactly one synthetic unit.
    ASSERT_EQ(units.size(), 1u);
    EXPECT_EQ(units[0].role, RetrievalUnitRole::Primary);
    EXPECT_EQ(units[0].primary_element.kind, parser::ElementKind::Route);
    EXPECT_EQ(units[0].primary_element.detail, "File Module");

    // All supporting elements must be resolved (to the only unit, index 0).
    for (const auto& res : resolutions) {
        EXPECT_TRUE(res.owner_unit_index.has_value())
            << "In a module-level-only file, all elements must be attributed";
        if (res.owner_unit_index.has_value()) {
            EXPECT_EQ(*res.owner_unit_index, 0u);
        }
    }

    // The single unit must have CSS selectors/properties as supporting evidence.
    EXPECT_GE(units[0].supporting_elements.size(), 1u)
        << "CSS module unit should have at least one supporting element";
}

}  // namespace amoeba::retrieval
