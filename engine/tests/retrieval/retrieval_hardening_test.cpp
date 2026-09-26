// Phase 6.7 — Final Retrieval Hardening & Adversarial Evaluation Test Suite
//
// This test suite stress-tests the retrieval foundation across:
// 1. Exact Identifier queries
// 2. Naturally-spaced compound queries
// 3. Case convention variants (camelCase, snake_case, PascalCase)
// 4. Natural language conceptual questions
// 5. Ambiguous short queries
// 6. Mixed technical queries
// 7. Determinism & repeatable ordering
// 8. Edge cases (empty, whitespace, punctuation, long query, path queries)
// 9. Cross-file symbol collision & independent addressability
// 10. Supporting evidence isolation (zero top-level pollution)

#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/retrieval/query_understanding.hpp"
#include "amoeba/retrieval/retrieval_unit.hpp"
#include "amoeba/semantic/deterministic_embedding_provider.hpp"
#include "amoeba/semantic/pretrained_embedding_provider.hpp"

#include <algorithm>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

using namespace amoeba;
using namespace amoeba::retrieval;
using namespace amoeba::parser;
using namespace amoeba::index;
using namespace amoeba::semantic;

class RetrievalHardeningTest : public ::testing::Test {
protected:
    void SetUp() override {
        SourceParser parser;

        // File 1: Auth controller (C++)
        const std::string auth_cpp = R"(
namespace amoeba::auth {
class AuthController {
public:
    bool authenticateUser(const std::string& token);
    void logoutUser(const std::string& userId);
    void resetPassword(const std::string& email);
};
}
)";

        // File 2: User profile component (TSX)
        const std::string profile_tsx = R"(
import { useState, useEffect } from 'react';

export interface UserProfileProps {
    userId: string;
    theme: string;
}

export function UserProfileCard({ userId, theme }: UserProfileProps) {
    const [profile, setProfile] = useState(null);
    return (
        <div className="user-profile-card p-4 bg-white rounded shadow">
            <h1 className="text-xl font-bold">User Profile</h1>
            <p>User details and preferences</p>
        </div>
    );
}
)";

        // File 3: Hook definition (TS)
        const std::string hook_ts = R"(
import { useState, useEffect } from 'react';

export function useCalendar(initialDate: Date) {
    const [currentDate, setCurrentDate] = useState(initialDate);
    const [viewMode, setViewMode] = useState('month');
    return { currentDate, setCurrentDate, viewMode, setViewMode };
}

export function useLocalStorage(key: string, initialValue: any) {
    const [storedValue, setStoredValue] = useState(initialValue);
    return [storedValue, setStoredValue];
}
)";

        // File 4: Date utilities (TS)
        const std::string date_utils_ts = R"(
export function formatDate(date: Date, formatStr: string): string {
    return date.toISOString();
}

export function parseDateString(input: string): Date {
    return new Date(input);
}
)";

        // File 5: Cross-file symbol collision fixture A (TSX)
        const std::string form_a_tsx = R"(
export function FormA() {
    const handleSubmit = () => { console.log("FormA submit"); };
    return <form onSubmit={handleSubmit}><button>Submit A</button></form>;
}
)";

        // File 6: Cross-file symbol collision fixture B (TSX)
        const std::string form_b_tsx = R"(
export function FormB() {
    const handleSubmit = () => { console.log("FormB submit"); };
    return <form onSubmit={handleSubmit}><button>Submit B</button></form>;
}
)";

        parsed_files.push_back(parser.parse_source(auth_cpp, "C++", "src/auth/AuthController.cpp"));
        parsed_files.push_back(
            parser.parse_source(profile_tsx, "TSX", "src/components/UserProfileCard.tsx"));
        parsed_files.push_back(
            parser.parse_source(hook_ts, "TypeScript", "src/hooks/useCalendar.ts"));
        parsed_files.push_back(
            parser.parse_source(date_utils_ts, "TypeScript", "src/lib/dateUtils.ts"));
        parsed_files.push_back(parser.parse_source(form_a_tsx, "TSX", "src/forms/FormA.tsx"));
        parsed_files.push_back(parser.parse_source(form_b_tsx, "TSX", "src/forms/FormB.tsx"));

        for (const auto& pf : parsed_files) {
            index.add_parsed_file(pf);
        }
    }

    std::vector<ParsedFile> parsed_files;
    InvertedIndex index;
    DeterministicEmbeddingProvider embedding_provider{64};
};

// =============================================================================
// 1. Exact Identifier Queries
// =============================================================================

TEST_F(RetrievalHardeningTest, ExactIdentifierMatchesTargetAtRank1) {
    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);

    const auto results = pipeline.search("formatDate");
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].unit.primary_element.name, "formatDate");
    EXPECT_EQ(results[0].unit.file_path.generic_string(), "src/lib/dateUtils.ts");
    EXPECT_EQ(results[0].unit.role, RetrievalUnitRole::Primary);
    EXPECT_TRUE(RetrievalUnitClassifier::is_primary(results[0].unit.primary_element.kind));
}

TEST_F(RetrievalHardeningTest, ExactClassIdentifierMatchesAtRank1) {
    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);

    const auto results = pipeline.search("AuthController");
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].unit.primary_element.name, "AuthController");
    EXPECT_EQ(results[0].unit.file_path.generic_string(), "src/auth/AuthController.cpp");
}

// =============================================================================
// 2. Compound Identifier Written Naturally
// =============================================================================

TEST_F(RetrievalHardeningTest, NaturallySpacedCompoundMatchesCamelCaseTarget) {
    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);

    const auto results = pipeline.search("use calendar");
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].unit.primary_element.name, "useCalendar");
    EXPECT_EQ(results[0].unit.file_path.generic_string(), "src/hooks/useCalendar.ts");
}

TEST_F(RetrievalHardeningTest, NaturallySpacedCompoundMatchesPascalCaseComponent) {
    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);

    const auto results = pipeline.search("user profile card");
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].unit.primary_element.name, "UserProfileCard");
}

// =============================================================================
// 3. Case Convention Normalization
// =============================================================================

TEST_F(RetrievalHardeningTest, CaseConventionsResolveToSamePrimaryUnit) {
    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);

    const auto res_camel = pipeline.search("useLocalStorage");
    const auto res_snake = pipeline.search("use_local_storage");
    const auto res_pascal = pipeline.search("UseLocalStorage");

    ASSERT_FALSE(res_camel.empty());
    ASSERT_FALSE(res_snake.empty());
    ASSERT_FALSE(res_pascal.empty());

    EXPECT_EQ(res_camel[0].unit.primary_element.name, "useLocalStorage");
    EXPECT_EQ(res_snake[0].unit.primary_element.name, "useLocalStorage");
    EXPECT_EQ(res_pascal[0].unit.primary_element.name, "useLocalStorage");
}

// =============================================================================
// 4. Conceptual & Natural Language Queries
// =============================================================================

TEST_F(RetrievalHardeningTest, NaturalLanguageQueryTriggersSemanticIntent) {
    const auto rep = QueryUnderstanding::analyze(
        "Where is the user profile card and theme preferences rendered?");
    EXPECT_EQ(rep.intent, QueryIntent::NaturalLanguage);
    EXPECT_LE(rep.recommended_alpha, 0.3);

    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);
    const auto results =
        pipeline.search("Where is the user profile card and theme preferences rendered?");
    ASSERT_FALSE(results.empty());
    // Must return valid primary units without crashing
    for (const auto& r : results) {
        EXPECT_EQ(r.unit.role, RetrievalUnitRole::Primary);
        EXPECT_TRUE(RetrievalUnitClassifier::is_primary(r.unit.primary_element.kind));
    }
}

// =============================================================================
// 5. Ambiguous Short Queries
// =============================================================================

TEST_F(RetrievalHardeningTest, AmbiguousQueryReturnsValidPrimaryUnitsWithoutPanic) {
    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);

    const auto results_user = pipeline.search("user");
    ASSERT_FALSE(results_user.empty());
    for (const auto& r : results_user) {
        EXPECT_EQ(r.unit.role, RetrievalUnitRole::Primary);
        EXPECT_TRUE(RetrievalUnitClassifier::is_primary(r.unit.primary_element.kind));
    }

    const auto results_date = pipeline.search("date");
    ASSERT_FALSE(results_date.empty());
    for (const auto& r : results_date) {
        EXPECT_EQ(r.unit.role, RetrievalUnitRole::Primary);
        EXPECT_TRUE(RetrievalUnitClassifier::is_primary(r.unit.primary_element.kind));
    }
}

// =============================================================================
// 6. Mixed Technical Queries
// =============================================================================

TEST_F(RetrievalHardeningTest, MixedTechnicalQueryMatchesRelevantSymbols) {
    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);

    const auto results = pipeline.search("formatDate string formatting utility");
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].unit.primary_element.name, "formatDate");
}

// =============================================================================
// 7. Strict Determinism & Repeatability
// =============================================================================

TEST_F(RetrievalHardeningTest, MultipleInvocationsProduceBitExactResults) {
    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);

    const std::string query = "authenticateUser";
    const auto run1 = pipeline.search(query);
    const auto run2 = pipeline.search(query);
    const auto run3 = pipeline.search(query);

    ASSERT_EQ(run1.size(), run2.size());
    ASSERT_EQ(run1.size(), run3.size());

    for (std::size_t i = 0; i < run1.size(); ++i) {
        EXPECT_EQ(run1[i].unit.primary_element.name, run2[i].unit.primary_element.name);
        EXPECT_EQ(run1[i].unit.primary_element.name, run3[i].unit.primary_element.name);
        EXPECT_DOUBLE_EQ(run1[i].hybrid_score, run2[i].hybrid_score);
        EXPECT_DOUBLE_EQ(run1[i].hybrid_score, run3[i].hybrid_score);
        EXPECT_EQ(run1[i].provenance, run2[i].provenance);
    }
}

// =============================================================================
// 8. Edge Cases (Empty, Whitespace, Punctuation, Long Query)
// =============================================================================

TEST_F(RetrievalHardeningTest, EmptyQueryReturnsEmptyVector) {
    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);
    EXPECT_TRUE(pipeline.search("").empty());
}

TEST_F(RetrievalHardeningTest, WhitespaceOnlyQueryReturnsEmptyVector) {
    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);
    EXPECT_TRUE(pipeline.search("   \t  \n  ").empty());
}

TEST_F(RetrievalHardeningTest, PunctuationOnlyQueryReturnsEmptyVectorGracefully) {
    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);
    const auto results = pipeline.search("!@#$%^&*()_+-=~`{}[]|;:,.<>?");
    // Should safely return empty without crashing or throwing
    EXPECT_TRUE(results.empty());
}

TEST_F(RetrievalHardeningTest, OneCharacterQueryExecutesSafely) {
    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);
    const auto results = pipeline.search("a");
    // Handled gracefully without crash
    for (const auto& r : results) {
        EXPECT_EQ(r.unit.role, RetrievalUnitRole::Primary);
        EXPECT_TRUE(RetrievalUnitClassifier::is_primary(r.unit.primary_element.kind));
    }
}

TEST_F(RetrievalHardeningTest, NonExistentQueryReturnsEmptyGracefully) {
    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);
    const auto lex_results =
        pipeline.search("NonExistentSymbol_XYZ_9999", PrimarySearchOptions{.alpha = 1.0});
    EXPECT_TRUE(lex_results.empty());

    const auto hybrid_results = pipeline.search("NonExistentSymbol_XYZ_9999");
    for (const auto& r : hybrid_results) {
        EXPECT_EQ(r.unit.role, RetrievalUnitRole::Primary);
        EXPECT_TRUE(RetrievalUnitClassifier::is_primary(r.unit.primary_element.kind));
    }
}

TEST_F(RetrievalHardeningTest, VeryLongQueryExecutesSafely) {
    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);
    std::string long_query = "find the user profile card with user ID and theme preferences ";
    for (int i = 0; i < 20; ++i) {
        long_query += "and additional metadata details ";
    }
    const auto results = pipeline.search(long_query);
    // Should complete safely without buffer overflow or crash
    for (const auto& r : results) {
        EXPECT_EQ(r.unit.role, RetrievalUnitRole::Primary);
        EXPECT_TRUE(RetrievalUnitClassifier::is_primary(r.unit.primary_element.kind));
    }
}

TEST_F(RetrievalHardeningTest, QueryWithFilePathSyntaxExecutesSafely) {
    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);
    const auto results = pipeline.search("src/hooks/useCalendar.ts");
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].unit.file_path.generic_string(), "src/hooks/useCalendar.ts");
}

// =============================================================================
// 9. Cross-File Symbol Collision & Independent Addressability
// =============================================================================

TEST_F(RetrievalHardeningTest, IdenticalSymbolNamesInDifferentFilesAreDistinct) {
    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);

    // FormA and FormB both define handleSubmit
    const auto results = pipeline.search("handleSubmit");
    ASSERT_GE(results.size(), 2U);

    // Verify both FormA and FormB are represented
    bool found_form_a = false;
    bool found_form_b = false;

    for (const auto& r : results) {
        if (r.unit.file_path.generic_string() == "src/forms/FormA.tsx") {
            found_form_a = true;
        }
        if (r.unit.file_path.generic_string() == "src/forms/FormB.tsx") {
            found_form_b = true;
        }
    }

    EXPECT_TRUE(found_form_a);
    EXPECT_TRUE(found_form_b);
}

// =============================================================================
// 10. Supporting Evidence Isolation Invariant
// =============================================================================

TEST_F(RetrievalHardeningTest, SupportingElementsNeverAppearAsTopLevelResults) {
    PrimaryRetrievalPipeline pipeline(parsed_files, index, embedding_provider);

    // Search for keywords that appear heavily in supporting elements (e.g., classes, JSX, calls)
    const std::vector<std::string> queries = {
        "useState", "className", "text-xl font-bold", "p-4 bg-white", "console.log", "toISOString",
    };

    for (const auto& q : queries) {
        const auto results = pipeline.search(q);
        for (const auto& r : results) {
            EXPECT_EQ(r.unit.role, RetrievalUnitRole::Primary)
                << "Supporting element surfaced as top-level result: "
                << r.unit.primary_element.name
                << " (Kind: " << parser::to_string(r.unit.primary_element.kind) << ")";
            EXPECT_TRUE(RetrievalUnitClassifier::is_primary(r.unit.primary_element.kind));
            EXPECT_NE(r.unit.primary_element.kind, ElementKind::Call);
            EXPECT_NE(r.unit.primary_element.kind, ElementKind::Attribute);
            EXPECT_NE(r.unit.primary_element.kind, ElementKind::Include);
            EXPECT_NE(r.unit.primary_element.kind, ElementKind::UtilityClass);
            EXPECT_NE(r.unit.primary_element.kind, ElementKind::JSXElement);
        }
    }
}

}  // namespace
