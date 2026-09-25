#include "amoeba/index/inverted_index.hpp"
#include "amoeba/index/search_engine.hpp"
#include "amoeba/rank/candidate_match.hpp"
#include "amoeba/rank/code_aware_ranker.hpp"

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

TEST(CodeAwareRankerTest, DeclarationPreferredOverUsage) {
    CorpusStats stats{
        .total_documents = 50,
        .avg_doc_length = 15.0,
        .doc_frequencies = {{"authenticate", 10}},
    };

    // Candidate 1: Call reference to authenticate()
    auto cand_call = make_candidate(1, ElementKind::Call, "authenticate", "src/login.cpp");
    cand_call.matched_terms = {"authenticate"};
    cand_call.term_frequencies = {{"authenticate", 1}};
    cand_call.matched_in_name = true;
    cand_call.name_match_count = 1;

    // Candidate 2: Class declaration of Authenticate
    auto cand_class =
        make_candidate(2, ElementKind::Class, "Authenticate", "src/auth/service.hpp");
    cand_class.matched_terms = {"authenticate"};
    cand_class.term_frequencies = {{"authenticate", 1}};
    cand_class.matched_in_name = true;
    cand_class.name_match_count = 1;

    vector<CandidateMatch> candidates = {cand_call, cand_class};
    const auto results = CodeAwareRanker::rank(candidates, stats, "authenticate", {"authenticate"});

    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].element.kind, ElementKind::Class);
    EXPECT_EQ(results[1].element.kind, ElementKind::Call);
    EXPECT_GT(results[0].score, results[1].score);
}

TEST(CodeAwareRankerTest, ParentContextDisambiguation) {
    CorpusStats stats{
        .total_documents = 50,
        .avg_doc_length = 15.0,
        .doc_frequencies = {{"scan", 10}, {"repository", 5}},
    };

    // Candidate 1: Generic scan method in ImageScanner
    auto cand_image =
        make_candidate(1, ElementKind::Method, "scan", "src/image.cpp", "ImageScanner");
    cand_image.matched_terms = {"scan"};
    cand_image.term_frequencies = {{"scan", 1}};
    cand_image.matched_in_name = true;
    cand_image.name_match_count = 1;

    // Candidate 2: scan method in RepositoryScanner
    auto cand_repo = make_candidate(2, ElementKind::Method, "scan", "src/scanner.cpp",
                                    "RepositoryScanner");
    cand_repo.matched_terms = {"scan", "repository"};
    cand_repo.term_frequencies = {{"scan", 1}, {"repository", 1}};
    cand_repo.matched_in_name = true;
    cand_repo.name_match_count = 1;
    cand_repo.matched_in_context = true;
    cand_repo.context_match_count = 1;

    vector<CandidateMatch> candidates = {cand_image, cand_repo};
    const auto results =
        CodeAwareRanker::rank(candidates, stats, "repository scan", {"repository", "scan"});

    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].element.parent_context, "RepositoryScanner");
    EXPECT_GT(results[0].score, results[1].score);
}

}  // namespace
