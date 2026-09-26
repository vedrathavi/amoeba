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
    result.missing_terms = {"jwt", "authentication"};
    result.reason = "None of the query keywords were found in the retrieved code candidates.";

    const auto text = result.format_grounded_refusal("Where is JWT authentication implemented?");
    EXPECT_NE(text.find("Where is JWT authentication implemented?"), std::string::npos);
    EXPECT_NE(text.find("\"jwt\""), std::string::npos);
    EXPECT_NE(text.find("\"authentication\""), std::string::npos);
}
