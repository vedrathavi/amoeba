#include "amoeba/index/inverted_index.hpp"
#include "amoeba/index/search_engine.hpp"
#include "amoeba/rank/baseline_ranker.hpp"
#include "amoeba/rank/candidate_match.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

using namespace std;
using namespace amoeba::index;
using namespace amoeba::parser;
using namespace amoeba::rank;

CandidateMatch make_candidate(ElementId id, ElementKind kind, string_view name,
                              string_view file_path, string_view parent = "",
                              string_view detail = "", size_t doc_len = 10) {
    return CandidateMatch{
        .element_id = id,
        .element =
            CodeElement{
                .kind = kind,
                .name = string(name),
                .location =
                    SourceRange{
                        .start = SourceLocation{.line = 1, .column = 1, .byte_offset = 0},
                        .end = SourceLocation{.line = 10, .column = 1, .byte_offset = 100},
                    },
                .parent_context = string(parent),
                .detail = string(detail),
            },
        .file_path = filesystem::path(file_path),
        .language = "C++",
        .matched_terms = {},
        .term_frequencies = {},
        .doc_length = doc_len,
        .exact_name_match = false,
        .normalized_name_match = false,
        .prefix_name_match = false,
        .matched_in_name = false,
        .matched_in_context = false,
        .matched_in_detail = false,
        .matched_in_path = false,
        .name_match_count = 0,
        .context_match_count = 0,
        .detail_match_count = 0,
        .path_match_count = 0,
    };
}

TEST(BaselineRankerTest, ExactIdentifierRanksAbovePartial) {
    auto cand_exact = make_candidate(1, ElementKind::Class, "AuthenticationService",
                                     "src/auth/service.hpp");
    cand_exact.exact_name_match = true;
    cand_exact.matched_in_name = true;
    cand_exact.matched_terms = {"authenticationservice"};

    auto cand_partial =
        make_candidate(2, ElementKind::Function, "authenticate", "src/auth/helper.cpp");
    cand_partial.matched_in_name = true;
    cand_partial.matched_terms = {"authenticate"};

    vector<CandidateMatch> candidates = {cand_partial, cand_exact};
    const auto results =
        BaselineRanker::rank(candidates, "AuthenticationService", {"authenticationservice"});

    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].element.name, "AuthenticationService");
    EXPECT_TRUE(results[0].exact_name_match);
    EXPECT_GT(results[0].score, results[1].score);
}

TEST(BaselineRankerTest, QueryTermCoverageRanking) {
    auto cand_full =
        make_candidate(1, ElementKind::Function, "user_login_auth", "src/auth.cpp");
    cand_full.matched_terms = {"user", "login", "auth"};

    auto cand_partial = make_candidate(2, ElementKind::Function, "user_login", "src/user.cpp");
    cand_partial.matched_terms = {"user", "login"};

    vector<CandidateMatch> candidates = {cand_partial, cand_full};
    const auto results =
        BaselineRanker::rank(candidates, "user login auth", {"user", "login", "auth"});

    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].element.name, "user_login_auth");
    EXPECT_GT(results[0].score, results[1].score);
}

TEST(BaselineRankerTest, StructuralFieldWeighting) {
    auto cand_name = make_candidate(1, ElementKind::Function, "scanner", "src/core.cpp");
    cand_name.matched_in_name = true;
    cand_name.matched_terms = {"scanner"};

    auto cand_context = make_candidate(2, ElementKind::Function, "run", "src/scanner.cpp",
                                       "RepositoryScanner");
    cand_context.matched_in_context = true;
    cand_context.matched_terms = {"scanner"};

    auto cand_path = make_candidate(3, ElementKind::Function, "run", "src/scanner/run.cpp");
    cand_path.matched_in_path = true;
    cand_path.matched_terms = {"scanner"};

    vector<CandidateMatch> candidates = {cand_path, cand_context, cand_name};
    const auto results = BaselineRanker::rank(candidates, "scanner", {"scanner"});

    ASSERT_EQ(results.size(), 3U);
    EXPECT_EQ(results[0].element.name, "scanner");
    EXPECT_GT(results[0].score, results[1].score);
    EXPECT_GT(results[1].score, results[2].score);
}

TEST(BaselineRankerTest, DeterministicTieBreaking) {
    auto cand_a = make_candidate(1, ElementKind::Function, "test_func", "src/a_file.cpp");
    cand_a.matched_terms = {"test"};

    auto cand_b = make_candidate(2, ElementKind::Function, "test_func", "src/b_file.cpp");
    cand_b.matched_terms = {"test"};

    vector<CandidateMatch> candidates1 = {cand_b, cand_a};
    const auto res1 = BaselineRanker::rank(candidates1, "test", {"test"});

    vector<CandidateMatch> candidates2 = {cand_a, cand_b};
    const auto res2 = BaselineRanker::rank(candidates2, "test", {"test"});

    ASSERT_EQ(res1.size(), 2U);
    ASSERT_EQ(res2.size(), 2U);
    EXPECT_EQ(res1[0].file_path, "src/a_file.cpp");
    EXPECT_EQ(res2[0].file_path, "src/a_file.cpp");
}

}  // namespace
