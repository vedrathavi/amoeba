// Phase 6.8 — CLI Retrieval Integration Tests
//
// Verifies:
//   1. Exact primary identifier returns primary unit
//   2. Spaced compound query returns synthesized primary unit
//   3. Supporting evidence isolation (calls/includes do not appear as top-level results)
//   4. Multiple supporting hits collapse into one primary result
//   5. Deterministic ordering across modes

#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/retrieval/primary_search_result.hpp"
#include "amoeba/retrieval/retrieval_unit.hpp"
#include "amoeba/semantic/deterministic_embedding_provider.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace amoeba::retrieval {

class CLIRetrievalIntegrationTest : public ::testing::Test {
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

// 1. Exact primary identifier
TEST_F(CLIRetrievalIntegrationTest, ExactPrimaryIdentifierAppearsAsPrimaryResult) {
    const std::string cpp_code = R"(
        namespace amoeba::retrieval {
            class QueryUnderstanding {
            public:
                static void analyze();
            };
        }
    )";

    auto pf = parser_.parse_source(cpp_code, "C++", "src/query_understanding.cpp");
    ASSERT_TRUE(pf.success);

    auto idx = build_index({pf});
    const parser::ParsedFile files[] = {pf};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    PrimarySearchOptions opts{
        .alpha = 1.0,
        .lexical_ranker = index::RankerType::CodeAware,
    };
    auto results = pipeline.search("QueryUnderstanding", opts);

    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].unit.primary_element.name, "QueryUnderstanding");
    EXPECT_TRUE(RetrievalUnitClassifier::is_primary(results[0].unit.primary_element.kind));
}

// 2. Spaced compound query
TEST_F(CLIRetrievalIntegrationTest, SpacedCompoundQueryMatchesPrimaryRetrievalPipeline) {
    const std::string cpp_code = R"(
        namespace amoeba::retrieval {
            class PrimaryRetrievalPipeline {
            public:
                void search();
            };
        }
    )";

    auto pf = parser_.parse_source(cpp_code, "C++", "src/primary_retrieval_pipeline.cpp");
    ASSERT_TRUE(pf.success);

    auto idx = build_index({pf});
    const parser::ParsedFile files[] = {pf};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    PrimarySearchOptions opts{
        .alpha = 1.0,
        .lexical_ranker = index::RankerType::CodeAware,
    };
    auto results = pipeline.search("retrieval pipeline", opts);

    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].unit.primary_element.name, "PrimaryRetrievalPipeline");
    EXPECT_EQ(results[0].unit.primary_element.kind, parser::ElementKind::Class);
}

// 3. Supporting evidence isolation
TEST_F(CLIRetrievalIntegrationTest, SupportingElementsNeverAppearAsTopLevelResults) {
    const std::string cpp_code = R"(
        #include "helper.hpp"
        void executeQuery() {
            helperCall();
            anotherHelper();
        }
    )";

    auto pf = parser_.parse_source(cpp_code, "C++", "src/query.cpp");
    ASSERT_TRUE(pf.success);

    auto idx = build_index({pf});
    const parser::ParsedFile files[] = {pf};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    // Search for a supporting element name ("helperCall")
    PrimarySearchOptions opts{
        .alpha = 1.0,
        .lexical_ranker = index::RankerType::CodeAware,
    };
    auto results = pipeline.search("helperCall", opts);

    for (const auto& r : results) {
        // Every top-level result must be a primary unit
        EXPECT_TRUE(RetrievalUnitClassifier::is_primary(r.unit.primary_element.kind))
            << "Unexpected supporting element at top level: " << r.unit.primary_element.name;
    }
}

// 4. Multiple supporting hits collapse into one primary result
TEST_F(CLIRetrievalIntegrationTest, MultipleSupportingHitsCollapseIntoOnePrimaryResult) {
    const std::string cpp_code = R"(
        void processBatch() {
            logMessage();
            logMessage();
            logMessage();
        }
    )";

    auto pf = parser_.parse_source(cpp_code, "C++", "src/batch.cpp");
    ASSERT_TRUE(pf.success);

    auto idx = build_index({pf});
    const parser::ParsedFile files[] = {pf};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    PrimarySearchOptions opts{
        .alpha = 1.0,
        .lexical_ranker = index::RankerType::CodeAware,
    };
    auto results = pipeline.search("logMessage", opts);

    // Should collapse to the owning function "processBatch", not 3 logMessage entries
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].unit.primary_element.name, "processBatch");
    EXPECT_GE(results[0].unit.supporting_elements.size(), 1u);
}

// 5. Determinism across runs
TEST_F(CLIRetrievalIntegrationTest, DeterministicOrderingAcrossRuns) {
    const std::string cpp_code = R"(
        class AlphaService { public: void run(); };
        class BetaService { public: void run(); };
        class GammaService { public: void run(); };
    )";

    auto pf = parser_.parse_source(cpp_code, "C++", "src/services.cpp");
    ASSERT_TRUE(pf.success);

    auto idx = build_index({pf});
    const parser::ParsedFile files[] = {pf};
    PrimaryRetrievalPipeline pipeline(files, idx, provider_);

    const std::vector<std::pair<std::string, PrimarySearchOptions>> test_modes = {
        {"Baseline",
         PrimarySearchOptions{.alpha = 1.0, .lexical_ranker = index::RankerType::Baseline}},
        {"BM25", PrimarySearchOptions{.alpha = 1.0, .lexical_ranker = index::RankerType::BM25}},
        {"CodeAware",
         PrimarySearchOptions{.alpha = 1.0, .lexical_ranker = index::RankerType::CodeAware}},
        {"Semantic", PrimarySearchOptions{.alpha = 0.0}},
        {"Hybrid", PrimarySearchOptions{.alpha = 0.5,
                                        .lexical_ranker = index::RankerType::CodeAware,
                                        .adaptive_fusion = true}},
    };

    for (const auto& [mode_name, opts] : test_modes) {
        auto run1 = pipeline.search("Service", opts);
        auto run2 = pipeline.search("Service", opts);

        ASSERT_EQ(run1.size(), run2.size()) << "Mode: " << mode_name;
        for (std::size_t i = 0; i < run1.size(); ++i) {
            EXPECT_EQ(run1[i].unit.primary_element.name, run2[i].unit.primary_element.name)
                << "Mismatch at index " << i << " in mode " << mode_name;
            EXPECT_DOUBLE_EQ(run1[i].hybrid_score, run2[i].hybrid_score)
                << "Score mismatch at index " << i << " in mode " << mode_name;
        }
    }
}

}  // namespace amoeba::retrieval
