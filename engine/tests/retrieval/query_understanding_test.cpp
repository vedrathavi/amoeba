#include "amoeba/retrieval/query_understanding.hpp"

#include <gtest/gtest.h>

namespace {

using namespace amoeba::retrieval;

TEST(QueryUnderstandingTest, NormalizeTextBasic) {
    EXPECT_EQ(QueryUnderstanding::normalize_text(""), "");
    EXPECT_EQ(QueryUnderstanding::normalize_text("   "), "");
    EXPECT_EQ(QueryUnderstanding::normalize_text("  hello   world  "), "hello world");
    EXPECT_EQ(QueryUnderstanding::normalize_text("Use Calendar"), "use calendar");
    EXPECT_EQ(QueryUnderstanding::normalize_text("CalendarGrid"), "calendargrid");
}

TEST(QueryUnderstandingTest, CollapseIdentifier) {
    EXPECT_EQ(QueryUnderstanding::collapse_identifier("use calendar"), "usecalendar");
    EXPECT_EQ(QueryUnderstanding::collapse_identifier("Calendar Grid"), "calendargrid");
    EXPECT_EQ(QueryUnderstanding::collapse_identifier("floating_toolbar_action"),
              "floatingtoolbaraction");
    EXPECT_EQ(QueryUnderstanding::collapse_identifier("get-user-by-id"), "getuserbyid");
    EXPECT_EQ(QueryUnderstanding::collapse_identifier("src/hooks/useCalendar.ts"),
              "srchooksusecalendarts");
}

TEST(QueryUnderstandingTest, SynthesizeCompounds) {
    const std::vector<std::string> terms = {"use", "calendar"};
    const auto compounds = QueryUnderstanding::synthesize_compounds(terms);

    ASSERT_FALSE(compounds.empty());
    EXPECT_EQ(compounds[0], "usecalendar");

    const std::vector<std::string> multi_terms = {"floating", "toolbar", "action"};
    const auto multi_compounds = QueryUnderstanding::synthesize_compounds(multi_terms);
    EXPECT_TRUE(std::find(multi_compounds.begin(), multi_compounds.end(),
                          "floatingtoolbaraction") != multi_compounds.end());
    EXPECT_TRUE(std::find(multi_compounds.begin(), multi_compounds.end(), "floatingtoolbar") !=
                multi_compounds.end());
    EXPECT_TRUE(std::find(multi_compounds.begin(), multi_compounds.end(), "toolbaraction") !=
                multi_compounds.end());
}

TEST(QueryUnderstandingTest, StopwordDetection) {
    EXPECT_TRUE(QueryUnderstanding::is_stopword("where"));
    EXPECT_TRUE(QueryUnderstanding::is_stopword("the"));
    EXPECT_TRUE(QueryUnderstanding::is_stopword("is"));
    EXPECT_TRUE(QueryUnderstanding::is_stopword("for"));
    EXPECT_TRUE(QueryUnderstanding::is_stopword("each"));

    EXPECT_FALSE(QueryUnderstanding::is_stopword("calendar"));
    EXPECT_FALSE(QueryUnderstanding::is_stopword("grid"));
    EXPECT_FALSE(QueryUnderstanding::is_stopword("auth"));
    EXPECT_FALSE(QueryUnderstanding::is_stopword("service"));
}

TEST(QueryUnderstandingTest, AnalyzeIdentifierQuery) {
    const auto rep = QueryUnderstanding::analyze("CalendarGrid");
    EXPECT_EQ(rep.intent, QueryIntent::IdentifierOrTechnical);
    EXPECT_TRUE(rep.has_code_syntax);
    EXPECT_GE(rep.recommended_alpha, 0.7);
    EXPECT_EQ(rep.collapsed_query, "calendargrid");
}

TEST(QueryUnderstandingTest, AnalyzeSpaceSeparatedHookQuery) {
    const auto rep = QueryUnderstanding::analyze("use calendar");
    EXPECT_EQ(rep.intent, QueryIntent::IdentifierOrTechnical);
    EXPECT_FALSE(rep.synthesized_identifiers.empty());
    EXPECT_EQ(rep.synthesized_identifiers[0], "usecalendar");
    EXPECT_GE(rep.recommended_alpha, 0.7);
}

TEST(QueryUnderstandingTest, AnalyzeNaturalLanguageQuestion) {
    const auto rep = QueryUnderstanding::analyze(
        "Where is the location destination photo and travel description rendered for each month?");
    EXPECT_EQ(rep.intent, QueryIntent::NaturalLanguage);
    EXPECT_TRUE(rep.has_interrogative);
    EXPECT_LE(rep.recommended_alpha, 0.3);
}

TEST(QueryUnderstandingTest, AnalyzeMultiTermCodeQuery) {
    const auto rep = QueryUnderstanding::analyze("FloatingToolbar action");
    EXPECT_EQ(rep.intent, QueryIntent::IdentifierOrTechnical);
    EXPECT_TRUE(rep.has_code_syntax);
    EXPECT_GE(rep.recommended_alpha, 0.7);
}

TEST(QueryUnderstandingTest, AnalyzeFrameworkRouteQuery) {
    const auto rep = QueryUnderstanding::analyze("RootLayout Next.js metadata");
    EXPECT_EQ(rep.intent, QueryIntent::IdentifierOrTechnical);
    EXPECT_TRUE(rep.has_code_syntax);
}

TEST(QueryUnderstandingTest, DeterministicStability) {
    const std::string query = "CalendarDay rendered inside CalendarGrid";
    const auto rep1 = QueryUnderstanding::analyze(query);
    const auto rep2 = QueryUnderstanding::analyze(query);

    EXPECT_EQ(rep1.intent, rep2.intent);
    EXPECT_EQ(rep1.recommended_alpha, rep2.recommended_alpha);
    EXPECT_EQ(rep1.raw_terms, rep2.raw_terms);
    EXPECT_EQ(rep1.all_search_terms, rep2.all_search_terms);
}

TEST(QueryUnderstandingTest, ConservativeStemmingPositive) {
    // Plurals to singular
    EXPECT_EQ(QueryUnderstanding::conservative_stem("months"), "month");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("components"), "component");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("tests"), "test");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("methods"), "method");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("classes"), "class");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("branches"), "branch");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("categories"), "category");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("properties"), "property");

    // Action verb conjugations
    EXPECT_EQ(QueryUnderstanding::conservative_stem("navigating"), "navigate");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("navigates"), "navigate");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("managed"), "manage");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("managing"), "manage");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("saved"), "save");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("saving"), "save");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("rendered"), "render");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("rendering"), "render");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("implemented"), "implement");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("created"), "create");
}

TEST(QueryUnderstandingTest, ConservativeStemmingNegativePreservation) {
    // Technical terms and short words must NOT be aggressively mangled
    EXPECT_EQ(QueryUnderstanding::conservative_stem("author"), "author");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("authority"), "authority");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("authentication"), "authentication");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("authorization"), "authorization");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("rbac"), "rbac");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("jwt"), "jwt");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("pass"), "pass");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("status"), "status");
    EXPECT_EQ(QueryUnderstanding::conservative_stem("this"), "this");
}

TEST(QueryUnderstandingTest, QueryTermRoleCategorization) {
    // Case 1: RBAC inquiry
    const auto rep1 = QueryUnderstanding::analyze("Where is RBAC implemented?");
    EXPECT_EQ(QueryUnderstanding::classify_term_role("where"), QueryTermRole::Context);
    EXPECT_EQ(QueryUnderstanding::classify_term_role("is"), QueryTermRole::Context);
    EXPECT_EQ(QueryUnderstanding::classify_term_role("rbac"), QueryTermRole::Subject);
    EXPECT_EQ(QueryUnderstanding::classify_term_role("implemented"), QueryTermRole::Action);

    EXPECT_EQ(rep1.subject_terms, (std::vector<std::string>{"rbac"}));
    EXPECT_EQ(rep1.action_terms, (std::vector<std::string>{"implement"}));

    // Case 2: Calendar navigation
    const auto rep2 = QueryUnderstanding::analyze("How does the calendar navigate between months?");
    EXPECT_EQ(QueryUnderstanding::classify_term_role("calendar"), QueryTermRole::Subject);
    EXPECT_EQ(QueryUnderstanding::classify_term_role("navigate"), QueryTermRole::Action);
    EXPECT_EQ(QueryUnderstanding::classify_term_role("months"), QueryTermRole::Subject);

    EXPECT_NE(std::find(rep2.subject_terms.begin(), rep2.subject_terms.end(), "calendar"),
              rep2.subject_terms.end());
    EXPECT_NE(std::find(rep2.subject_terms.begin(), rep2.subject_terms.end(), "month"),
              rep2.subject_terms.end());
    EXPECT_NE(std::find(rep2.action_terms.begin(), rep2.action_terms.end(), "navigate"),
              rep2.action_terms.end());

    // Case 3: Calendar state management
    const auto rep3 = QueryUnderstanding::analyze("Where is calendar state managed?");
    EXPECT_NE(std::find(rep3.subject_terms.begin(), rep3.subject_terms.end(), "calendar"),
              rep3.subject_terms.end());
    EXPECT_NE(std::find(rep3.subject_terms.begin(), rep3.subject_terms.end(), "state"),
              rep3.subject_terms.end());
    EXPECT_NE(std::find(rep3.action_terms.begin(), rep3.action_terms.end(), "manage"),
              rep3.action_terms.end());
}

TEST(QueryUnderstandingTest, GenericComponentTermDetection) {
    EXPECT_TRUE(QueryUnderstanding::is_generic_component_term("resolver"));
    EXPECT_TRUE(QueryUnderstanding::is_generic_component_term("handler"));
    EXPECT_TRUE(QueryUnderstanding::is_generic_component_term("manager"));
    EXPECT_TRUE(QueryUnderstanding::is_generic_component_term("service"));
    EXPECT_TRUE(QueryUnderstanding::is_generic_component_term("provider"));
    EXPECT_TRUE(QueryUnderstanding::is_generic_component_term("controller"));
    EXPECT_TRUE(QueryUnderstanding::is_generic_component_term("parser"));
    EXPECT_TRUE(QueryUnderstanding::is_generic_component_term("deployment"));
    EXPECT_TRUE(QueryUnderstanding::is_generic_component_term("authentication"));

    EXPECT_FALSE(QueryUnderstanding::is_generic_component_term("graphql"));
    EXPECT_FALSE(QueryUnderstanding::is_generic_component_term("jwt"));
    EXPECT_FALSE(QueryUnderstanding::is_generic_component_term("oauth"));
    EXPECT_FALSE(QueryUnderstanding::is_generic_component_term("calendar"));
    EXPECT_FALSE(QueryUnderstanding::is_generic_component_term("note"));
    EXPECT_FALSE(QueryUnderstanding::is_generic_component_term("inverted"));
}

TEST(QueryUnderstandingTest, DistinguishingSubjectSeparation) {
    // Compound concept: "GraphQL resolver"
    const auto rep = QueryUnderstanding::analyze("Where is the GraphQL resolver?");
    EXPECT_NE(std::find(rep.subject_terms.begin(), rep.subject_terms.end(), "graphql"),
              rep.subject_terms.end());
    EXPECT_NE(std::find(rep.subject_terms.begin(), rep.subject_terms.end(), "resolver"),
              rep.subject_terms.end());

    EXPECT_NE(std::find(rep.distinguishing_subject_terms.begin(),
                        rep.distinguishing_subject_terms.end(), "graphql"),
              rep.distinguishing_subject_terms.end());
    EXPECT_NE(std::find(rep.generic_component_terms.begin(), rep.generic_component_terms.end(),
                        "resolver"),
              rep.generic_component_terms.end());

    // Compound concept: "JWT authentication"
    const auto rep2 = QueryUnderstanding::analyze("Where is JWT authentication implemented?");
    EXPECT_NE(std::find(rep2.distinguishing_subject_terms.begin(),
                        rep2.distinguishing_subject_terms.end(), "jwt"),
              rep2.distinguishing_subject_terms.end());
    EXPECT_NE(std::find(rep2.generic_component_terms.begin(), rep2.generic_component_terms.end(),
                        "authentication"),
              rep2.generic_component_terms.end());
}

TEST(QueryUnderstandingTest, TechnicalEntityExtraction) {
    EXPECT_TRUE(QueryUnderstanding::is_technical_entity("useCalendar"));
    EXPECT_TRUE(QueryUnderstanding::is_technical_entity("createAuthClient"));
    EXPECT_TRUE(QueryUnderstanding::is_technical_entity("HTMLDocumentBackend"));
    EXPECT_TRUE(QueryUnderstanding::is_technical_entity("after_agent_callback"));
    EXPECT_TRUE(QueryUnderstanding::is_technical_entity("JWT"));
    EXPECT_TRUE(QueryUnderstanding::is_technical_entity("MCP"));
    EXPECT_TRUE(QueryUnderstanding::is_technical_entity("OAuth2"));
    EXPECT_TRUE(QueryUnderstanding::is_technical_entity("traefik.yml"));
    EXPECT_TRUE(QueryUnderstanding::is_technical_entity("Zoned::strftime"));

    EXPECT_FALSE(QueryUnderstanding::is_technical_entity("how"));
    EXPECT_FALSE(QueryUnderstanding::is_technical_entity("tradeoff"));
    EXPECT_FALSE(QueryUnderstanding::is_technical_entity("problem"));
}

TEST(QueryUnderstandingTest, ConversationalNoiseSeparationAndCleanQuery) {
    const std::string prompt = "Sequential Agent vs. LLM Agent in after_agent_callback - Architecture Trade-offs?";
    const auto rep = QueryUnderstanding::analyze(prompt);

    EXPECT_FALSE(rep.technical_entities.empty());
    EXPECT_FALSE(rep.conversational_noise_terms.empty());
    EXPECT_NE(std::find(rep.technical_entities.begin(), rep.technical_entities.end(), "after_agent_callback"),
              rep.technical_entities.end());
    EXPECT_NE(std::find(rep.conversational_noise_terms.begin(), rep.conversational_noise_terms.end(), "trade-offs"),
              rep.conversational_noise_terms.end());

    // Clean technical query focuses on technical entities and core subjects
    EXPECT_NE(rep.clean_technical_query.find("after_agent_callback"), std::string::npos);
    EXPECT_EQ(rep.clean_technical_query.find("trade-offs"), std::string::npos);
}

}  // namespace
