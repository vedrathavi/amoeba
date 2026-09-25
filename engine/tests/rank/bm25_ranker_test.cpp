#include "amoeba/index/inverted_index.hpp"
#include "amoeba/index/search_engine.hpp"
#include "amoeba/rank/bm25_ranker.hpp"
#include "amoeba/rank/candidate_match.hpp"

#include <cmath>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

using namespace std;
using namespace amoeba::index;
using namespace amoeba::parser;
using namespace amoeba::rank;

CandidateMatch make_candidate(ElementId id, ElementKind kind, string_view name,
                              string_view file_path, size_t doc_len) {
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
                .parent_context = "",
                .detail = "",
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

TEST(BM25MathTest, RSJ_IDF_HandCalculated) {
    // N = 100, DF = 10
    // IDF = ln(1 + (100 - 10 + 0.5) / (10 + 0.5)) = ln(1 + 90.5 / 10.5) = ln(1 + 8.619047) = ln(9.619047) = 2.2637
    const double idf = BM25Ranker::idf(100, 10);
    EXPECT_NEAR(idf, 2.26374, 0.001);

    // Rare term (DF = 1) vs Common term (DF = 90)
    const double idf_rare = BM25Ranker::idf(100, 1);
    const double idf_common = BM25Ranker::idf(100, 90);
    EXPECT_GT(idf_rare, idf_common);
}

TEST(BM25MathTest, TFSaturationBehavior) {
    const BM25Params params{.k1 = 1.2, .b = 0.75};
    const double term_idf = 2.0;
    const double avgdl = 10.0;
    const size_t doc_len = 10;

    const double score_tf1 = BM25Ranker::compute_term_score(1, doc_len, avgdl, term_idf, params);
    const double score_tf2 = BM25Ranker::compute_term_score(2, doc_len, avgdl, term_idf, params);
    const double score_tf10 = BM25Ranker::compute_term_score(10, doc_len, avgdl, term_idf, params);
    const double score_tf100 =
        BM25Ranker::compute_term_score(100, doc_len, avgdl, term_idf, params);

    // Diminishing returns (non-linear saturation)
    const double diff_1_to_2 = score_tf2 - score_tf1;
    const double diff_2_to_10 = (score_tf10 - score_tf2) / 8.0;
    EXPECT_GT(diff_1_to_2, diff_2_to_10);
    EXPECT_NEAR(score_tf100, term_idf * (params.k1 + 1.0), 0.1);
}

TEST(BM25MathTest, DocumentLengthNormalization) {
    const BM25Params params{.k1 = 1.2, .b = 0.75};
    const double term_idf = 2.0;
    const double avgdl = 20.0;

    // Same TF (3), but doc_short (|D|=5) vs doc_long (|D|=100)
    const double score_short = BM25Ranker::compute_term_score(3, 5, avgdl, term_idf, params);
    const double score_long = BM25Ranker::compute_term_score(3, 100, avgdl, term_idf, params);

    EXPECT_GT(score_short, score_long);
}

TEST(BM25SearchEngineTest, MultiTermBM25Ranking) {
    CorpusStats stats{
        .total_documents = 100,
        .avg_doc_length = 20.0,
        .doc_frequencies = {{"auth", 50}, {"jwt", 2}},
    };

    auto cand_rare = make_candidate(1, ElementKind::Function, "verify_jwt", "src/jwt.cpp", 10);
    cand_rare.matched_terms = {"jwt"};
    cand_rare.term_frequencies = {{"jwt", 1}};

    auto cand_common =
        make_candidate(2, ElementKind::Function, "auth_helper", "src/auth.cpp", 10);
    cand_common.matched_terms = {"auth"};
    cand_common.term_frequencies = {{"auth", 1}};

    vector<CandidateMatch> candidates = {cand_common, cand_rare};
    const auto results =
        BM25Ranker::rank(candidates, stats, "auth jwt", {"auth", "jwt"});

    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].element.name, "verify_jwt");
    EXPECT_GT(results[0].score, results[1].score);
}

}  // namespace
