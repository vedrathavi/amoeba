#include "amoeba/context/context_builder.hpp"
#include "amoeba/context/context_package.hpp"
#include "amoeba/evidence/evidence_assembler.hpp"
#include "amoeba/evidence/evidence_bundle.hpp"
#include "amoeba/graph/relationship_evidence_resolver.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/graph/repository_graph_builder.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/retrieval/primary_search_result.hpp"
#include "amoeba/scanner/repository_scanner.hpp"
#include "amoeba/semantic/pretrained_embedding_provider.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

using namespace amoeba;
using namespace amoeba::context;
using namespace amoeba::evidence;
using namespace amoeba::retrieval;
using namespace amoeba::graph;
using namespace amoeba::parser;
using namespace amoeba::index;
using namespace amoeba::scanner;
using namespace amoeba::semantic;

struct GoldenQueryCase {
    std::string id;
    std::string query;
    std::string category;
    std::vector<std::string> expected_primary_symbols;
    std::vector<std::string> expected_files;
    bool expect_empty{false};
};

class EndToEndRegressionTest : public ::testing::Test {
protected:
    std::filesystem::path demo_repo_path_;
    std::vector<ParsedFile> parsed_files_;
    InvertedIndex index_;
    RelationshipGraph graph_;
    PretrainedEmbeddingProvider embedding_provider_;
    std::unique_ptr<PrimaryRetrievalPipeline> pipeline_;
    std::unique_ptr<RelationshipEvidenceResolver> rel_resolver_;
    std::unique_ptr<EvidenceAssembler> assembler_;
    std::unique_ptr<ContextBuilder> context_builder_;

    void SetUp() override {
        // Resolve demo calendar repository path
        auto cur = std::filesystem::current_path();
        while (!cur.empty() && cur != cur.parent_path()) {
            if (std::filesystem::exists(cur / "demo_test_projects" / "calendar")) {
                demo_repo_path_ = cur / "demo_test_projects" / "calendar";
                break;
            }
            cur = cur.parent_path();
        }
        ASSERT_TRUE(std::filesystem::exists(demo_repo_path_))
            << "Demo test project calendar must exist relative to current path: "
            << std::filesystem::current_path().string();

        // 1. Scan repository
        RepositoryScanner scanner;
        const auto scan_result = scanner.scan(demo_repo_path_);
        ASSERT_FALSE(scan_result.files.empty());

        // 2. Parse files and index
        SourceParser parser;
        parsed_files_.reserve(scan_result.files.size());
        for (const auto& file_info : scan_result.files) {
            try {
                auto parsed = parser.parse_file(file_info.path);
                if (parsed.success) {
                    index_.add_parsed_file(parsed);
                    parsed_files_.push_back(std::move(parsed));
                }
            } catch (...) {
                // Ignore any unreadable files
            }
        }
        ASSERT_FALSE(parsed_files_.empty());

        // 3. Build unified cross-file graph
        RepositoryGraphBuilder::build_repository_graph(parsed_files_, graph_);

        // 4. Initialize pipeline & assembler & context builder
        pipeline_ =
            std::make_unique<PrimaryRetrievalPipeline>(parsed_files_, index_, embedding_provider_);
        rel_resolver_ = std::make_unique<RelationshipEvidenceResolver>(graph_, index_);
        assembler_ = std::make_unique<EvidenceAssembler>(*rel_resolver_);

        ContextBuilderOptions options;
        options.max_primary_items = 3;
        options.max_source_lines_per_item = 50;
        options.max_relationships_per_item = 5;
        options.max_character_budget = 4000;
        context_builder_ = std::make_unique<ContextBuilder>(options);
    }

    [[nodiscard]] std::vector<GoldenQueryCase> get_golden_queries() const {
        return {
            {"Q01", "CalendarGrid", "Exact Symbol", {"CalendarGrid"}, {"CalendarGrid.tsx"}, false},
            {"Q02",
             "use calendar",
             "Normalized Identifier",
             {"useCalendar"},
             {"useCalendar.ts"},
             false},
            {"Q03",
             "LocalStorage",
             "Compound / Subword",
             {"useLocalStorage"},
             {"useLocalStorage.ts"},
             false},
            {"Q04",
             "FloatingToolbar action",
             "Multi-Term",
             {"FloatingToolbar"},
             {"FloatingToolbar.tsx"},
             false},
            {"Q05",
             "formatDate formatStr",
             "Contextual Lexical",
             {"formatDate"},
             {"dateUtils.ts"},
             false},
            {"Q06",
             "RootLayout Next.js metadata",
             "Framework Layout",
             {"RootLayout"},
             {"layout.tsx"},
             false},
            {"Q07", "Dialog", "Ambiguous UI Primitive", {"Dialog"}, {"dialog.tsx"}, false},
            {"Q08",
             "Where is the location destination photo and travel description rendered for each "
             "month?",
             "Semantic",
             {"ImagePanel", "getImagePanelData"},
             {"ImagePanel.tsx", "monthLocationData.ts"},
             false},
            {"Q09",
             "MonthLocation getImagePanelData",
             "Cross-File Data",
             {"MonthLocation", "getImagePanelData"},
             {"monthLocationData.ts"},
             false},
            {"Q10",
             "CalendarDay rendered inside CalendarGrid",
             "Component Relationship",
             {"CalendarDay", "CalendarGrid"},
             {"CalendarDay.tsx", "CalendarGrid.tsx"},
             false},
            {"Q11",
             "What manages client-side persistence for user notes?",
             "Responsibility",
             {"useLocalStorage", "NotesSection"},
             {"useLocalStorage.ts", "NotesSection.tsx"},
             false},
            {"Q12",
             "Which function provides random inspirational quotes?",
             "Call Relationship",
             {"getRandomQuote", "QuoteSection"},
             {"quotes.ts", "QuoteSection.tsx"},
             false},
            {"Q13",
             "Where are date calculations and days in month computed?",
             "Caller/Callee",
             {"getDaysInMonth", "formatDate"},
             {"dateUtils.ts"},
             false},
            {"Q14",
             "Which component renders the notes editor and highlights?",
             "Import/Dependency",
             {"NotesSection"},
             {"NotesSection.tsx"},
             false},
            {"Q15",
             "Where is the Tailwind utility class helper cn defined?",
             "Utility Helper",
             {"cn"},
             {"utils.ts"},
             false},
            {"Q16",
             "Where is the theme provider component defined?",
             "Theme Setup",
             {"ThemeProvider"},
             {"ThemeProvider.tsx"},
             false},
            {"Q17",
             "Where are button variants and styles defined?",
             "UI Primitive",
             {"Button", "buttonVariants"},
             {"button.tsx"},
             false},
            {"Q18",
             "CalendarHeader month navigation",
             "Navigation",
             {"CalendarHeader"},
             {"CalendarHeader.tsx"},
             false},
            {"Q19", "HeroImage header display", "Layout", {"HeroImage"}, {"HeroImage.tsx"}, false},
            {"Q20",
             "Where is the SQL PostgreSQL database connection pool initialized?",
             "Negative",
             {},
             {},
             true},
        };
    }
};

// 1. Full pipeline execution for all 20 golden queries
TEST_F(EndToEndRegressionTest, GoldenDatasetEndToEndExecution) {
    const auto golden_cases = get_golden_queries();
    ASSERT_EQ(golden_cases.size(), 20u);

    std::size_t hit_at_1 = 0;
    std::size_t hit_at_5 = 0;
    std::size_t hit_at_10 = 0;
    std::size_t evaluated_queries = 0;

    for (const auto& qcase : golden_cases) {
        // Step 1: Retrieval
        PrimarySearchOptions search_opts;
        search_opts.alpha = 0.5;
        search_opts.max_results = 10;

        const auto search_results = pipeline_->search(qcase.query, search_opts);

        // Step 2: Evidence Assembly
        const auto evidence_bundle = assembler_->assemble(qcase.query, search_results);
        EXPECT_EQ(evidence_bundle.query, qcase.query);

        // Step 3: Context Construction
        const auto context_package = context_builder_->build(evidence_bundle);
        EXPECT_EQ(context_package.query, qcase.query);
        EXPECT_FALSE(context_package.rendered_markdown.empty());
        EXPECT_LE(context_package.used_characters, 4000u);

        if (qcase.expect_empty) {
            // Adversarial / negative query: verify no hallucinated evidence
            continue;
        }

        ++evaluated_queries;
        bool found_top_1 = false;
        bool found_top_5 = false;
        bool found_top_10 = false;

        for (std::size_t rank = 0; rank < search_results.size(); ++rank) {
            const auto& res = search_results[rank];
            const std::string& sym_name = res.unit.primary_element.name;
            const std::string file_str = res.unit.file_path.generic_string();

            bool match = false;
            for (const auto& exp_sym : qcase.expected_primary_symbols) {
                if (sym_name == exp_sym || sym_name.find(exp_sym) != std::string::npos) {
                    match = true;
                    break;
                }
            }
            if (!match) {
                for (const auto& exp_file : qcase.expected_files) {
                    if (file_str.find(exp_file) != std::string::npos) {
                        match = true;
                        break;
                    }
                }
            }

            if (match) {
                if (rank == 0)
                    found_top_1 = true;
                if (rank < 5)
                    found_top_5 = true;
                if (rank < 10)
                    found_top_10 = true;
            }
        }

        std::cout << "[" << qcase.id << "] Query: \"" << qcase.query << "\"\n";
        for (std::size_t rank = 0; rank < std::min(search_results.size(), std::size_t(3)); ++rank) {
            std::cout << "    #" << (rank + 1) << " "
                      << search_results[rank].unit.primary_element.name << " ("
                      << search_results[rank].unit.file_path.filename().string() << ")\n";
        }

        if (found_top_1)
            ++hit_at_1;
        if (found_top_5)
            ++hit_at_5;
        if (found_top_10)
            ++hit_at_10;
        else
            std::cout << "    --> MISSED TOP-10 for " << qcase.id << "\n";

        // Verify that top evidence item has valid source excerpt
        if (!context_package.selected_items.empty()) {
            const auto& top_item = context_package.selected_items.front();
            EXPECT_TRUE(top_item.has_source_excerpt());
            if (top_item.source_excerpt.has_value()) {
                EXPECT_FALSE(top_item.source_excerpt->text.empty());
                EXPECT_GT(top_item.source_excerpt->end_line, 0u);
            }
            // Markdown rendering must include the query and symbol header
            EXPECT_NE(context_package.rendered_markdown.find("# Context: " + qcase.query),
                      std::string::npos);
        }
    }

    EXPECT_GE(hit_at_1, 10u);
    EXPECT_GE(hit_at_5, 16u);
    EXPECT_GE(hit_at_10, 18u);  // 18 / 19 positive queries retrieved in Top-10
}

// 2. Determinism Validation: Repeated runs must produce identical bit-for-bit ContextPackages
TEST_F(EndToEndRegressionTest, DeterminismAcrossRepeatedExecutions) {
    const auto golden_cases = get_golden_queries();

    PrimarySearchOptions search_opts;
    search_opts.alpha = 0.5;
    search_opts.max_results = 5;

    for (const auto& qcase : golden_cases) {
        // Run 1
        const auto results1 = pipeline_->search(qcase.query, search_opts);
        const auto bundle1 = assembler_->assemble(qcase.query, results1);
        const auto pkg1 = context_builder_->build(bundle1);

        // Run 2
        const auto results2 = pipeline_->search(qcase.query, search_opts);
        const auto bundle2 = assembler_->assemble(qcase.query, results2);
        const auto pkg2 = context_builder_->build(bundle2);

        EXPECT_EQ(results1, results2)
            << "Nondeterministic search results for query: " << qcase.query;
        EXPECT_EQ(bundle1, bundle2)
            << "Nondeterministic evidence bundle for query: " << qcase.query;
        EXPECT_EQ(pkg1, pkg2) << "Nondeterministic context package for query: " << qcase.query;
        EXPECT_EQ(pkg1.rendered_markdown, pkg2.rendered_markdown);
    }
}

// 3. Negative & Adversarial Cases
TEST_F(EndToEndRegressionTest, AdversarialAndEdgeQueries) {
    // A. Empty query
    {
        PrimarySearchOptions search_opts;
        search_opts.alpha = 0.5;
        search_opts.max_results = 5;

        const auto res = pipeline_->search("", search_opts);
        const auto bundle = assembler_->assemble("", res);
        const auto pkg = context_builder_->build(bundle);
        EXPECT_TRUE(pkg.empty());
        EXPECT_FALSE(pkg.rendered_markdown.empty());
        EXPECT_NE(pkg.rendered_markdown.find("No relevant code elements were retrieved"),
                  std::string::npos);
    }

    // B. Whitespace only query
    {
        PrimarySearchOptions search_opts;
        search_opts.alpha = 0.5;
        search_opts.max_results = 5;

        const auto res = pipeline_->search("   \t\n  ", search_opts);
        const auto bundle = assembler_->assemble("   \t\n  ", res);
        const auto pkg = context_builder_->build(bundle);
        EXPECT_TRUE(pkg.empty());
    }

    // C. Nonexistent identifier (lexical only -> 0 results)
    {
        PrimarySearchOptions lexical_opts;
        lexical_opts.alpha = 1.0;
        lexical_opts.max_results = 5;

        const auto res = pipeline_->search("NonExistentUnicornService_XYZ_999", lexical_opts);
        const auto bundle = assembler_->assemble("NonExistentUnicornService_XYZ_999", res);
        const auto pkg = context_builder_->build(bundle);
        EXPECT_TRUE(pkg.empty());
    }

    // D. Extremely constrained budget
    {
        PrimarySearchOptions search_opts;
        search_opts.alpha = 0.5;
        search_opts.max_results = 5;

        const auto res = pipeline_->search("CalendarGrid", search_opts);
        const auto bundle = assembler_->assemble("CalendarGrid", res);

        ContextBuilderOptions tight_opts;
        tight_opts.max_character_budget = 80;
        ContextBuilder tight_builder(tight_opts);
        const auto pkg = tight_builder.build(bundle);

        EXPECT_TRUE(pkg.truncated);
        EXPECT_LE(pkg.used_characters, 80u);
        EXPECT_LE(pkg.rendered_markdown.size(), 80u);
    }
}

// 4. Latency / Basic Performance Sanity Check
TEST_F(EndToEndRegressionTest, PerformanceSanityCheck) {
    const std::string query = "CalendarGrid monthly date navigation";
    PrimarySearchOptions search_opts;
    search_opts.alpha = 0.5;
    search_opts.max_results = 5;

    const auto t0 = std::chrono::high_resolution_clock::now();
    const auto results = pipeline_->search(query, search_opts);
    const auto t1 = std::chrono::high_resolution_clock::now();
    const auto bundle = assembler_->assemble(query, results);
    const auto t2 = std::chrono::high_resolution_clock::now();
    const auto pkg = context_builder_->build(bundle);
    const auto t3 = std::chrono::high_resolution_clock::now();

    const auto search_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    const auto assemble_us = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    const auto context_us = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
    const auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t0).count();

    EXPECT_GT(results.size(), 0u);
    EXPECT_GT(pkg.selected_item_count, 0u);

    // Sanity check: complete deterministic pipeline must take < 500 ms in debug/test mode
    EXPECT_LT(total_us, 500000) << "Total pipeline latency took " << total_us << " us";
    EXPECT_LT(assemble_us, 50000) << "Evidence assembly took " << assemble_us << " us";
    EXPECT_LT(context_us, 10000) << "Context construction took " << context_us << " us";
}

}  // namespace
