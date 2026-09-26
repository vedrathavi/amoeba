// Phase 7.2 — RelationshipEvidenceResolver Tests
//
// Tests cover:
//   A. Function with outgoing Calls (callees)
//   B. Function with incoming Calls (callers)
//   C. Both callers and callees on the same function
//   D. Imports relationship evidence
//   E. Includes relationship evidence
//   F. Inheritance (base classes and subclasses)
//   G. Implements (interfaces and implementers)
//   H. References relationship evidence
//   I. Contains hierarchy evidence
//   J. Multiple mixed relationship kinds on a single symbol
//   K. Symbol with no relationships
//   L. Missing/unresolvable target ID handling
//   M. High-degree fanout capping (max per kind)
//   N. Deterministic ordering across calls
//   O. Cyclic relationships (A -> B -> A) safely resolved without recursion
//   P. Cross-file relationships with accurate file paths
//   Q. Resolution via RetrievalUnit and PrimarySearchResult overloads
//   R. Repeated resolution producing identical output

#include "amoeba/graph/relationship_evidence_resolver.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"

#include <gtest/gtest.h>

namespace amoeba::graph {

namespace {

class RelationshipEvidenceResolverTest : public ::testing::Test {
protected:
    parser::SourceParser parser_;
    index::InvertedIndex index_;
    RelationshipGraph graph_;

    ElementId add_element(std::string_view name, parser::ElementKind kind,
                          std::string_view file_path, uint32_t line = 1, uint32_t col = 1) {
        parser::ParsedFile pf{
            .file_path = std::filesystem::path(file_path),
            .language = "C++",
            .success = true,
            .has_syntax_errors = false,
            .elements = {parser::CodeElement{
                .kind = kind,
                .name = std::string(name),
                .location =
                    parser::SourceRange{
                        .start = {.line = line, .column = col, .byte_offset = 0},
                        .end = {.line = line + 5, .column = 1, .byte_offset = 50},
                    },
                .parent_context = "",
                .detail = "",
            }},
        };
        index_.add_parsed_file(pf);
        return static_cast<ElementId>(index_.element_count() - 1);
    }
};

// A. Function with outgoing Calls (callees)
TEST_F(RelationshipEvidenceResolverTest, FunctionWithOutgoingCalls) {
    const auto caller_id =
        add_element("processData", parser::ElementKind::Function, "src/proc.cpp", 10);
    const auto callee1_id =
        add_element("validateInput", parser::ElementKind::Function, "src/val.cpp", 20);
    const auto callee2_id =
        add_element("saveToDb", parser::ElementKind::Function, "src/db.cpp", 30);

    graph_.add_relationship(caller_id, callee1_id, RelationshipKind::Calls);
    graph_.add_relationship(caller_id, callee2_id, RelationshipKind::Calls);

    RelationshipEvidenceResolver resolver(graph_, index_);
    const auto evidence = resolver.resolve(caller_id);

    ASSERT_EQ(evidence.size(), 2u);
    EXPECT_EQ(evidence[0].direction, RelationshipDirection::Outgoing);
    EXPECT_EQ(evidence[0].kind, RelationshipKind::Calls);
    EXPECT_EQ(evidence[1].direction, RelationshipDirection::Outgoing);
    EXPECT_EQ(evidence[1].kind, RelationshipKind::Calls);

    // Sorted by file path / line
    EXPECT_EQ(evidence[0].related_name, "saveToDb");
    EXPECT_EQ(evidence[1].related_name, "validateInput");
}

// B. Function with incoming Calls (callers)
TEST_F(RelationshipEvidenceResolverTest, FunctionWithIncomingCalls) {
    const auto target_id = add_element("logError", parser::ElementKind::Function, "src/log.cpp", 5);
    const auto caller1_id =
        add_element("handleAuth", parser::ElementKind::Function, "src/auth.cpp", 15);
    const auto caller2_id =
        add_element("handlePayment", parser::ElementKind::Function, "src/pay.cpp", 25);

    graph_.add_relationship(caller1_id, target_id, RelationshipKind::Calls);
    graph_.add_relationship(caller2_id, target_id, RelationshipKind::Calls);

    RelationshipEvidenceResolver resolver(graph_, index_);
    const auto evidence = resolver.resolve(target_id);

    ASSERT_EQ(evidence.size(), 2u);
    EXPECT_EQ(evidence[0].direction, RelationshipDirection::Incoming);
    EXPECT_EQ(evidence[0].kind, RelationshipKind::Calls);
    EXPECT_EQ(evidence[0].related_name, "handleAuth");

    EXPECT_EQ(evidence[1].direction, RelationshipDirection::Incoming);
    EXPECT_EQ(evidence[1].kind, RelationshipKind::Calls);
    EXPECT_EQ(evidence[1].related_name, "handlePayment");
}

// C. Both callers and callees on the same function
TEST_F(RelationshipEvidenceResolverTest, BothCallersAndCallees) {
    const auto main_fn =
        add_element("executePipeline", parser::ElementKind::Function, "src/pipe.cpp", 10);
    const auto caller = add_element("runService", parser::ElementKind::Function, "src/srv.cpp", 5);
    const auto callee =
        add_element("flushBuffer", parser::ElementKind::Function, "src/buf.cpp", 20);

    graph_.add_relationship(caller, main_fn, RelationshipKind::Calls);
    graph_.add_relationship(main_fn, callee, RelationshipKind::Calls);

    RelationshipEvidenceResolver resolver(graph_, index_);
    const auto evidence = resolver.resolve(main_fn);

    ASSERT_EQ(evidence.size(), 2u);
    // Outgoing (callee) appears before Incoming (caller) in default sort
    EXPECT_EQ(evidence[0].direction, RelationshipDirection::Outgoing);
    EXPECT_EQ(evidence[0].related_name, "flushBuffer");

    EXPECT_EQ(evidence[1].direction, RelationshipDirection::Incoming);
    EXPECT_EQ(evidence[1].related_name, "runService");
}

// D. Imports relationship
TEST_F(RelationshipEvidenceResolverTest, ImportsRelationship) {
    const auto file_id = add_element("main.ts", parser::ElementKind::Class, "src/main.ts", 1);
    const auto mod_id = add_element("utils", parser::ElementKind::Class, "src/utils.ts", 1);

    graph_.add_relationship(file_id, mod_id, RelationshipKind::Imports);

    RelationshipEvidenceResolver resolver(graph_, index_);
    const auto evidence = resolver.resolve(file_id);

    ASSERT_EQ(evidence.size(), 1u);
    EXPECT_EQ(evidence[0].kind, RelationshipKind::Imports);
    EXPECT_EQ(evidence[0].direction, RelationshipDirection::Outgoing);
    EXPECT_EQ(evidence[0].related_name, "utils");
}

// E. Includes relationship
TEST_F(RelationshipEvidenceResolverTest, IncludesRelationship) {
    const auto cpp_id = add_element("app.cpp", parser::ElementKind::Function, "src/app.cpp", 1);
    const auto hpp_id = add_element("app.hpp", parser::ElementKind::Class, "include/app.hpp", 1);

    graph_.add_relationship(cpp_id, hpp_id, RelationshipKind::Includes);

    RelationshipEvidenceResolver resolver(graph_, index_);
    const auto evidence = resolver.resolve(cpp_id);

    ASSERT_EQ(evidence.size(), 1u);
    EXPECT_EQ(evidence[0].kind, RelationshipKind::Includes);
    EXPECT_EQ(evidence[0].related_name, "app.hpp");
}

// F. Inheritance (Base classes & Derived subclasses)
TEST_F(RelationshipEvidenceResolverTest, InheritanceRelationships) {
    const auto base_id =
        add_element("EmbeddingProvider", parser::ElementKind::Class, "include/provider.hpp", 10);
    const auto derived_id =
        add_element("PretrainedProvider", parser::ElementKind::Class, "include/pretrained.hpp", 20);

    graph_.add_relationship(derived_id, base_id, RelationshipKind::InheritsFrom);

    RelationshipEvidenceResolver resolver(graph_, index_);

    // Check derived side (outgoing InheritsFrom base)
    const auto derived_ev = resolver.resolve(derived_id);
    ASSERT_EQ(derived_ev.size(), 1u);
    EXPECT_EQ(derived_ev[0].direction, RelationshipDirection::Outgoing);
    EXPECT_EQ(derived_ev[0].kind, RelationshipKind::InheritsFrom);
    EXPECT_EQ(derived_ev[0].related_name, "EmbeddingProvider");

    // Check base side (incoming InheritsFrom derived)
    const auto base_ev = resolver.resolve(base_id);
    ASSERT_EQ(base_ev.size(), 1u);
    EXPECT_EQ(base_ev[0].direction, RelationshipDirection::Incoming);
    EXPECT_EQ(base_ev[0].kind, RelationshipKind::InheritsFrom);
    EXPECT_EQ(base_ev[0].related_name, "PretrainedProvider");
}

// G. Implements (Interface & Implementer)
TEST_F(RelationshipEvidenceResolverTest, ImplementsRelationships) {
    const auto iface_id =
        add_element("ISerializable", parser::ElementKind::Interface, "include/serial.hpp", 5);
    const auto impl_id =
        add_element("UserConfig", parser::ElementKind::Class, "src/config.cpp", 10);

    graph_.add_relationship(impl_id, iface_id, RelationshipKind::Implements);

    RelationshipEvidenceResolver resolver(graph_, index_);
    const auto ev = resolver.resolve(impl_id);

    ASSERT_EQ(ev.size(), 1u);
    EXPECT_EQ(ev[0].kind, RelationshipKind::Implements);
    EXPECT_EQ(ev[0].direction, RelationshipDirection::Outgoing);
    EXPECT_EQ(ev[0].related_name, "ISerializable");
}

// H. References relationship
TEST_F(RelationshipEvidenceResolverTest, ReferencesRelationship) {
    const auto fn_id = add_element("renderUI", parser::ElementKind::Function, "src/ui.cpp", 10);
    const auto var_id =
        add_element("GLOBAL_THEME", parser::ElementKind::Attribute, "src/theme.cpp", 1);

    graph_.add_relationship(fn_id, var_id, RelationshipKind::References);

    RelationshipEvidenceResolver resolver(graph_, index_);
    const auto ev = resolver.resolve(fn_id);

    ASSERT_EQ(ev.size(), 1u);
    EXPECT_EQ(ev[0].kind, RelationshipKind::References);
    EXPECT_EQ(ev[0].related_name, "GLOBAL_THEME");
}

// I. Contains hierarchy
TEST_F(RelationshipEvidenceResolverTest, ContainsHierarchy) {
    const auto class_id = add_element("AuthManager", parser::ElementKind::Class, "src/auth.cpp", 1);
    const auto method_id = add_element("login", parser::ElementKind::Method, "src/auth.cpp", 10);

    graph_.add_relationship(class_id, method_id, RelationshipKind::Contains);

    RelationshipEvidenceResolver resolver(graph_, index_);
    const auto ev = resolver.resolve(class_id);

    ASSERT_EQ(ev.size(), 1u);
    EXPECT_EQ(ev[0].kind, RelationshipKind::Contains);
    EXPECT_EQ(ev[0].related_name, "login");
}

// J. Multiple mixed relationship kinds
TEST_F(RelationshipEvidenceResolverTest, MultipleRelationshipKinds) {
    const auto focal =
        add_element("SearchEngine", parser::ElementKind::Class, "src/search.cpp", 10);
    const auto parent_file =
        add_element("search.cpp", parser::ElementKind::Class, "src/search.cpp", 1);
    const auto base = add_element("BaseEngine", parser::ElementKind::Class, "include/base.hpp", 5);
    const auto callee = add_element("tokenize", parser::ElementKind::Function, "src/tok.cpp", 20);
    const auto caller = add_element("cliSearch", parser::ElementKind::Function, "src/cli.cpp", 30);

    graph_.add_relationship(parent_file, focal, RelationshipKind::Contains);
    graph_.add_relationship(focal, base, RelationshipKind::InheritsFrom);
    graph_.add_relationship(focal, callee, RelationshipKind::Calls);
    graph_.add_relationship(caller, focal, RelationshipKind::Calls);

    RelationshipEvidenceResolver resolver(graph_, index_);
    const auto ev = resolver.resolve(focal);

    ASSERT_EQ(ev.size(), 4u);
}

// K. Symbol with no relationships
TEST_F(RelationshipEvidenceResolverTest, SymbolWithNoRelationshipsReturnsEmpty) {
    const auto lonely_id =
        add_element("standaloneHelper", parser::ElementKind::Function, "src/alone.cpp", 1);

    RelationshipEvidenceResolver resolver(graph_, index_);
    const auto ev = resolver.resolve(lonely_id);

    EXPECT_TRUE(ev.empty());
}

// L. Missing / unresolvable target ID
TEST_F(RelationshipEvidenceResolverTest, UnresolvableTargetIdHandledSafely) {
    const auto focal = add_element("callerFn", parser::ElementKind::Function, "src/caller.cpp", 1);
    const ElementId unindexed_id = 99999;  // Not in InvertedIndex

    graph_.add_relationship(focal, unindexed_id, RelationshipKind::Calls);

    RelationshipEvidenceResolver resolver(graph_, index_);
    const auto ev = resolver.resolve(focal);

    ASSERT_EQ(ev.size(), 1u);
    EXPECT_EQ(ev[0].related_element_id, unindexed_id);
    EXPECT_EQ(ev[0].related_kind, parser::ElementKind::Unknown);
    EXPECT_EQ(ev[0].related_name, "");
}

// M. High-degree node fanout limit
TEST_F(RelationshipEvidenceResolverTest, FanoutLimitCapsPerKind) {
    const auto focal = add_element("hubFunction", parser::ElementKind::Function, "src/hub.cpp", 1);

    // Add 10 outgoing calls
    for (int i = 0; i < 10; ++i) {
        const auto callee = add_element("target_" + std::to_string(i),
                                        parser::ElementKind::Function, "src/target.cpp", 10 + i);
        graph_.add_relationship(focal, callee, RelationshipKind::Calls);
    }

    RelationshipEvidenceResolver resolver(graph_, index_);
    RelationshipEvidenceOptions opts{
        .max_outgoing_per_kind = 5,
        .max_incoming_per_kind = 5,
    };
    const auto ev = resolver.resolve(focal, opts);

    // Exactly 5 calls kept
    EXPECT_EQ(ev.size(), 5u);
}

// N. Deterministic ordering
TEST_F(RelationshipEvidenceResolverTest, DeterministicOrderingAcrossRuns) {
    const auto focal = add_element("rootService", parser::ElementKind::Class, "src/root.cpp", 1);
    const auto c1 = add_element("zetaFn", parser::ElementKind::Function, "src/z.cpp", 10);
    const auto c2 = add_element("alphaFn", parser::ElementKind::Function, "src/a.cpp", 10);
    const auto c3 = add_element("betaFn", parser::ElementKind::Function, "src/b.cpp", 10);

    graph_.add_relationship(focal, c1, RelationshipKind::Calls);
    graph_.add_relationship(focal, c2, RelationshipKind::Calls);
    graph_.add_relationship(focal, c3, RelationshipKind::Calls);

    RelationshipEvidenceResolver resolver(graph_, index_);
    const auto ev1 = resolver.resolve(focal);
    const auto ev2 = resolver.resolve(focal);

    ASSERT_EQ(ev1.size(), 3u);
    EXPECT_EQ(ev1, ev2);
    // Verified alphabetical ordering by file path: a.cpp, b.cpp, z.cpp
    EXPECT_EQ(ev1[0].related_name, "alphaFn");
    EXPECT_EQ(ev1[1].related_name, "betaFn");
    EXPECT_EQ(ev1[2].related_name, "zetaFn");
}

// O. Cyclic relationships safely resolved (no recursion)
TEST_F(RelationshipEvidenceResolverTest, CyclicRelationshipsDoNotLoop) {
    const auto a = add_element("serviceA", parser::ElementKind::Class, "src/a.cpp", 1);
    const auto b = add_element("serviceB", parser::ElementKind::Class, "src/b.cpp", 1);

    // A calls B and B calls A
    graph_.add_relationship(a, b, RelationshipKind::Calls);
    graph_.add_relationship(b, a, RelationshipKind::Calls);

    RelationshipEvidenceResolver resolver(graph_, index_);
    const auto ev_a = resolver.resolve(a);
    const auto ev_b = resolver.resolve(b);

    ASSERT_EQ(ev_a.size(), 2u);  // 1 outgoing (Calls B) + 1 incoming (Called by B)
    ASSERT_EQ(ev_b.size(), 2u);  // 1 outgoing (Calls A) + 1 incoming (Called by A)
}

// P. Cross-file relationships with accurate file paths
TEST_F(RelationshipEvidenceResolverTest, CrossFileMetadataAccurate) {
    const auto server =
        add_element("ApiServer", parser::ElementKind::Class, "server/server.cpp", 10);
    const auto client =
        add_element("ApiClient", parser::ElementKind::Class, "client/client.ts", 20);

    graph_.add_relationship(server, client, RelationshipKind::Calls);

    RelationshipEvidenceResolver resolver(graph_, index_);
    const auto ev = resolver.resolve(server);

    ASSERT_EQ(ev.size(), 1u);
    EXPECT_EQ(ev[0].related_file_path.generic_string(), "client/client.ts");
    EXPECT_EQ(ev[0].related_location.start.line, 20u);
}

// Q. Resolution via RetrievalUnit and PrimarySearchResult
TEST_F(RelationshipEvidenceResolverTest, ResolutionViaRetrievalUnitAndSearchResult) {
    const auto focal_id =
        add_element("PaymentService", parser::ElementKind::Class, "src/pay.cpp", 1);
    const auto dep_id =
        add_element("StripeClient", parser::ElementKind::Class, "src/stripe.cpp", 1);

    graph_.add_relationship(focal_id, dep_id, RelationshipKind::References);

    retrieval::RetrievalUnit unit{
        .primary_element_id = focal_id,
        .primary_element = parser::CodeElement{.name = "PaymentService"},
        .file_path = "src/pay.cpp",
        .language = "C++",
    };

    retrieval::PrimarySearchResult result{
        .unit = unit,
        .hybrid_score = 0.95,
    };

    RelationshipEvidenceResolver resolver(graph_, index_);
    const auto ev_unit = resolver.resolve(unit);
    const auto ev_res = resolver.resolve(result);

    EXPECT_EQ(ev_unit.size(), 1u);
    EXPECT_EQ(ev_unit, ev_res);
    EXPECT_EQ(ev_unit[0].related_name, "StripeClient");
}

}  // namespace
}  // namespace amoeba::graph
