#include "amoeba/evidence/evidence_bundle.hpp"
#include "amoeba/evidence/evidence_sufficiency.hpp"
#include "amoeba/evidence/semantic_evidence_support.hpp"
#include "amoeba/retrieval/query_understanding.hpp"
#include "amoeba/semantic/deterministic_embedding_provider.hpp"
#include "amoeba/semantic/pretrained_embedding_provider.hpp"
#include "amoeba/semantic/semantic_index.hpp"

#include <gtest/gtest.h>

namespace amoeba::evidence::test {

namespace {

EvidenceItem
create_item(index::ElementId id, std::string name, std::string file, parser::ElementKind kind,
            double score,
            retrieval::RetrievalProvenance prov = retrieval::RetrievalProvenance::HybridBoth,
            std::string excerpt_text = "", std::string detail = "") {
    EvidenceItem item;
    item.primary_result.unit.primary_element_id = id;
    item.primary_result.unit.primary_element.name = name;
    item.primary_result.unit.primary_element.kind = kind;
    item.primary_result.unit.primary_element.detail = detail;
    item.primary_result.unit.file_path = file;
    item.primary_result.unit.language = "TypeScript";
    item.primary_result.hybrid_score = score;
    item.primary_result.provenance = prov;

    if (!excerpt_text.empty()) {
        source::SourceExcerpt excerpt;
        excerpt.text = excerpt_text;
        excerpt.file_path = file;
        excerpt.start_line = 1;
        excerpt.end_line = 10;
        item.source_excerpt = excerpt;
    }

    return item;
}

}  // namespace

class SemanticEvidenceSupportTest : public ::testing::Test {
protected:
    semantic::PretrainedEmbeddingProvider provider;
};

// 1. Conceptual positive vocabulary gap: "authentication" supported by LoginService &
// SessionManager
TEST_F(SemanticEvidenceSupportTest, ConceptualVocabularyGapAuthenticationSupported) {
    EvidenceBundle bundle;
    bundle.query = "How does the application authenticate users?";
    bundle.items.push_back(create_item(1, "LoginService", "src/auth/login_service.ts",
                                       parser::ElementKind::Class, 0.82,
                                       retrieval::RetrievalProvenance::SemanticOnly,
                                       "export class LoginService { login(user, pass) {} }",
                                       "handles user login and credential verification"));
    bundle.items.push_back(create_item(2, "SessionManager", "src/auth/session_manager.ts",
                                       parser::ElementKind::Class, 0.78,
                                       retrieval::RetrievalProvenance::SemanticOnly,
                                       "export class SessionManager { createSession(userId) {} }",
                                       "manages user authentication tokens and active sessions"));

    const auto query_rep = retrieval::QueryUnderstanding::analyze(bundle.query);
    SemanticEvidenceSupport sem_support(provider);
    const auto sem_result = sem_support.evaluate(query_rep, bundle);

    const auto* auth_sup = sem_result.find_concept("authenticate");
    ASSERT_NE(auth_sup, nullptr);
    EXPECT_TRUE(auth_sup->is_supported());
    EXPECT_GE(auth_sup->coherent_support_score, 0.60f);

    // Integrated sufficiency evaluation
    const auto res = EvidenceSufficiencyChecker::check(query_rep, bundle, sem_result);
    EXPECT_TRUE(res.is_sufficient);
    EXPECT_FALSE(res.matched_subjects.empty());
}

// 2. Isolated semantic candidate: RBAC against generic search engine is rejected
TEST_F(SemanticEvidenceSupportTest, IsolatedSemanticCandidateRBACIsInsufficient) {
    EvidenceBundle bundle;
    bundle.query = "Where is RBAC implemented?";
    bundle.items.push_back(create_item(
        10, "RelationshipAwareSearchEngine", "src/search/engine.cpp", parser::ElementKind::Class,
        0.72, retrieval::RetrievalProvenance::SemanticOnly,
        "// Implemented by SearchEngine class\nvoid search() {}", "general graph search engine"));

    const auto query_rep = retrieval::QueryUnderstanding::analyze(bundle.query);
    SemanticEvidenceSupport sem_support(provider);
    const auto sem_result = sem_support.evaluate(query_rep, bundle);

    const auto* rbac_sup = sem_result.find_concept("rbac");
    ASSERT_NE(rbac_sup, nullptr);
    EXPECT_FALSE(rbac_sup->is_supported());

    const auto res = EvidenceSufficiencyChecker::check(query_rep, bundle, sem_result);
    EXPECT_FALSE(res.is_sufficient);
    EXPECT_TRUE(res.matched_subjects.empty());
    EXPECT_FALSE(res.missing_subjects.empty());
    EXPECT_EQ(res.missing_subjects[0], "rbac");
}

// 3. Exact lexical presence is preserved and gives 1.0 support
TEST_F(SemanticEvidenceSupportTest, LexicalSupportPreserved) {
    EvidenceBundle bundle;
    bundle.query = "Where is calendar state managed?";
    bundle.items.push_back(create_item(
        20, "CalendarDay", "src/components/CalendarDay.tsx", parser::ElementKind::Function, 0.85,
        retrieval::RetrievalProvenance::HybridBoth, "export const CalendarDay = () => <div />;",
        "calendar day rendering component"));

    const auto query_rep = retrieval::QueryUnderstanding::analyze(bundle.query);
    SemanticEvidenceSupport sem_support(provider);
    const auto sem_result = sem_support.evaluate(query_rep, bundle);

    const auto* cal_sup = sem_result.find_concept("calendar");
    ASSERT_NE(cal_sup, nullptr);
    EXPECT_TRUE(cal_sup->has_lexical_support);
    EXPECT_TRUE(cal_sup->is_supported());
    EXPECT_FLOAT_EQ(cal_sup->coherent_support_score, 1.0f);

    const auto res = EvidenceSufficiencyChecker::check(query_rep, bundle, sem_result);
    EXPECT_TRUE(res.is_sufficient);
}

// 4. Multiple coherent candidates provide stronger support than one isolated candidate
TEST_F(SemanticEvidenceSupportTest, MultiCandidateReinforcement) {
    EvidenceBundle bundle_multi;
    bundle_multi.query = "How is state persistence handled?";
    bundle_multi.items.push_back(create_item(30, "useLocalStorage", "src/hooks/useLocalStorage.ts",
                                             parser::ElementKind::Function, 0.80,
                                             retrieval::RetrievalProvenance::SemanticOnly,
                                             "export function useLocalStorage(key, val) {}",
                                             "persists and stores state in browser storage"));
    bundle_multi.items.push_back(create_item(
        31, "SaveManager", "src/storage/save_manager.ts", parser::ElementKind::Class, 0.76,
        retrieval::RetrievalProvenance::SemanticOnly,
        "export class SaveManager { saveToDisk() {} }", "disk storage persistence manager"));

    const auto query_rep = retrieval::QueryUnderstanding::analyze(bundle_multi.query);
    SemanticEvidenceSupport sem_support(provider);
    const auto sem_result = sem_support.evaluate(query_rep, bundle_multi);

    const auto* persist_sup = sem_result.find_concept("persistence");
    ASSERT_NE(persist_sup, nullptr);
    EXPECT_TRUE(persist_sup->is_supported());
    EXPECT_GE(persist_sup->supporting_candidate_count, 1u);
}

// 5. Deliberately unsupported concept remains insufficient
TEST_F(SemanticEvidenceSupportTest, UnsupportedConceptRemainsInsufficient) {
    EvidenceBundle bundle;
    bundle.query = "Where is crypto blockchain mining executed?";
    bundle.items.push_back(create_item(
        40, "GeneralUtility", "src/utils/general.ts", parser::ElementKind::Function, 0.60,
        retrieval::RetrievalProvenance::SemanticOnly, "function helper() { return 42; }"));

    const auto query_rep = retrieval::QueryUnderstanding::analyze(bundle.query);
    SemanticEvidenceSupport sem_support(provider);
    const auto sem_result = sem_support.evaluate(query_rep, bundle);

    const auto res = EvidenceSufficiencyChecker::check(query_rep, bundle, sem_result);
    EXPECT_FALSE(res.is_sufficient);
}

// 6. Determinism: Repeated evaluations produce identical results
TEST_F(SemanticEvidenceSupportTest, DeterministicRepeatedEvaluation) {
    EvidenceBundle bundle;
    bundle.query = "How does the application authenticate users?";
    bundle.items.push_back(create_item(1, "LoginService", "src/auth/login_service.ts",
                                       parser::ElementKind::Class, 0.82,
                                       retrieval::RetrievalProvenance::SemanticOnly,
                                       "export class LoginService { login(user, pass) {} }",
                                       "handles user login and credential verification"));

    const auto query_rep = retrieval::QueryUnderstanding::analyze(bundle.query);
    SemanticEvidenceSupport sem_support(provider);
    const auto res1 = sem_support.evaluate(query_rep, bundle);
    const auto res2 = sem_support.evaluate(query_rep, bundle);

    EXPECT_EQ(res1.concepts.size(), res2.concepts.size());
    EXPECT_FLOAT_EQ(res1.concepts[0].coherent_support_score,
                    res2.concepts[0].coherent_support_score);
    EXPECT_EQ(res1.concepts[0].is_supported(), res2.concepts[0].is_supported());
}

// 7. Regression: Token extraction must NOT accept hash_token_seed as valid evidence
TEST_F(SemanticEvidenceSupportTest, TokenExtractionRejectsHashTokenSeed) {
    EvidenceBundle bundle_fp;
    bundle_fp.query = "Where is token extraction performed?";
    bundle_fp.items.push_back(create_item(
        50, "hash_token_seed", "engine/src/semantic/pretrained_embedding_provider.cpp",
        parser::ElementKind::Function, 0.68, retrieval::RetrievalProvenance::SemanticOnly,
        "uint64_t hash_token_seed(string_view token) { ... }",
        "computes fnv-1a hash seed for deterministic pseudo-random projection"));

    const auto query_rep = retrieval::QueryUnderstanding::analyze(bundle_fp.query);
    SemanticEvidenceSupport sem_support(provider);
    const auto sem_result = sem_support.evaluate(query_rep, bundle_fp);

    // hash_token_seed has no lexical token extraction support and is a lone hash helper
    const auto res = EvidenceSufficiencyChecker::check(query_rep, bundle_fp, sem_result);
    EXPECT_FALSE(res.is_sufficient);
}

// 8. Regression: True token extraction (CodeTokenizer::tokenize) is supported
TEST_F(SemanticEvidenceSupportTest, TokenExtractionAcceptsCodeTokenizer) {
    EvidenceBundle bundle_tp;
    bundle_tp.query = "Where is token extraction performed?";
    bundle_tp.items.push_back(create_item(
        51, "tokenize", "engine/src/index/code_tokenizer.cpp", parser::ElementKind::Function, 0.88,
        retrieval::RetrievalProvenance::HybridBoth,
        "vector<string> CodeTokenizer::tokenize(string_view source) { ... }",
        "extracts code tokens, identifiers, and symbols from source text"));

    const auto query_rep = retrieval::QueryUnderstanding::analyze(bundle_tp.query);
    SemanticEvidenceSupport sem_support(provider);
    const auto sem_result = sem_support.evaluate(query_rep, bundle_tp);

    const auto res = EvidenceSufficiencyChecker::check(query_rep, bundle_tp, sem_result);
    EXPECT_TRUE(res.is_sufficient);
}

// 9. Negative regression matrix: JWT, Payment, Redis
TEST_F(SemanticEvidenceSupportTest, NegativeQueriesMatrixRejected) {
    // JWT
    {
        EvidenceBundle bundle;
        bundle.query = "Where is JWT authentication implemented?";
        bundle.items.push_back(create_item(60, "CalendarDay", "src/CalendarDay.tsx",
                                           parser::ElementKind::Function, 0.50,
                                           retrieval::RetrievalProvenance::SemanticOnly));
        const auto query_rep = retrieval::QueryUnderstanding::analyze(bundle.query);
        SemanticEvidenceSupport sem_support(provider);
        const auto sem_result = sem_support.evaluate(query_rep, bundle);
        const auto res = EvidenceSufficiencyChecker::check(query_rep, bundle, sem_result);
        EXPECT_FALSE(res.is_sufficient);
    }

    // Payment
    {
        EvidenceBundle bundle;
        bundle.query = "Where is payment processing implemented?";
        bundle.items.push_back(create_item(61, "onDateClick", "src/CalendarGrid.tsx",
                                           parser::ElementKind::Function, 0.48,
                                           retrieval::RetrievalProvenance::SemanticOnly));
        const auto query_rep = retrieval::QueryUnderstanding::analyze(bundle.query);
        SemanticEvidenceSupport sem_support(provider);
        const auto sem_result = sem_support.evaluate(query_rep, bundle);
        const auto res = EvidenceSufficiencyChecker::check(query_rep, bundle, sem_result);
        EXPECT_FALSE(res.is_sufficient);
    }

    // Redis
    {
        EvidenceBundle bundle;
        bundle.query = "Where is Redis configured?";
        bundle.items.push_back(create_item(62, "CalendarContext", "src/CalendarContext.tsx",
                                           parser::ElementKind::Class, 0.42,
                                           retrieval::RetrievalProvenance::SemanticOnly));
        const auto query_rep = retrieval::QueryUnderstanding::analyze(bundle.query);
        SemanticEvidenceSupport sem_support(provider);
        const auto sem_result = sem_support.evaluate(query_rep, bundle);
        const auto res = EvidenceSufficiencyChecker::check(query_rep, bundle, sem_result);
        EXPECT_FALSE(res.is_sufficient);
    }
}

// 10. Positive vocabulary gap: Date formatting
TEST_F(SemanticEvidenceSupportTest, DateFormattingSupported) {
    EvidenceBundle bundle;
    bundle.query = "How are dates formatted for display?";
    bundle.items.push_back(create_item(70, "formatDate", "src/utils/date.ts",
                                       parser::ElementKind::Function, 0.85,
                                       retrieval::RetrievalProvenance::HybridBoth,
                                       "export function formatDate(date: Date): string { ... }",
                                       "formats calendar dates into localized display strings"));
    bundle.items.push_back(create_item(71, "MONTH_NAMES", "src/constants/calendar.ts",
                                       parser::ElementKind::Variable, 0.74,
                                       retrieval::RetrievalProvenance::SemanticOnly,
                                       "export const MONTH_NAMES = ['Jan', 'Feb', ...];",
                                       "month name labels for calendar display"));

    const auto query_rep = retrieval::QueryUnderstanding::analyze(bundle.query);
    SemanticEvidenceSupport sem_support(provider);
    const auto sem_result = sem_support.evaluate(query_rep, bundle);
    const auto res = EvidenceSufficiencyChecker::check(query_rep, bundle, sem_result);
    EXPECT_TRUE(res.is_sufficient);
}

// 11. Negative regression: OAuth on calendar repository is rejected
TEST_F(SemanticEvidenceSupportTest, OAuthOnCalendarRepositoryIsRejected) {
    EvidenceBundle bundle;
    bundle.query = "Where is OAuth configured?";
    bundle.items.push_back(
        create_item(80, "handleAction", "src/components/floating-toolbar/FloatingToolbar.tsx",
                    parser::ElementKind::Function, 0.67, retrieval::RetrievalProvenance::SemanticOnly,
                    "const handleAction = (action: string) => { ... }",
                    "handles floating toolbar click actions"));
    bundle.items.push_back(
        create_item(81, "FloatingToolbar", "src/components/floating-toolbar/FloatingToolbar.tsx",
                    parser::ElementKind::Function, 0.66, retrieval::RetrievalProvenance::SemanticOnly,
                    "export const FloatingToolbar = () => { ... }",
                    "floating toolbar container component"));

    const auto query_rep = retrieval::QueryUnderstanding::analyze(bundle.query);
    SemanticEvidenceSupport sem_support(provider);
    const auto sem_result = sem_support.evaluate(query_rep, bundle);
    const auto res = EvidenceSufficiencyChecker::check(query_rep, bundle, sem_result);
    EXPECT_FALSE(res.is_sufficient);
}

}  // namespace amoeba::evidence::test

