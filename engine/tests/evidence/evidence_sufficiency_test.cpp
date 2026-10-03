#include "amoeba/evidence/evidence_sufficiency.hpp"

#include <gtest/gtest.h>

using namespace amoeba;
using namespace amoeba::evidence;
using namespace amoeba::retrieval;
using namespace amoeba::parser;
using namespace amoeba::source;

class EvidenceSufficiencyTest : public ::testing::Test {
protected:
    EvidenceItem create_item(std::string name, std::string file_path, double score,
                             RetrievalProvenance prov = RetrievalProvenance::HybridBoth,
                             std::string excerpt_code = "") {
        EvidenceItem item;
        item.primary_result.unit.primary_element.name = name;
        item.primary_result.unit.primary_element.kind = ElementKind::Function;
        item.primary_result.unit.file_path = file_path;
        item.primary_result.hybrid_score = score;
        item.primary_result.normalized_lexical_score =
            (prov != RetrievalProvenance::SemanticOnly) ? score : 0.0;
        item.primary_result.normalized_semantic_score =
            (prov != RetrievalProvenance::LexicalOnly) ? score : 0.0;
        item.primary_result.provenance = prov;

        if (!excerpt_code.empty()) {
            SourceExcerpt excerpt;
            excerpt.text = excerpt_code;
            excerpt.start_line = 1;
            excerpt.end_line = 10;
            item.source_excerpt = excerpt;
        }

        return item;
    }
};

// 1. Empty bundle is insufficient
TEST_F(EvidenceSufficiencyTest, EmptyBundleIsInsufficient) {
    EvidenceBundle bundle;
    bundle.query = "Where is calendar state managed?";

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_FALSE(res.is_sufficient);
    EXPECT_DOUBLE_EQ(res.confidence_score, 0.0);
    EXPECT_FALSE(res.reason.empty());
}

// 2. Strong exact identifier match is sufficient
TEST_F(EvidenceSufficiencyTest, StrongExactIdentifierMatchIsSufficient) {
    EvidenceBundle bundle;
    bundle.query = "Where is useCalendar defined?";
    bundle.items.push_back(create_item("useCalendar", "src/hooks/useCalendar.ts", 0.95,
                                       RetrievalProvenance::HybridBoth,
                                       "export function useCalendar() { return { state }; }"));

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_TRUE(res.is_sufficient);
    EXPECT_GT(res.confidence_score, 0.5);
    EXPECT_FALSE(res.matched_terms.empty());
}

// 3. Strong compound identifier match is sufficient
TEST_F(EvidenceSufficiencyTest, StrongCompoundIdentifierMatchIsSufficient) {
    EvidenceBundle bundle;
    bundle.query = "Where is calendar grid rendered?";
    bundle.items.push_back(create_item("CalendarGrid", "src/components/CalendarGrid.tsx", 0.88,
                                       RetrievalProvenance::HybridBoth,
                                       "export const CalendarGrid = () => <div>Grid</div>;"));

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_TRUE(res.is_sufficient);
    EXPECT_GT(res.confidence_score, 0.5);
}

// 4. Clearly unrelated candidates are insufficient
TEST_F(EvidenceSufficiencyTest, ClearlyUnrelatedCandidatesAreInsufficient) {
    EvidenceBundle bundle;
    bundle.query = "Where is JWT authentication implemented?";
    bundle.items.push_back(create_item("FloatingToolbar", "src/components/FloatingToolbar.tsx",
                                       0.25, RetrievalProvenance::SemanticOnly,
                                       "export const FloatingToolbar = () => <div />;"));
    bundle.items.push_back(create_item("checkDevice", "src/utils/device.ts", 0.20,
                                       RetrievalProvenance::SemanticOnly,
                                       "export function checkDevice() { return true; }"));

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_FALSE(res.is_sufficient);
    EXPECT_TRUE(res.matched_terms.empty());
    EXPECT_FALSE(res.missing_terms.empty());

    const auto refusal = res.format_grounded_refusal(bundle.query);
    EXPECT_NE(refusal.find("couldn't find sufficient evidence"), std::string::npos);
    EXPECT_NE(refusal.find("jwt"), std::string::npos);
}

// 5. Weak semantic-only result without keyword match is insufficient
TEST_F(EvidenceSufficiencyTest, WeakSemanticOnlyResultIsInsufficient) {
    EvidenceBundle bundle;
    bundle.query = "Where is OAuth token refreshed?";
    bundle.items.push_back(create_item(
        "hash_token_seed", "src/semantic/provider.cpp", 0.35, RetrievalProvenance::SemanticOnly,
        "uint64_t hash_token_seed(std::string_view str) { return 0; }"));

    EvidenceSufficiencyOptions opts;
    opts.min_semantic_only_score = 0.60;

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle, opts);
    EXPECT_FALSE(res.is_sufficient);
}

// 6. Positive Calendar query with realistic bundle is sufficient
TEST_F(EvidenceSufficiencyTest, PositiveCalendarQueryIsSufficient) {
    EvidenceBundle bundle;
    bundle.query = "Where is calendar state managed?";
    bundle.items.push_back(create_item("useState", "src/components/CalendarDay.tsx", 0.93,
                                       RetrievalProvenance::HybridBoth,
                                       "const [selectedDate, setSelectedDate] = useState(null);"));
    bundle.items.push_back(create_item("useCalendar", "src/hooks/useCalendar.ts", 0.85,
                                       RetrievalProvenance::HybridBoth,
                                       "export function useCalendar() { return { state }; }"));

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_TRUE(res.is_sufficient);
    EXPECT_GE(res.confidence_score, 0.5);
}

// 7. Positive Notes query with realistic bundle is sufficient
TEST_F(EvidenceSufficiencyTest, PositiveNotesQueryIsSufficient) {
    EvidenceBundle bundle;
    bundle.query = "Where are notes saved?";
    bundle.items.push_back(create_item("NotesSection", "src/components/NotesSection.tsx", 0.90,
                                       RetrievalProvenance::HybridBoth,
                                       "function handleModalSave() { saveNotes(); }"));

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_TRUE(res.is_sufficient);
    EXPECT_GE(res.confidence_score, 0.5);
}

// 8. Deterministic repeated evaluation yields identical results
TEST_F(EvidenceSufficiencyTest, DeterministicRepeatedEvaluation) {
    EvidenceBundle bundle;
    bundle.query = "Where is calendar state managed?";
    bundle.items.push_back(create_item("useCalendar", "src/hooks/useCalendar.ts", 0.85));

    const auto res1 = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    const auto res2 = EvidenceSufficiencyChecker::check(bundle.query, bundle);

    EXPECT_EQ(res1.is_sufficient, res2.is_sufficient);
    EXPECT_DOUBLE_EQ(res1.confidence_score, res2.confidence_score);
    EXPECT_EQ(res1.reason, res2.reason);
    EXPECT_EQ(res1.matched_terms, res2.matched_terms);
    EXPECT_EQ(res1.missing_terms, res2.missing_terms);
}

// 9. Formatted refusal string is clean and contains no hallucinated symbols
TEST_F(EvidenceSufficiencyTest, GroundedRefusalFormat) {
    EvidenceSufficiencyResult result;
    result.is_sufficient = false;
    result.missing_subjects = {"jwt", "authentication"};
    result.reason =
        "None of the query subject concepts were found in the retrieved code candidates.";

    const auto text = result.format_grounded_refusal("Where is JWT authentication implemented?");
    EXPECT_NE(text.find("Where is JWT authentication implemented?"), std::string::npos);
    EXPECT_NE(text.find("\"jwt\""), std::string::npos);
    EXPECT_NE(text.find("\"authentication\""), std::string::npos);
}

// 10. Subject missing + Action present must be INSUFFICIENT (RBAC False-Positive Guard)
TEST_F(EvidenceSufficiencyTest, SubjectMissingActionPresentIsInsufficient) {
    EvidenceBundle bundle;
    bundle.query = "Where is RBAC implemented?";
    // Candidate contains the action word "implemented", but NOT the subject "rbac"
    bundle.items.push_back(create_item(
        "RelationshipAwareSearchEngine::search", "src/search/relationship_aware_search.cpp", 0.75,
        RetrievalProvenance::SemanticOnly, "// Implemented by RelationshipKind..."));

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_FALSE(res.is_sufficient);
    EXPECT_DOUBLE_EQ(res.confidence_score, 0.0);
    EXPECT_TRUE(res.matched_subjects.empty());
    EXPECT_FALSE(res.missing_subjects.empty());
    EXPECT_EQ(res.missing_subjects[0], "rbac");

    const auto refusal = res.format_grounded_refusal(bundle.query);
    EXPECT_NE(refusal.find("rbac"), std::string::npos);
}

// 11. Calendar Navigation Query with Plural Normalization is SUFFICIENT (Calendar False-Negative
// Fix)
TEST_F(EvidenceSufficiencyTest, CalendarNavigationWithPluralStemmingIsSufficient) {
    EvidenceBundle bundle;
    bundle.query = "How does the calendar navigate between months?";
    bundle.items.push_back(create_item(
        "CalendarDay", "src/components/CalendarDay.tsx", 0.85, RetrievalProvenance::HybridBoth,
        "export const CalendarDay = ({ date, currentMonth }) => <div />;"));
    bundle.items.push_back(create_item("CalendarHeader", "src/components/CalendarHeader.tsx", 0.80,
                                       RetrievalProvenance::HybridBoth,
                                       "<button onClick={onPreviousMonth}>Prev</button>"));

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_TRUE(res.is_sufficient);
    EXPECT_GE(res.confidence_score, 0.5);
    // Verified that "calendar" and "month" (stemmed from "months") were matched
    EXPECT_NE(std::find(res.matched_subjects.begin(), res.matched_subjects.end(), "calendar"),
              res.matched_subjects.end());
    EXPECT_NE(std::find(res.matched_subjects.begin(), res.matched_subjects.end(), "month"),
              res.matched_subjects.end());
}

// 12. Generic Non-Existent Concept is INSUFFICIENT
TEST_F(EvidenceSufficiencyTest, GenericNonExistentConceptIsInsufficient) {
    EvidenceBundle bundle;
    bundle.query = "Where is payment processing implemented?";
    bundle.items.push_back(create_item("GeneralUtility", "src/utils/general.cpp", 0.65,
                                       RetrievalProvenance::SemanticOnly,
                                       "void format_log() { /* general implementation */ }"));

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_FALSE(res.is_sufficient);
    EXPECT_TRUE(res.matched_subjects.empty());
}

// 13. Phase 7.6.1: Generic Component Term Collision Rejection (GraphQL Resolver)
TEST_F(EvidenceSufficiencyTest, GenericComponentCollisionWithoutDistinguishingSubjectIsRejected) {
    EvidenceBundle bundle;
    bundle.query = "Where is the GraphQL resolver?";
    // SupportingEvidenceResolver matches "resolver" but does NOT contain "graphql"
    bundle.items.push_back(create_item("SupportingEvidenceResolver",
                                       "engine/src/retrieval/supporting_evidence_resolver.cpp", 0.70,
                                       RetrievalProvenance::HybridBoth,
                                       "class SupportingEvidenceResolver { resolve_supporting_elements(); };"));

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_FALSE(res.is_sufficient);  // Must reject!
    EXPECT_DOUBLE_EQ(res.confidence_score, 0.0);
    EXPECT_NE(std::find(res.missing_subjects.begin(), res.missing_subjects.end(), "graphql"),
              res.missing_subjects.end());
    EXPECT_NE(res.reason.find("Only generic component terms were matched"), std::string::npos);
}

// 14. Phase 7.6.1: Legitimate Query Matching Distinguishing Subjects is ACCEPTED
TEST_F(EvidenceSufficiencyTest, LegitimateQueryMatchingDistinguishingSubjectsIsAccepted) {
    EvidenceBundle bundle;
    bundle.query = "Where are supporting AST elements resolved?";
    bundle.items.push_back(create_item("SupportingEvidenceResolver",
                                       "engine/src/retrieval/supporting_evidence_resolver.cpp", 0.85,
                                       RetrievalProvenance::HybridBoth,
                                       "class SupportingEvidenceResolver { resolve_supporting_elements(); };"));

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_TRUE(res.is_sufficient);  // Must accept!
    EXPECT_GE(res.confidence_score, 0.5);
    EXPECT_NE(std::find(res.matched_subjects.begin(), res.matched_subjects.end(), "support"),
              res.matched_subjects.end());
}

// 15. Phase 7.6.1: Single-Term Generic Component Query is ACCEPTED When Present
TEST_F(EvidenceSufficiencyTest, SingleTermGenericQueryIsAcceptedWhenPresent) {
    EvidenceBundle bundle;
    bundle.query = "Where is the parser?";
    bundle.items.push_back(create_item("SourceParser",
                                       "engine/src/parser/source_parser.cpp", 0.90,
                                       RetrievalProvenance::HybridBoth,
                                       "class SourceParser { ParsedFile parse_file(); };"));

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_TRUE(res.is_sufficient);
}

// ─── Phase 8.2.6 Regression Tests ────────────────────────────────────────────

// 16. Correct code + conversational framing → ACCEPT
TEST_F(EvidenceSufficiencyTest, CorrectCodeWithConversationalFramingIsAccepted) {
    EvidenceBundle bundle;
    bundle.query = "What are the trade-offs and how do I handle PostgreSQL pool connection timeout?";
    bundle.items.push_back(create_item(
        "PostgreSQLPool", "src/db/postgres_pool.cpp", 0.92, RetrievalProvenance::HybridBoth,
        "class PostgreSQLPool { int connection_timeout_ms; void connect(); };"));

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_TRUE(res.is_sufficient);
    EXPECT_GE(res.confidence_score, 0.5);
    EXPECT_FALSE(res.matched_subjects.empty());
}

// 17. Correct code + irrelevant generic terms → ACCEPT only when subject is grounded
TEST_F(EvidenceSufficiencyTest, CorrectCodeWithIrrelevantGenericTermsAcceptedWhenSubjectGrounded) {
    EvidenceBundle bundle;
    bundle.query = "What is the best way to handle file work problem in WalkerConfig parser?";
    bundle.items.push_back(create_item(
        "WalkerConfig", "src/config/walker_config.cpp", 0.88, RetrievalProvenance::HybridBoth,
        "struct WalkerConfig { std::string parse_config(const std::string& path); };"));

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_TRUE(res.is_sufficient);
    EXPECT_GE(res.confidence_score, 0.5);
}

// 18. Unrelated code + generic overlap → REJECT
TEST_F(EvidenceSufficiencyTest, UnrelatedCodeWithGenericOverlapIsRejected) {
    EvidenceBundle bundle;
    bundle.query = "How does the engine handle worker thread pool configuration?";
    // Candidate only contains generic words: "handle", "configuration", but missing "worker", "thread", "pool"
    bundle.items.push_back(create_item(
        "GenericHandler", "src/ui/generic_handler.cpp", 0.65, RetrievalProvenance::SemanticOnly,
        "void handle_configuration_change() { /* UI generic handler */ }"));

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_FALSE(res.is_sufficient);
    EXPECT_DOUBLE_EQ(res.confidence_score, 0.0);
}

// 19. Unsupported domain subject → REJECT
TEST_F(EvidenceSufficiencyTest, UnsupportedDomainSubjectIsRejected) {
    EvidenceBundle bundle;
    bundle.query = "Where is the Kubernetes ingress controller configured?";
    bundle.items.push_back(create_item(
        "HttpRouter", "src/network/http_router.cpp", 0.55, RetrievalProvenance::SemanticOnly,
        "class HttpRouter { void route_request(); };"));

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_FALSE(res.is_sufficient);
    EXPECT_DOUBLE_EQ(res.confidence_score, 0.0);
}

// 20. Negative benchmark cases → still REFUSE
TEST_F(EvidenceSufficiencyTest, NegativeBenchmarkCasesRefuseUnsupportedFeatures) {
    EvidenceBundle bundle;
    bundle.query = "How do I configure SAML 2.0 single sign-on authentication in Beszel?";
    bundle.items.push_back(create_item(
        "SimpleAuth", "src/auth/simple_auth.go", 0.60, RetrievalProvenance::SemanticOnly,
        "func CheckPassword(user, pass string) bool { return true }"));

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_FALSE(res.is_sufficient);
    EXPECT_DOUBLE_EQ(res.confidence_score, 0.0);
}

// 21. Phase 8.2.9.1 Safety Invariant: Expanded items cannot manufacture ungrounded subject
TEST_F(EvidenceSufficiencyTest, ExpandedSupportingEvidenceCannotManufactureUngroundedSubject) {
    EvidenceBundle bundle;
    bundle.query = "Does adk-python provide built-in vector database indexing with Qdrant natively in core?";

    // Primary item is ungrounded (just a random handler with zero Qdrant / vector matches)
    auto primary_item = create_item("get_details", "src/adk/handlers.py", 0.15,
                                    RetrievalProvenance::LexicalOnly,
                                    "def get_details(): pass");
    primary_item.is_expanded_relationship = false;
    bundle.items.push_back(primary_item);

    // Expanded supporting item has random generic words ("indexing", "options") but NOT in primary
    auto expanded_item = create_item("LiveRequestQueue", "src/adk/queue.py", 0.10,
                                     RetrievalProvenance::SemanticOnly,
                                     "class LiveRequestQueue: \n    def indexing_options(self): pass");
    expanded_item.is_expanded_relationship = true;
    expanded_item.expansion_relationship_type = "CALLS";
    expanded_item.expansion_depth = 1;
    bundle.items.push_back(expanded_item);

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_FALSE(res.is_sufficient);
    EXPECT_DOUBLE_EQ(res.confidence_score, 0.0);
    EXPECT_NE(res.reason.find("primary retrieval candidates"), std::string::npos);
}

// 22. Phase 8.2.9.1 Safety Invariant: Expanded items legally enrich grounded primary subject
TEST_F(EvidenceSufficiencyTest, ExpandedSupportingEvidenceCanEnrichGroundedPrimarySubject) {
    EvidenceBundle bundle;
    bundle.query = "How do I print a Zoned timestamp with an offset formatting?";

    // Primary item grounds the core subject: "Zoned" and "timestamp"
    auto primary_item = create_item("Zoned", "src/zoned.rs", 0.85,
                                    RetrievalProvenance::HybridBoth,
                                    "pub struct Zoned { timestamp: Timestamp, offset: Offset }");
    primary_item.is_expanded_relationship = false;
    bundle.items.push_back(primary_item);

    // Expanded item supplies the supporting helper detail: "offset formatting"
    auto expanded_item = create_item("print_time_zone_annotation_buf", "src/fmt/rfc9557.rs", 0.70,
                                     RetrievalProvenance::SemanticOnly,
                                     "pub fn print_time_zone_annotation_buf(buf: &mut String, offset: Offset) {}");
    expanded_item.is_expanded_relationship = true;
    expanded_item.expansion_relationship_type = "CONTAINS";
    expanded_item.expansion_depth = 2;
    bundle.items.push_back(expanded_item);

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_TRUE(res.is_sufficient);
    EXPECT_GT(res.confidence_score, 0.4);
}

// 23. Phase 8.2.9.1 Safety Invariant: Spring Boot controller negative refusal
TEST_F(EvidenceSufficiencyTest, NegativeSpringControllerQueryRefusedDespiteGenericCaller) {
    EvidenceBundle bundle;
    bundle.query = "Where is the Spring Boot auto-configuration controller implemented in pkl?";

    // Primary item is ungrounded
    auto primary_item = create_item("getSourceModulesAsUris", "src/cli/CliBaseOptions.java", 0.12,
                                    RetrievalProvenance::LexicalOnly,
                                    "public List<URI> getSourceModulesAsUris() { return null; }");
    primary_item.is_expanded_relationship = false;
    bundle.items.push_back(primary_item);

    // Expanded item mentions "controller" in a generic sense
    auto expanded_item = create_item("ProxySelector", "src/net/ProxySelector.java", 0.10,
                                     RetrievalProvenance::SemanticOnly,
                                     "// CLI controller options\npublic class ProxySelector { static ProxySelector create() { return null; } }");
    expanded_item.is_expanded_relationship = true;
    expanded_item.expansion_relationship_type = "CALLS";
    expanded_item.expansion_depth = 1;
    bundle.items.push_back(expanded_item);

    const auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    EXPECT_FALSE(res.is_sufficient);
    EXPECT_DOUBLE_EQ(res.confidence_score, 0.0);
}


