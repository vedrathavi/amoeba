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
#include "amoeba/source/source_snippet_reader.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

using namespace amoeba;
using namespace amoeba::evidence;
using namespace amoeba::retrieval;
using namespace amoeba::graph;
using namespace amoeba::parser;
using namespace amoeba::index;
using namespace amoeba::source;

class EvidenceAssemblerTest : public ::testing::Test {
protected:
    std::filesystem::path test_dir_;
    InvertedIndex index_;
    RelationshipGraph graph_;

    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "amoeba_evidence_assembler_test";
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::filesystem::path write_temp_file(const std::string& filename, const std::string& content) {
        auto path = test_dir_ / filename;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream ofs(path, std::ios::binary);
        ofs << content;
        ofs.close();
        return path;
    }

    ElementId add_element(std::string_view name, ElementKind kind,
                          const std::filesystem::path& file_path, uint32_t start_line = 1,
                          uint32_t end_line = 5, uint32_t start_offset = 0,
                          uint32_t end_offset = 50, std::string_view parent_ctx = "",
                          std::string_view detail = "") {
        ParsedFile pf{
            .file_path = file_path,
            .language = "C++",
            .success = true,
            .has_syntax_errors = false,
            .elements = {CodeElement{
                .kind = kind,
                .name = std::string(name),
                .location =
                    SourceRange{
                        .start = {.line = start_line, .column = 1, .byte_offset = start_offset},
                        .end = {.line = end_line, .column = 1, .byte_offset = end_offset},
                    },
                .parent_context = std::string(parent_ctx),
                .detail = std::string(detail),
            }},
        };
        index_.add_parsed_file(pf);
        return static_cast<ElementId>(index_.element_count() - 1);
    }
};

// A. One primary result with source
TEST_F(EvidenceAssemblerTest, SinglePrimaryResultWithSource) {
    auto file_path = write_temp_file("auth.cpp", "// Auth module\n"
                                                 "bool authenticateUser(const string& user) {\n"
                                                 "    return true;\n"
                                                 "}\n");

    auto fn_id = add_element("authenticateUser", ElementKind::Function, file_path, 2, 4, 16, 68, "",
                             "bool authenticateUser(const string& user)");

    RelationshipEvidenceResolver rel_resolver(graph_, index_);
    EvidenceAssembler assembler(rel_resolver);

    PrimarySearchResult result;
    result.unit.primary_element_id = fn_id;
    result.unit.primary_element = index_.get_element(fn_id).element;
    result.unit.file_path = file_path;
    result.unit.language = "cpp";
    result.hybrid_score = 0.95;
    result.provenance = RetrievalProvenance::HybridBoth;

    auto bundle = assembler.assemble("authenticate user", {result});

    EXPECT_EQ(bundle.query, "authenticate user");
    ASSERT_EQ(bundle.size(), 1u);

    const auto& item = bundle.items.front();
    EXPECT_EQ(item.primary_element_id(), fn_id);
    EXPECT_EQ(item.primary_element().name, "authenticateUser");
    EXPECT_EQ(item.primary_result.hybrid_score, 0.95);
    EXPECT_EQ(item.primary_result.provenance, RetrievalProvenance::HybridBoth);
    ASSERT_TRUE(item.has_source_excerpt());
    EXPECT_EQ(item.source_excerpt->start_line, 2u);
    EXPECT_EQ(item.source_excerpt->end_line, 4u);
    EXPECT_NE(item.source_excerpt->text.find("authenticateUser"), std::string::npos);
    EXPECT_TRUE(item.direct_relationships.empty());
}

// B. Multiple primary results preserve retrieval order
TEST_F(EvidenceAssemblerTest, MultiplePrimaryResultsPreserveRetrievalOrder) {
    auto path = write_temp_file("math.cpp", "int add(int a, int b) { return a + b; }\n"
                                            "int sub(int a, int b) { return a - b; }\n"
                                            "int mul(int a, int b) { return a * b; }\n");

    auto add_id = add_element("add", ElementKind::Function, path, 1, 1, 0, 40);
    auto sub_id = add_element("sub", ElementKind::Function, path, 2, 2, 41, 81);
    auto mul_id = add_element("mul", ElementKind::Function, path, 3, 3, 82, 122);

    RelationshipEvidenceResolver rel_resolver(graph_, index_);
    EvidenceAssembler assembler(rel_resolver);

    PrimarySearchResult res1;
    res1.unit.primary_element_id = mul_id;
    res1.unit.primary_element = index_.get_element(mul_id).element;
    res1.unit.file_path = path;
    res1.hybrid_score = 0.9;

    PrimarySearchResult res2;
    res2.unit.primary_element_id = add_id;
    res2.unit.primary_element = index_.get_element(add_id).element;
    res2.unit.file_path = path;
    res2.hybrid_score = 0.7;

    PrimarySearchResult res3;
    res3.unit.primary_element_id = sub_id;
    res3.unit.primary_element = index_.get_element(sub_id).element;
    res3.unit.file_path = path;
    res3.hybrid_score = 0.5;

    auto bundle = assembler.assemble("math ops", {res1, res2, res3});

    ASSERT_EQ(bundle.size(), 3u);
    EXPECT_EQ(bundle.items[0].primary_element().name, "mul");
    EXPECT_EQ(bundle.items[1].primary_element().name, "add");
    EXPECT_EQ(bundle.items[2].primary_element().name, "sub");
}

// C & D. Primary metadata preserved and SourceExcerpt attached
TEST_F(EvidenceAssemblerTest, PrimaryMetadataAndSourceExcerptWithContext) {
    auto path = write_temp_file("service.cpp", "// Header\n"
                                               "#include <iostream>\n"
                                               "class PaymentService {\n"
                                               "public:\n"
                                               "    void process() {}\n"
                                               "};\n"
                                               "// Footer\n");

    auto cls_id = add_element("PaymentService", ElementKind::Class, path, 3, 6, 29, 78, "",
                              "class PaymentService");

    RelationshipEvidenceResolver rel_resolver(graph_, index_);
    EvidenceAssembler assembler(rel_resolver);

    PrimarySearchResult res;
    res.unit.primary_element_id = cls_id;
    res.unit.primary_element = index_.get_element(cls_id).element;
    res.unit.file_path = path;
    res.lexical_score = 12.5;
    res.lexical_rank = 1;
    res.provenance = RetrievalProvenance::LexicalOnly;

    EvidenceAssemblerOptions opts;
    opts.source_context_lines = 1;

    auto bundle = assembler.assemble("payment service", {res}, opts);
    ASSERT_EQ(bundle.size(), 1u);

    const auto& item = bundle.items.front();
    EXPECT_EQ(item.primary_element().name, "PaymentService");
    EXPECT_EQ(item.primary_element().kind, ElementKind::Class);
    EXPECT_EQ(item.primary_element().detail, "class PaymentService");
    EXPECT_EQ(item.primary_result.lexical_score, 12.5);
    EXPECT_EQ(item.primary_result.lexical_rank, 1u);

    ASSERT_TRUE(item.has_source_excerpt());
    EXPECT_EQ(item.source_excerpt->start_line, 2u);  // 1 line of context before
    EXPECT_EQ(item.source_excerpt->end_line, 7u);    // 1 line of context after
    EXPECT_TRUE(item.source_excerpt->has_context_lines);
    EXPECT_NE(item.source_excerpt->text.find("#include <iostream>"), std::string::npos);
    EXPECT_NE(item.source_excerpt->text.find("// Footer"), std::string::npos);
}

// E & I. Supporting AST evidence attached vs absent
TEST_F(EvidenceAssemblerTest, SupportingASTEvidenceAttachedAndAbsent) {
    auto path = write_temp_file("calc.cpp", "int compute() {\n"
                                            "    helper();\n"
                                            "    logger();\n"
                                            "    return 42;\n"
                                            "}\n");

    ParsedFile pf{
        .file_path = path,
        .language = "cpp",
        .success = true,
        .has_syntax_errors = false,
        .elements = {
            CodeElement{
                .kind = ElementKind::Function,
                .name = "compute",
                .location = SourceRange{.start = {.line = 1, .column = 1, .byte_offset = 0},
                                        .end = {.line = 5, .column = 2, .byte_offset = 60}},
            },
            CodeElement{
                .kind = ElementKind::Call,
                .name = "helper",
                .location = SourceRange{.start = {.line = 2, .column = 5, .byte_offset = 20},
                                        .end = {.line = 2, .column = 13, .byte_offset = 28}},
                .parent_context = "compute",
            },
            CodeElement{
                .kind = ElementKind::Call,
                .name = "logger",
                .location = SourceRange{.start = {.line = 3, .column = 5, .byte_offset = 34},
                                        .end = {.line = 3, .column = 13, .byte_offset = 42}},
                .parent_context = "compute",
            },
        }};
    index_.add_parsed_file(pf);

    auto units = SupportingEvidenceResolver::resolve_units(pf);
    ASSERT_EQ(units.size(), 1u);
    ASSERT_EQ(units.front().supporting_elements.size(), 2u);

    RelationshipEvidenceResolver rel_resolver(graph_, index_);
    EvidenceAssembler assembler(rel_resolver);

    PrimarySearchResult res_with_supp;
    res_with_supp.unit = units.front();

    auto bundle = assembler.assemble("calc", {res_with_supp});
    ASSERT_EQ(bundle.size(), 1u);
    EXPECT_EQ(bundle.items.front().supporting_elements().size(), 2u);
    EXPECT_EQ(bundle.items.front().supporting_elements()[0].name, "helper");
    EXPECT_EQ(bundle.items.front().supporting_elements()[1].name, "logger");

    // Primary without supporting evidence
    PrimarySearchResult res_no_supp;
    res_no_supp.unit.primary_element = pf.elements.front();
    res_no_supp.unit.file_path = path;
    auto bundle2 = assembler.assemble("calc no supp", {res_no_supp});
    ASSERT_EQ(bundle2.size(), 1u);
    EXPECT_TRUE(bundle2.items.front().supporting_elements().empty());
}

// F, G, H, J. Relationship evidence attached, callers & callees, multiple kinds, and absent
TEST_F(EvidenceAssemblerTest, RelationshipEvidenceCallersCalleesAndKinds) {
    auto path = write_temp_file("graph_flow.cpp", "void logger() {}\n"
                                                  "void worker() { logger(); }\n"
                                                  "void controller() { worker(); }\n"
                                                  "class IWorker {};\n"
                                                  "class BaseWorker : public IWorker {};\n");

    auto id_log = add_element("logger", ElementKind::Function, path, 1, 1, 0, 16);
    auto id_work = add_element("worker", ElementKind::Function, path, 2, 2, 17, 44);
    auto id_ctrl = add_element("controller", ElementKind::Function, path, 3, 3, 45, 76);
    auto id_iface = add_element("IWorker", ElementKind::Interface, path, 4, 4, 77, 94);
    auto id_base = add_element("BaseWorker", ElementKind::Class, path, 5, 5, 95, 132);

    // worker calls logger (outgoing Calls)
    graph_.add_relationship(id_work, id_log, RelationshipKind::Calls);
    // controller calls worker (incoming Calls to worker)
    graph_.add_relationship(id_ctrl, id_work, RelationshipKind::Calls);
    // BaseWorker implements IWorker
    graph_.add_relationship(id_base, id_iface, RelationshipKind::Implements);

    RelationshipEvidenceResolver rel_resolver(graph_, index_);
    EvidenceAssembler assembler(rel_resolver);

    // Assemble worker: has 1 outgoing Call (logger) and 1 incoming Call (controller)
    PrimarySearchResult res_work;
    res_work.unit.primary_element_id = id_work;
    res_work.unit.primary_element = index_.get_element(id_work).element;
    res_work.unit.file_path = path;

    auto bundle_work = assembler.assemble("worker", {res_work});
    ASSERT_EQ(bundle_work.size(), 1u);
    const auto& rels_work = bundle_work.items.front().direct_relationships;
    ASSERT_EQ(rels_work.size(), 2u);

    EXPECT_EQ(rels_work[0].direction, RelationshipDirection::Outgoing);
    EXPECT_EQ(rels_work[0].kind, RelationshipKind::Calls);
    EXPECT_EQ(rels_work[0].related_name, "logger");

    EXPECT_EQ(rels_work[1].direction, RelationshipDirection::Incoming);
    EXPECT_EQ(rels_work[1].kind, RelationshipKind::Calls);
    EXPECT_EQ(rels_work[1].related_name, "controller");

    // Assemble BaseWorker: has 1 outgoing Implements
    PrimarySearchResult res_base;
    res_base.unit.primary_element_id = id_base;
    res_base.unit.primary_element = index_.get_element(id_base).element;
    res_base.unit.file_path = path;

    auto bundle_base = assembler.assemble("base worker", {res_base});
    ASSERT_EQ(bundle_base.size(), 1u);
    const auto& rels_base = bundle_base.items.front().direct_relationships;
    ASSERT_EQ(rels_base.size(), 1u);
    EXPECT_EQ(rels_base[0].direction, RelationshipDirection::Outgoing);
    EXPECT_EQ(rels_base[0].kind, RelationshipKind::Implements);
    EXPECT_EQ(rels_base[0].related_name, "IWorker");

    // Node without relationships
    PrimarySearchResult res_iface;
    res_iface.unit.primary_element_id = id_iface;
    res_iface.unit.primary_element = index_.get_element(id_iface).element;
    res_iface.unit.file_path = path;

    EvidenceAssemblerOptions no_rel_opts;
    no_rel_opts.include_relationships = false;
    auto bundle_no_rel = assembler.assemble("iface", {res_iface}, no_rel_opts);
    ASSERT_EQ(bundle_no_rel.size(), 1u);
    EXPECT_TRUE(bundle_no_rel.items.front().direct_relationships.empty());
}

// K & L. Missing source file and invalid source range handled safely
TEST_F(EvidenceAssemblerTest, MissingSourceFileAndInvalidRangeHandledSafely) {
    RelationshipEvidenceResolver rel_resolver(graph_, index_);
    EvidenceAssembler assembler(rel_resolver);

    // Missing file
    PrimarySearchResult res_missing;
    res_missing.unit.primary_element.name = "GhostFunction";
    res_missing.unit.primary_element.kind = ElementKind::Function;
    res_missing.unit.primary_element.location =
        SourceRange{.start = {.line = 1, .column = 1, .byte_offset = 0},
                    .end = {.line = 5, .column = 1, .byte_offset = 50}};
    res_missing.unit.file_path = test_dir_ / "does_not_exist.cpp";

    auto bundle_missing = assembler.assemble("ghost", {res_missing});
    ASSERT_EQ(bundle_missing.size(), 1u);
    EXPECT_FALSE(bundle_missing.items.front().has_source_excerpt());
    EXPECT_EQ(bundle_missing.items.front().primary_element().name, "GhostFunction");

    // Existing file with invalid range (e.g. beyond EOF)
    auto valid_file = write_temp_file("short.cpp", "int x = 1;\n");
    PrimarySearchResult res_bad_range;
    res_bad_range.unit.primary_element.name = "OutRange";
    res_bad_range.unit.primary_element.kind = ElementKind::Function;
    res_bad_range.unit.primary_element.location =
        SourceRange{.start = {.line = 100, .column = 1, .byte_offset = 1000},
                    .end = {.line = 110, .column = 1, .byte_offset = 2000}};
    res_bad_range.unit.file_path = valid_file;

    auto bundle_bad_range = assembler.assemble("bad range", {res_bad_range});
    ASSERT_EQ(bundle_bad_range.size(), 1u);
    EXPECT_FALSE(bundle_bad_range.items.front().has_source_excerpt());
    EXPECT_EQ(bundle_bad_range.items.front().primary_element().name, "OutRange");
}

// M. Unresolvable relationship target
TEST_F(EvidenceAssemblerTest, UnresolvableRelationshipTarget) {
    auto path = write_temp_file("caller.cpp", "void foo() {}\n");
    auto foo_id = add_element("foo", ElementKind::Function, path, 1, 1, 0, 13);

    // Edge to a target ID (99999) that does NOT exist in index
    graph_.add_relationship(foo_id, 99999, RelationshipKind::Calls);

    RelationshipEvidenceResolver rel_resolver(graph_, index_);
    EvidenceAssembler assembler(rel_resolver);

    PrimarySearchResult res;
    res.unit.primary_element_id = foo_id;
    res.unit.primary_element = index_.get_element(foo_id).element;
    res.unit.file_path = path;

    auto bundle = assembler.assemble("foo", {res});
    ASSERT_EQ(bundle.size(), 1u);
    ASSERT_EQ(bundle.items.front().direct_relationships.size(), 1u);
    const auto& rel = bundle.items.front().direct_relationships.front();
    EXPECT_EQ(rel.related_element_id, 99999u);
    EXPECT_TRUE(rel.related_name.empty());
}

// N. Empty retrieval result
TEST_F(EvidenceAssemblerTest, EmptyRetrievalResult) {
    RelationshipEvidenceResolver rel_resolver(graph_, index_);
    EvidenceAssembler assembler(rel_resolver);

    auto bundle = assembler.assemble("non-existent query", std::vector<PrimarySearchResult>{});
    EXPECT_EQ(bundle.query, "non-existent query");
    EXPECT_TRUE(bundle.empty());
    EXPECT_EQ(bundle.size(), 0u);
}

// O & P. Multiple files and cross-file relationships
TEST_F(EvidenceAssemblerTest, CrossFileRelationshipsAndMultipleFiles) {
    auto file_a = write_temp_file("module_a.cpp", "void functionA() {}\n");
    auto file_b = write_temp_file("module_b.cpp", "void functionB() { functionA(); }\n");

    auto id_a = add_element("functionA", ElementKind::Function, file_a, 1, 1, 0, 19);
    auto id_b = add_element("functionB", ElementKind::Function, file_b, 1, 1, 0, 34);

    // functionB in module_b calls functionA in module_a
    graph_.add_relationship(id_b, id_a, RelationshipKind::Calls);

    RelationshipEvidenceResolver rel_resolver(graph_, index_);
    EvidenceAssembler assembler(rel_resolver);

    PrimarySearchResult res_a;
    res_a.unit.primary_element_id = id_a;
    res_a.unit.primary_element = index_.get_element(id_a).element;
    res_a.unit.file_path = file_a;

    PrimarySearchResult res_b;
    res_b.unit.primary_element_id = id_b;
    res_b.unit.primary_element = index_.get_element(id_b).element;
    res_b.unit.file_path = file_b;

    auto bundle = assembler.assemble("cross file flow", {res_b, res_a});
    ASSERT_EQ(bundle.size(), 2u);

    // Check item B
    EXPECT_EQ(bundle.items[0].file_path(), file_b);
    ASSERT_EQ(bundle.items[0].direct_relationships.size(), 1u);
    EXPECT_EQ(bundle.items[0].direct_relationships[0].direction, RelationshipDirection::Outgoing);
    EXPECT_EQ(bundle.items[0].direct_relationships[0].related_name, "functionA");
    EXPECT_EQ(bundle.items[0].direct_relationships[0].related_file_path, file_a);

    // Check item A
    EXPECT_EQ(bundle.items[1].file_path(), file_a);
    ASSERT_EQ(bundle.items[1].direct_relationships.size(), 1u);
    EXPECT_EQ(bundle.items[1].direct_relationships[0].direction, RelationshipDirection::Incoming);
    EXPECT_EQ(bundle.items[1].direct_relationships[0].related_name, "functionB");
    EXPECT_EQ(bundle.items[1].direct_relationships[0].related_file_path, file_b);
}

// Q. Deterministic repeated assembly
TEST_F(EvidenceAssemblerTest, DeterministicRepeatedAssembly) {
    auto file_path = write_temp_file("repeat.cpp", "void alpha() {}\nvoid beta() {}\n");

    auto id1 = add_element("alpha", ElementKind::Function, file_path, 1, 1, 0, 15);
    auto id2 = add_element("beta", ElementKind::Function, file_path, 2, 2, 16, 30);

    graph_.add_relationship(id1, id2, RelationshipKind::Calls);

    RelationshipEvidenceResolver rel_resolver(graph_, index_);
    EvidenceAssembler assembler(rel_resolver);

    PrimarySearchResult res1;
    res1.unit.primary_element_id = id1;
    res1.unit.primary_element = index_.get_element(id1).element;
    res1.unit.file_path = file_path;
    res1.hybrid_score = 0.99;

    PrimarySearchResult res2;
    res2.unit.primary_element_id = id2;
    res2.unit.primary_element = index_.get_element(id2).element;
    res2.unit.file_path = file_path;
    res2.hybrid_score = 0.88;

    std::vector<PrimarySearchResult> results = {res1, res2};

    auto bundle_run1 = assembler.assemble("query repeat", results);
    auto bundle_run2 = assembler.assemble("query repeat", results);

    EXPECT_EQ(bundle_run1, bundle_run2);
}

// R. Retrieval scores and provenance preserved
TEST_F(EvidenceAssemblerTest, RetrievalScoresAndProvenancePreserved) {
    RelationshipEvidenceResolver rel_resolver(graph_, index_);
    EvidenceAssembler assembler(rel_resolver);

    PrimarySearchResult res;
    res.unit.primary_element.name = "scoredFunc";
    res.lexical_score = 4.2;
    res.normalized_lexical_score = 0.84;
    res.lexical_rank = 2;
    res.semantic_score = 0.91;
    res.normalized_semantic_score = 0.95;
    res.semantic_rank = 1;
    res.hybrid_score = 0.895;
    res.provenance = RetrievalProvenance::HybridBoth;

    auto bundle = assembler.assemble("score test", {res});
    ASSERT_EQ(bundle.size(), 1u);

    const auto& item = bundle.items.front();
    EXPECT_DOUBLE_EQ(item.primary_result.lexical_score, 4.2);
    EXPECT_DOUBLE_EQ(item.primary_result.normalized_lexical_score, 0.84);
    EXPECT_EQ(item.primary_result.lexical_rank, 2u);
    EXPECT_DOUBLE_EQ(item.primary_result.semantic_score, 0.91);
    EXPECT_DOUBLE_EQ(item.primary_result.normalized_semantic_score, 0.95);
    EXPECT_EQ(item.primary_result.semantic_rank, 1u);
    EXPECT_DOUBLE_EQ(item.primary_result.hybrid_score, 0.895);
    EXPECT_EQ(item.primary_result.provenance, RetrievalProvenance::HybridBoth);
}

// S. No graph traversal beyond one hop
TEST_F(EvidenceAssemblerTest, NoGraphTraversalBeyondOneHop) {
    auto path = write_temp_file("chain.cpp", "void A() {}\nvoid B() {}\nvoid C() {}\n");

    auto id_a = add_element("A", ElementKind::Function, path, 1, 1, 0, 11);
    auto id_b = add_element("B", ElementKind::Function, path, 2, 2, 12, 23);
    auto id_c = add_element("C", ElementKind::Function, path, 3, 3, 24, 35);

    // A -> B -> C
    graph_.add_relationship(id_a, id_b, RelationshipKind::Calls);
    graph_.add_relationship(id_b, id_c, RelationshipKind::Calls);

    RelationshipEvidenceResolver rel_resolver(graph_, index_);
    EvidenceAssembler assembler(rel_resolver);

    PrimarySearchResult res_a;
    res_a.unit.primary_element_id = id_a;
    res_a.unit.primary_element = index_.get_element(id_a).element;
    res_a.unit.file_path = path;

    auto bundle_a = assembler.assemble("A", {res_a});
    ASSERT_EQ(bundle_a.size(), 1u);
    ASSERT_EQ(bundle_a.items.front().direct_relationships.size(), 1u);
    // Directly calls B only; does NOT include C
    EXPECT_EQ(bundle_a.items.front().direct_relationships[0].related_name, "B");
}

// T. No mutation of input retrieval results
TEST_F(EvidenceAssemblerTest, NoMutationOfInputResults) {
    RelationshipEvidenceResolver rel_resolver(graph_, index_);
    EvidenceAssembler assembler(rel_resolver);

    PrimarySearchResult orig;
    orig.unit.primary_element.name = "immutable";
    orig.hybrid_score = 0.77;
    const PrimarySearchResult copy = orig;

    auto bundle = assembler.assemble("immutability", {orig});
    EXPECT_EQ(orig, copy);
}

}  // namespace
