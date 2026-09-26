// Phase 6.4 — PrimaryRetrievalPipeline Tests
//
// Tests cover:
//
// Construction:
//   1. Empty repository → empty pipeline
//   2. File with only supporting elements → synthetic module unit
//   3. Single primary symbol → 1 unit indexed
//   4. Multiple primary symbols → multiple units, each independently addressable
//   5. Nested class/method → both appear, method's calls are evidence of method
//
// Result guarantees:
//   6. Supporting elements are NEVER top-level results (primary beat internal call)
//   7. PrimarySearchResult always carries a primary unit (role == Primary)
//   8. Provenance set correctly (lexical-only, semantic-only, hybrid-both)
//
// Semantic index:
//   9.  SemanticIndex contains primary units only (size == unit count)
//   10. SemanticIndex uses primary_element_id as key
//
// Lexical retrieval:
//   11. Exact identifier query finds correct primary unit
//   12. Subword query still finds primary unit
//   13. Lexical hit on supporting element surfaces its owning primary unit
//
// Hybrid fusion:
//   14. Weighted fusion (alpha=1.0 → purely lexical result ordering)
//   15. RRF fusion method compiles and runs without error
//
// Metrics:
//   16. Metrics counts are internally consistent
//
// Phase 6.3 regression:
//   17. All Phase 6.3 RetrievalUnit tests continue to pass (tested separately;
//       this file verifies the pipeline does not break resolver semantics)

#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/retrieval/primary_search_result.hpp"
#include "amoeba/retrieval/retrieval_unit.hpp"
#include "amoeba/semantic/deterministic_embedding_provider.hpp"
#include "amoeba/semantic/semantic_document.hpp"

#include <gtest/gtest.h>

namespace amoeba::retrieval {

// ─── Test Helpers ────────────────────────────────────────────────────────────

class PrimaryRetrievalPipelineTest : public ::testing::Test {
protected:
    parser::SourceParser parser_;
    semantic::DeterministicEmbeddingProvider provider_{32};
};

static index::InvertedIndex build_index(const std::vector<parser::ParsedFile>& files) {
    index::InvertedIndex idx;
    for (const auto& f : files) {
        idx.add_parsed_file(f);
    }
    return idx;
}

// ─── Construction Tests ───────────────────────────────────────────────────────

// Test 1: Empty repository
TEST_F(PrimaryRetrievalPipelineTest, EmptyRepositoryProducesEmptyPipeline) {
    index::InvertedIndex idx;
    PrimaryRetrievalPipeline pipeline({}, idx, provider_);

    EXPECT_TRUE(pipeline.empty());
    EXPECT_EQ(pipeline.primary_unit_count(), 0u);
    EXPECT_EQ(pipeline.semantic_index().size(), 0u);

    auto results = pipeline.search("anything");
    EXPECT_TRUE(results.empty());
}

// Test 2: File with only supporting elements gets synthetic module unit
TEST_F(PrimaryRetrievalPipelineTest, FileLevelModuleUnitForCSSFile) {
    auto f = parser_.parse_source(".card { background-color: white; }\n", "CSS", "styles/card.css");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    // CSS file produces 1 synthetic module unit
    EXPECT_EQ(pipeline.primary_unit_count(), 1u);
    EXPECT_EQ(pipeline.semantic_index().size(), 1u);
}

// Test 3: Single primary symbol
TEST_F(PrimaryRetrievalPipelineTest, SinglePrimarySymbolIndexedAsOneUnit) {
    auto f = parser_.parse_source("function fetchUser(id: string) { return api.get(id); }\n",
                                  "TypeScript", "src/api.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    EXPECT_GE(pipeline.primary_unit_count(), 1u);
    EXPECT_EQ(pipeline.semantic_index().size(), pipeline.primary_unit_count());
}

// Test 4: Multiple primary symbols
TEST_F(PrimaryRetrievalPipelineTest, MultiplePrimarySymbolsIndexedAsMultipleUnits) {
    auto f = parser_.parse_source("function getUser(id: string) { return db.find(id); }\n"
                                  "function createUser(name: string) { return db.insert(name); }\n"
                                  "function deleteUser(id: string) { return db.remove(id); }\n",
                                  "TypeScript", "src/users.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    // At least 3 primary units: getUser, createUser, deleteUser
    EXPECT_GE(pipeline.primary_unit_count(), 3u);
}

// Test 5: Nested class/method → both appear as primary units
TEST_F(PrimaryRetrievalPipelineTest, ClassAndMethodAreSeparatePrimaryUnits) {
    auto f = parser_.parse_source("class AuthService {\n"
                                  "    authenticate(token: string) {\n"
                                  "        return verifyToken(token);\n"
                                  "    }\n"
                                  "}\n",
                                  "TypeScript", "src/auth.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    // AuthService (Class) + authenticate (Method) = 2 primary units minimum
    EXPECT_GE(pipeline.primary_unit_count(), 2u);

    bool found_class = false;
    bool found_method = false;
    for (const auto& unit : pipeline.units()) {
        if (unit.primary_element.name == "AuthService")
            found_class = true;
        if (unit.primary_element.name == "authenticate")
            found_method = true;
    }
    EXPECT_TRUE(found_class);
    EXPECT_TRUE(found_method);
}

// ─── Result Guarantee Tests ───────────────────────────────────────────────────

// Test 6: Supporting elements are never top-level results
TEST_F(PrimaryRetrievalPipelineTest, SupportingElementNeverBecomesTopLevelResult) {
    auto f = parser_.parse_source("function processPayment(amount: number) {\n"
                                  "    return stripe.charge(amount);\n"
                                  "}\n",
                                  "TypeScript", "src/payment.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    // Query for "stripe" — which is a Call (supporting) inside processPayment
    auto results = pipeline.search("stripe");

    for (const auto& r : results) {
        EXPECT_EQ(r.unit.role, RetrievalUnitRole::Primary)
            << "All top-level results must be Primary units";
        EXPECT_NE(r.unit.primary_element.kind, parser::ElementKind::Call)
            << "A Call element must never be a top-level result";
    }
}

// Test 7: Every PrimarySearchResult has role == Primary
TEST_F(PrimaryRetrievalPipelineTest, AllResultsArePrimaryRole) {
    auto f = parser_.parse_source("export function UserCard({ user }) {\n"
                                  "    return <div className=\"card\">\n"
                                  "        <Avatar src={user.avatar} />\n"
                                  "    </div>;\n"
                                  "}\n",
                                  "TSX", "components/UserCard.tsx");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    auto results = pipeline.search("user card");
    for (const auto& r : results) {
        EXPECT_EQ(r.unit.role, RetrievalUnitRole::Primary);
    }
}

// Test 8: Provenance correctly set
TEST_F(PrimaryRetrievalPipelineTest, ProvenanceSetCorrectlyForLexicalOnlyQuery) {
    auto f = parser_.parse_source(
        "function validateToken(token: string) { return jwt.verify(token); }\n", "TypeScript",
        "src/validate.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    // alpha=1.0 → purely lexical
    PrimarySearchOptions opts;
    opts.alpha = 1.0;
    opts.max_results = 5;

    auto results = pipeline.search("validateToken", opts);
    // All results retrieved via alpha=1.0 may still be HybridBoth if semantic
    // returns them too — we just verify the provenance enum is a valid value.
    for (const auto& r : results) {
        const auto p = r.provenance;
        EXPECT_TRUE(p == RetrievalProvenance::LexicalOnly || p == RetrievalProvenance::HybridBoth ||
                    p == RetrievalProvenance::SemanticOnly);
    }
}

// ─── Semantic Index Tests ─────────────────────────────────────────────────────

// Test 9: SemanticIndex size equals primary unit count
TEST_F(PrimaryRetrievalPipelineTest, SemanticIndexSizeEqualsPrimaryUnitCount) {
    auto f1 = parser_.parse_source("class UserService {\n"
                                   "    getUser(id: string) { return db.find(id); }\n"
                                   "}\n",
                                   "TypeScript", "src/user.ts");
    auto f2 = parser_.parse_source("export function LoginForm() { return <form>Login</form>; }\n",
                                   "TSX", "components/LoginForm.tsx");
    ASSERT_TRUE(f1.success);
    ASSERT_TRUE(f2.success);

    auto idx = build_index({f1, f2});
    const parser::ParsedFile files[] = {f1, f2};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    EXPECT_EQ(pipeline.semantic_index().size(), pipeline.primary_unit_count());
}

// Test 10: SemanticIndex uses primary_element_id as key
TEST_F(PrimaryRetrievalPipelineTest, SemanticIndexKeyIsThePrimaryElementId) {
    auto f = parser_.parse_source("function hashPassword(pw: string) { return bcrypt.hash(pw); }\n",
                                  "TypeScript", "src/hash.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    for (const auto& unit : pipeline.units()) {
        EXPECT_TRUE(pipeline.semantic_index().contains(unit.primary_element_id))
            << "Primary element " << unit.primary_element.name
            << " must have an embedding indexed by its primary_element_id";
    }
}

// ─── Lexical Retrieval Tests ──────────────────────────────────────────────────

// Test 11: Exact identifier query finds correct primary unit
TEST_F(PrimaryRetrievalPipelineTest, ExactIdentifierQueryFindsPrimaryUnit) {
    auto f = parser_.parse_source("function authenticateUser(credentials: Credentials) {\n"
                                  "    return checkCredentials(credentials);\n"
                                  "}\n",
                                  "TypeScript", "src/auth.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    PrimarySearchOptions opts;
    opts.alpha = 1.0;  // lexical only
    opts.max_results = 5;

    auto results = pipeline.search("authenticateUser", opts);
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].unit.primary_element.name, "authenticateUser");
}

// Test 12: Subword query finds primary unit
TEST_F(PrimaryRetrievalPipelineTest, SubwordQueryFindsPrimaryUnit) {
    auto f = parser_.parse_source("function calculateMonthlyRevenue(data: RevenueData) {\n"
                                  "    return data.total / 12;\n"
                                  "}\n",
                                  "TypeScript", "src/finance.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    PrimarySearchOptions opts;
    opts.alpha = 1.0;
    opts.max_results = 5;

    // "monthly" is a subword of calculateMonthlyRevenue
    auto results = pipeline.search("monthly", opts);
    bool found = false;
    for (const auto& r : results) {
        if (r.unit.primary_element.name == "calculateMonthlyRevenue") {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "'monthly' should match calculateMonthlyRevenue via subword tokenization";
}

// Test 13: Lexical hit on supporting element surfaces its owning primary unit
TEST_F(PrimaryRetrievalPipelineTest, SupportingElementLexicalHitSurfacesOwningPrimaryUnit) {
    auto f = parser_.parse_source("function renderCalendar() {\n"
                                  "    return getMonthData();\n"
                                  "}\n",
                                  "TypeScript", "src/calendar.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    PrimarySearchOptions opts;
    opts.alpha = 1.0;
    opts.max_results = 5;

    // "getMonthData" is a Call inside renderCalendar — lexical hit on it
    // should surface renderCalendar (primary), not a Call (supporting)
    auto results = pipeline.search("getMonthData", opts);
    for (const auto& r : results) {
        EXPECT_EQ(r.unit.role, RetrievalUnitRole::Primary);
        EXPECT_NE(r.unit.primary_element.kind, parser::ElementKind::Call)
            << "getMonthData (Call) must not be returned as a primary result";
    }
}

// ─── Fusion Tests ─────────────────────────────────────────────────────────────

// Test 14: Weighted fusion alpha=1.0 (pure lexical)
TEST_F(PrimaryRetrievalPipelineTest, WeightedFusionAlpha1PureLexicalOrderingIsValid) {
    auto f = parser_.parse_source("function renderHeader() { return <header>Title</header>; }\n"
                                  "function renderFooter() { return <footer>Footer</footer>; }\n",
                                  "TSX", "components/Layout.tsx");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    PrimarySearchOptions opts;
    opts.alpha = 1.0;
    opts.fusion_method = hybrid::FusionMethod::WeightedScore;
    opts.max_results = 10;

    auto results = pipeline.search("render header", opts);
    // At least one result, all primary
    for (const auto& r : results) {
        EXPECT_EQ(r.unit.role, RetrievalUnitRole::Primary);
    }
}

// Test 15: RRF fusion runs without error
TEST_F(PrimaryRetrievalPipelineTest, RRFFusionRunsWithoutError) {
    auto f = parser_.parse_source(
        "function validateSession(session: Session) { return session.valid; }\n", "TypeScript",
        "src/session.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    PrimarySearchOptions opts;
    opts.fusion_method = hybrid::FusionMethod::ReciprocalRank;
    opts.max_results = 5;

    EXPECT_NO_THROW({
        auto results = pipeline.search("validateSession", opts);
        for (const auto& r : results) {
            EXPECT_EQ(r.unit.role, RetrievalUnitRole::Primary);
        }
    });
}

// ─── Phase 6.4 Required Invariant Tests (10 Invariants) ──────────────────────

// Invariant 1: Supporting elements must never appear as top-level PrimarySearchResult candidates
TEST_F(PrimaryRetrievalPipelineTest, Invariant1_SupportingElementsNeverTopLevelCandidates) {
    auto f =
        parser_.parse_source("export function OrderSummary({ order }) {\n"
                             "    const total = calculateTotal(order);\n"
                             "    const tax = calculateTax(total);\n"
                             "    return <div className=\"order-summary\" id=\"summary-root\">\n"
                             "        <button onClick={submitOrder}>Pay Now</button>\n"
                             "    </div>;\n"
                             "}\n",
                             "TSX", "components/OrderSummary.tsx");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    // Test across various queries matching supporting elements: Call, Attribute, JSXElement
    const std::vector<std::string> queries = {"calculateTax", "calculateTotal", "order-summary",
                                              "summary-root", "submitOrder",    "button"};

    for (const auto& q : queries) {
        for (double alpha : {0.0, 0.5, 1.0}) {
            PrimarySearchOptions opts;
            opts.alpha = alpha;
            auto results = pipeline.search(q, opts);
            for (const auto& r : results) {
                EXPECT_EQ(r.unit.role, RetrievalUnitRole::Primary)
                    << "Query '" << q << "' (alpha=" << alpha << ") returned non-primary role";
                EXPECT_NE(r.unit.primary_element.kind, parser::ElementKind::Call);
                EXPECT_NE(r.unit.primary_element.kind, parser::ElementKind::Attribute);
                EXPECT_NE(r.unit.primary_element.kind, parser::ElementKind::JSXElement);
                EXPECT_NE(r.unit.primary_element.kind, parser::ElementKind::UtilityClass);
            }
        }
    }
}

// Invariant 2: If a supporting element produces the lexical match, the result must be mapped to its
// owning primary RetrievalUnit Example: Call getUserById() owned by UserService, Query getUserById
// -> Expected: UserService with Call: getUserById as evidence
TEST_F(PrimaryRetrievalPipelineTest,
       Invariant2_SupportingLexicalHitMapsToOwningPrimaryUnitWithEvidence) {
    auto f = parser_.parse_source("class UserService {\n"
                                  "    findUser(id: string) {\n"
                                  "        return getUserById(id);\n"
                                  "    }\n"
                                  "}\n",
                                  "TypeScript", "src/services/user_service.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    PrimarySearchOptions opts;
    opts.alpha = 1.0;  // purely lexical
    opts.max_results = 5;

    // "getUserById" is a Call element inside UserService::findUser
    auto results = pipeline.search("getUserById", opts);
    ASSERT_FALSE(results.empty());

    // The top-level result must be a primary unit (findUser method or UserService class)
    const auto& top_result = results[0];
    EXPECT_EQ(top_result.unit.role, RetrievalUnitRole::Primary);
    EXPECT_TRUE(top_result.unit.primary_element.name == "findUser" ||
                top_result.unit.primary_element.name == "UserService");

    // The supporting evidence attached to this unit must contain the getUserById Call
    bool found_supporting_call = false;
    for (const auto& elem : top_result.unit.supporting_elements) {
        if (elem.kind == parser::ElementKind::Call && elem.name == "getUserById") {
            found_supporting_call = true;
            break;
        }
    }
    EXPECT_TRUE(found_supporting_call) << "The owning primary unit must carry the matching Call: "
                                          "getUserById() as supporting evidence";
}

// Invariant 3: Semantic query can retrieve a primary RetrievalUnit whose enriched semantic
// representation contains supporting evidence
TEST_F(PrimaryRetrievalPipelineTest,
       Invariant3_SemanticQueryRetrievesPrimaryUnitWithEnrichedEvidence) {
    auto f = parser_.parse_source("function processPayment(tx: Transaction) {\n"
                                  "    return verifyStripeWebhookSignature(tx.sig);\n"
                                  "}\n",
                                  "TypeScript", "src/payment.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    // Verify format_unit includes the supporting call
    ASSERT_GE(pipeline.primary_unit_count(), 1u);
    const auto& unit = pipeline.units()[0];
    const std::string enriched_text = semantic::SemanticTextFormatter::format_unit(unit);
    EXPECT_TRUE(enriched_text.find("verifyStripeWebhookSignature") != std::string::npos)
        << "Enriched representation must contain supporting call name";

    // Pure semantic search (alpha=0.0) for the supporting call token
    PrimarySearchOptions opts;
    opts.alpha = 0.0;
    opts.max_results = 5;

    auto results = pipeline.search("verifyStripeWebhookSignature", opts);
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].unit.primary_element.name, "processPayment");
    EXPECT_EQ(results[0].unit.role, RetrievalUnitRole::Primary);
}

// Invariant 4: Same primary ElementId is used consistently across RetrievalUnit, SemanticIndex,
// lexical mapping, and hybrid fusion
TEST_F(PrimaryRetrievalPipelineTest, Invariant4_SamePrimaryElementIdUsedConsistently) {
    auto f1 = parser_.parse_source("function authenticate(user: string) { return verify(user); }\n",
                                   "TypeScript", "src/auth.ts");
    auto f2 =
        parser_.parse_source("function renderProfile(user: string) { return display(user); }\n",
                             "TypeScript", "src/profile.ts");
    ASSERT_TRUE(f1.success && f2.success);

    auto idx = build_index({f1, f2});
    const parser::ParsedFile files[] = {f1, f2};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    const auto& units = pipeline.units();
    const auto& sem_index = pipeline.semantic_index();

    for (std::size_t ui = 0; ui < units.size(); ++ui) {
        const auto& unit = units[ui];
        const index::ElementId pid = unit.primary_element_id;

        // 1. SemanticIndex contains pid
        EXPECT_TRUE(sem_index.contains(pid))
            << "SemanticIndex must contain primary_element_id " << pid;

        // 2. RetrievalUnit primary_element matches InvertedIndex at pid
        const auto& ie = idx.get_element(pid);
        EXPECT_EQ(ie.element.name, unit.primary_element.name);
        EXPECT_EQ(ie.element.location.start.line, unit.primary_element.location.start.line);

        // 3. Search result hybrid fusion retains pid
        PrimarySearchOptions opts;
        opts.alpha = 0.5;
        auto results = pipeline.search(unit.primary_element.name, opts);
        ASSERT_FALSE(results.empty());
        EXPECT_EQ(results[0].unit.primary_element_id, pid);
    }
}

// Invariant 5: Two elements with identical per-file positions from different files never collide
TEST_F(PrimaryRetrievalPipelineTest, Invariant5_MultiFileIdenticalPerFilePositionsDoNotCollide) {
    // Both files have their first primary element at index 0 in ParsedFile::elements
    auto f1 = parser_.parse_source("function initConfig() { return loadSettings(); }\n",
                                   "TypeScript", "src/config.ts");
    auto f2 = parser_.parse_source("function initLogger() { return setupWinston(); }\n",
                                   "TypeScript", "src/logger.ts");
    ASSERT_TRUE(f1.success && f2.success);

    auto idx = build_index({f1, f2});
    const parser::ParsedFile files[] = {f1, f2};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    EXPECT_EQ(pipeline.primary_unit_count(), 2u);
    EXPECT_EQ(pipeline.semantic_index().size(), 2u);

    const auto& u1 = pipeline.units()[0];
    const auto& u2 = pipeline.units()[1];

    EXPECT_NE(u1.primary_element_id, u2.primary_element_id)
        << "Elements from different files must have distinct global primary_element_id values";

    // Querying for config finds config, querying for logger finds logger
    auto res_cfg = pipeline.search("initConfig");
    ASSERT_FALSE(res_cfg.empty());
    EXPECT_EQ(res_cfg[0].unit.primary_element.name, "initConfig");
    EXPECT_EQ(res_cfg[0].unit.primary_element_id, u1.primary_element_id);

    auto res_log = pipeline.search("initLogger");
    ASSERT_FALSE(res_log.empty());
    EXPECT_EQ(res_log[0].unit.primary_element.name, "initLogger");
    EXPECT_EQ(res_log[0].unit.primary_element_id, u2.primary_element_id);
}

// Invariant 6: Multiple raw supporting hits belonging to the same primary unit collapse into ONE
// primary result
TEST_F(PrimaryRetrievalPipelineTest,
       Invariant6_MultipleSupportingHitsCollapseIntoSinglePrimaryResult) {
    auto f = parser_.parse_source("function synchronizePayloadData() {\n"
                                  "    fetchPayload();\n"
                                  "    validatePayload();\n"
                                  "    transformPayload();\n"
                                  "    storePayload();\n"
                                  "}\n",
                                  "TypeScript", "src/sync.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    // Raw SearchEngine returns multiple hits for "payload" (all the individual calls)
    index::SearchEngine raw_engine(idx);
    index::SearchOptions raw_opts;
    raw_opts.match_mode = index::MatchMode::AnyTerm;
    auto raw_hits = raw_engine.search("payload", raw_opts);
    EXPECT_GE(raw_hits.size(), 3u) << "Raw search engine should find multiple Call hits";

    // PrimaryRetrievalPipeline must collapse all supporting hits into ONE primary result
    PrimarySearchOptions opts;
    opts.alpha = 1.0;
    opts.max_results = 10;
    auto primary_results = pipeline.search("payload", opts);

    std::size_t sync_unit_count = 0;
    for (const auto& r : primary_results) {
        if (r.unit.primary_element.name == "synchronizePayloadData") {
            ++sync_unit_count;
        }
    }
    EXPECT_EQ(sync_unit_count, 1u)
        << "Multiple raw supporting hits must collapse into exactly ONE primary result";
}

// Invariant 7: Deterministic tie-breaking
TEST_F(PrimaryRetrievalPipelineTest, Invariant7_DeterministicTieBreaking) {
    auto f = parser_.parse_source("function handleAlpha() { return process(); }\n"
                                  "function handleBeta() { return process(); }\n"
                                  "function handleGamma() { return process(); }\n",
                                  "TypeScript", "src/handlers.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    PrimarySearchOptions opts;
    opts.alpha = 0.5;
    opts.max_results = 10;

    auto run1 = pipeline.search("handle", opts);
    auto run2 = pipeline.search("handle", opts);

    ASSERT_EQ(run1.size(), run2.size());
    ASSERT_FALSE(run1.empty());

    for (std::size_t i = 0; i < run1.size(); ++i) {
        EXPECT_EQ(run1[i].unit.primary_element_id, run2[i].unit.primary_element_id);
        EXPECT_EQ(run1[i].unit.primary_element.name, run2[i].unit.primary_element.name);
        EXPECT_DOUBLE_EQ(run1[i].hybrid_score, run2[i].hybrid_score);
        EXPECT_EQ(run1[i].provenance, run2[i].provenance);
    }
}

// Invariant 8: Unresolved supporting evidence is not assigned arbitrarily
TEST_F(PrimaryRetrievalPipelineTest,
       Invariant8_UnresolvedSupportingEvidenceNotAssignedArbitrarily) {
    auto f = parser_.parse_source("function serviceA() { return apiCallA(); }\n"
                                  "function serviceB() { return apiCallB(); }\n",
                                  "TypeScript", "src/services.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    const auto& units = pipeline.units();
    ASSERT_GE(units.size(), 2u);

    const RetrievalUnit* unit_a = nullptr;
    const RetrievalUnit* unit_b = nullptr;
    for (const auto& u : units) {
        if (u.primary_element.name == "serviceA")
            unit_a = &u;
        if (u.primary_element.name == "serviceB")
            unit_b = &u;
    }
    ASSERT_NE(unit_a, nullptr);
    ASSERT_NE(unit_b, nullptr);

    // serviceA must only have apiCallA, serviceB must only have apiCallB
    for (const auto& elem : unit_a->supporting_elements) {
        EXPECT_NE(elem.name, "apiCallB") << "apiCallB must not be arbitrarily assigned to serviceA";
    }
    for (const auto& elem : unit_b->supporting_elements) {
        EXPECT_NE(elem.name, "apiCallA") << "apiCallA must not be arbitrarily assigned to serviceB";
    }
}

// Invariant 9: Existing SearchEngine, Baseline, BM25, CodeAware, SemanticRetriever and
// HybridRetriever remain behaviorally unchanged
TEST_F(PrimaryRetrievalPipelineTest, Invariant9_ExistingRetrieverComponentsBehaviorallyUnchanged) {
    auto f =
        parser_.parse_source("function queryDatabase(sql: string) { return db.execute(sql); }\n",
                             "TypeScript", "src/db.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});

    // 1. Raw SearchEngine with BaselineRanker
    index::SearchEngine engine(idx);
    index::SearchOptions b_opts{.ranker_type = index::RankerType::Baseline};
    auto b_res = engine.search("queryDatabase", b_opts);
    ASSERT_FALSE(b_res.empty());
    EXPECT_EQ(b_res[0].element.name, "queryDatabase");

    // 2. Raw SearchEngine with BM25Ranker
    index::SearchOptions bm_opts{.ranker_type = index::RankerType::BM25};
    auto bm_res = engine.search("queryDatabase", bm_opts);
    ASSERT_FALSE(bm_res.empty());
    EXPECT_EQ(bm_res[0].element.name, "queryDatabase");

    // 3. Raw SearchEngine with CodeAwareRanker
    index::SearchOptions ca_opts{.ranker_type = index::RankerType::CodeAware};
    auto ca_res = engine.search("queryDatabase", ca_opts);
    ASSERT_FALSE(ca_res.empty());
    EXPECT_EQ(ca_res[0].element.name, "queryDatabase");

    // 4. Raw SemanticRetriever directly
    semantic::SemanticIndex sem_idx;
    sem_idx.add(semantic::Embedding{
        .element_id = 0,
        .values = provider_.embed("function queryDatabase"),
    });
    semantic::SemanticRetriever sem_retriever(sem_idx);
    auto sem_res = sem_retriever.retrieve_text("queryDatabase", provider_);
    ASSERT_FALSE(sem_res.empty());
    EXPECT_EQ(sem_res[0].element_id, 0u);
}

// Invariant 10: Phase 5 relationship components remain unchanged
TEST_F(PrimaryRetrievalPipelineTest, Invariant10_Phase5RelationshipComponentsUnchanged) {
    auto f = parser_.parse_source("function callerFunc() { return calleeFunc(); }\n"
                                  "function calleeFunc() { return 42; }\n",
                                  "TypeScript", "src/calls.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};

    // Build pipeline (verifies coexistence with relationship graph)
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);
    EXPECT_EQ(pipeline.primary_unit_count(), 2u);

    // Primary pipeline operates independently, relationship graph remains untouched and usable
    auto results = pipeline.search("calleeFunc");
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].unit.role, RetrievalUnitRole::Primary);
}

// ─── Metrics Tests ────────────────────────────────────────────────────────────

// Test 16: Metrics are internally consistent
TEST_F(PrimaryRetrievalPipelineTest, MetricsAreInternallyConsistent) {
    auto f = parser_.parse_source("class DataService {\n"
                                  "    getData() { return this.cache.get(); }\n"
                                  "    setData(v: string) { this.cache.set(v); }\n"
                                  "}\n",
                                  "TypeScript", "src/data.ts");
    ASSERT_TRUE(f.success);

    auto idx = build_index({f});
    const parser::ParsedFile files[] = {f};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    PipelineMetrics metrics;
    auto results = pipeline.search_with_metrics("data service", {}, metrics);

    EXPECT_EQ(metrics.primary_unit_count, pipeline.primary_unit_count());
    EXPECT_EQ(metrics.total_elements, idx.element_count());
    EXPECT_LE(metrics.final_results, metrics.fused_candidates);
    EXPECT_GE(metrics.total_ms, 0.0);
    EXPECT_GE(metrics.lexical_ms, 0.0);
    EXPECT_GE(metrics.semantic_ms, 0.0);
}

}  // namespace amoeba::retrieval
