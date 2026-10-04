#include "amoeba/evidence/evidence_assembler.hpp"
#include "amoeba/evidence/evidence_sufficiency.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/graph/repository_graph_builder.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/investigation/bounded_investigation_service.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/semantic/deterministic_embedding_provider.hpp"
#include "amoeba/source/source_snippet_reader.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

class BoundedInvestigationServiceTest : public ::testing::Test {
protected:
    fs::path temp_dir_;
    std::unique_ptr<amoeba::semantic::DeterministicEmbeddingProvider> embedding_provider_;
    std::unique_ptr<amoeba::parser::SourceParser> parser_;
    std::unique_ptr<amoeba::source::SourceSnippetReader> snippet_reader_;

    void SetUp() override {
        temp_dir_ = fs::temp_directory_path() / ("amoeba_investigation_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        fs::create_directories(temp_dir_);

        embedding_provider_ = std::make_unique<amoeba::semantic::DeterministicEmbeddingProvider>(64);
        parser_ = std::make_unique<amoeba::parser::SourceParser>();
        snippet_reader_ = std::make_unique<amoeba::source::SourceSnippetReader>();
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    fs::path create_file(const std::string& relative_path, const std::string& content) {
        fs::path full_path = temp_dir_ / relative_path;
        fs::create_directories(full_path.parent_path());
        std::ofstream ofs(full_path);
        ofs << content;
        ofs.close();
        return full_path;
    }
};

TEST_F(BoundedInvestigationServiceTest, Hop0RemainsUnchangedWhenInvestigationDisabled) {
    const auto file1 = create_file("auth_controller.cpp", R"(
        class AuthController {
        public:
            void handle_login() {
                auth_service.verify();
            }
        };
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;
    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);

    const auto hop0_results = pipeline.search("AuthController");
    ASSERT_FALSE(hop0_results.empty());

    amoeba::investigation::InvestigationOptions opts;
    opts.enabled = false;

    const auto results = investigator.investigate("AuthController", hop0_results, opts);
    EXPECT_EQ(results.size(), hop0_results.size());
    EXPECT_EQ(results[0].unit.primary_element.name, "AuthController");
}

TEST_F(BoundedInvestigationServiceTest, RelevantCandidateTriggersHop1AndDiscoversTarget) {
    const auto file1 = create_file("auth_controller.cpp", R"(
        class AuthController {
        public:
            void handle_login() {
                AuthService::verify_token();
            }
        };
    )");

    const auto file2 = create_file("auth_service.cpp", R"(
        class AuthService {
        public:
            static bool verify_token() {
                return true;
            }
        };
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;

    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));

    auto p2 = parser_->parse_file(file2);
    index.add_parsed_file(p2);
    parsed_files.push_back(std::move(p2));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);

    // Query specifically for login handling
    const auto hop0_results = pipeline.search("handle_login");
    ASSERT_FALSE(hop0_results.empty());

    amoeba::investigation::InvestigationOptions opts;
    opts.enabled = true;
    opts.max_hops = 1;
    opts.max_seeds = 2;
    opts.max_discovered_per_seed = 3;

    amoeba::investigation::InvestigationMetrics metrics;
    const auto final_results = investigator.investigate_with_metrics(
        amoeba::retrieval::QueryUnderstanding::analyze("handle_login"),
        hop0_results, opts, metrics);

    EXPECT_TRUE(metrics.investigation_triggered);
    EXPECT_GE(final_results.size(), hop0_results.size());

    bool found_auth_service = false;
    for (const auto& r : final_results) {
        if (r.unit.primary_element.name == "AuthService" || r.unit.primary_element.name == "verify_token") {
            found_auth_service = true;
            break;
        }
    }
    EXPECT_TRUE(found_auth_service);
}

TEST_F(BoundedInvestigationServiceTest, WeakCandidateDoesNotTriggerUncontrolledInvestigation) {
    const auto file1 = create_file("utils.cpp", R"(
        void helper() {}
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;
    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);

    // Search unrelated query
    const auto hop0_results = pipeline.search("nonexistent_symbol");

    amoeba::investigation::InvestigationOptions opts;
    opts.min_seed_relevance = 0.5; // Strict threshold

    amoeba::investigation::InvestigationMetrics metrics;
    const auto final_results = investigator.investigate_with_metrics(
        amoeba::retrieval::QueryUnderstanding::analyze("nonexistent_symbol"),
        hop0_results, opts, metrics);

    EXPECT_FALSE(metrics.investigation_triggered);
    EXPECT_EQ(metrics.hop1_candidates_discovered, 0);
}

TEST_F(BoundedInvestigationServiceTest, MaximumHopAndCandidateBoundsAreEnforced) {
    const auto file1 = create_file("main.cpp", R"(
        void a() { b(); }
        void b() { c(); }
        void c() { d(); }
        void d() {}
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;
    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);

    const auto hop0_results = pipeline.search("a");
    ASSERT_FALSE(hop0_results.empty());

    amoeba::investigation::InvestigationOptions opts;
    opts.max_hops = 1;
    opts.max_total_candidates = 3;

    amoeba::investigation::InvestigationMetrics metrics;
    const auto final_results = investigator.investigate_with_metrics(
        amoeba::retrieval::QueryUnderstanding::analyze("a"),
        hop0_results, opts, metrics);

    EXPECT_LE(final_results.size(), 3);
    for (const auto& step : metrics.steps) {
        EXPECT_EQ(step.step_number, 1);
    }
}

TEST_F(BoundedInvestigationServiceTest, DeduplicationPreservesSingleInstanceOfDiscoveredCandidate) {
    const auto file1 = create_file("service.cpp", R"(
        class Caller1 { void run() { Target::execute(); } };
        class Caller2 { void run() { Target::execute(); } };
        class Target { static void execute() {} };
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;
    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);

    const auto hop0_results = pipeline.search("Caller");

    amoeba::investigation::InvestigationOptions opts;
    opts.max_seeds = 3;

    amoeba::investigation::InvestigationMetrics metrics;
    const auto final_results = investigator.investigate_with_metrics(
        amoeba::retrieval::QueryUnderstanding::analyze("Caller"),
        hop0_results, opts, metrics);

    std::unordered_set<std::string> seen;
    int target_count = 0;
    for (const auto& r : final_results) {
        const std::string full_name = r.unit.primary_element.parent_context.empty()
                                          ? r.unit.primary_element.name
                                          : r.unit.primary_element.parent_context + "::" + r.unit.primary_element.name;
        EXPECT_EQ(seen.count(full_name), 0) << "Duplicate candidate discovered: " << full_name;
        seen.insert(full_name);
        if (r.unit.primary_element.name == "Target" || r.unit.primary_element.name == "execute") {
            target_count++;
        }
    }
    EXPECT_GT(target_count, 0);
}

TEST_F(BoundedInvestigationServiceTest, ExpandedEvidenceCannotManufacturePrimarySufficiency) {
    const auto file1 = create_file("generic_helper.cpp", R"(
        void helper() {
            // mentions unrelated token
        }
    )");

    const auto file2 = create_file("auth_engine.cpp", R"(
        class AuthenticationManager {
        public:
            void authenticate_user() {}
        };
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;
    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));

    auto p2 = parser_->parse_file(file2);
    index.add_parsed_file(p2);
    parsed_files.push_back(std::move(p2));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::graph::RelationshipEvidenceResolver rel_resolver(graph, index);
    amoeba::evidence::EvidenceAssembler assembler(*snippet_reader_, rel_resolver);

    // Negative control query: "Where is GraphQL resolver implemented?"
    const std::string query = "Where is GraphQL resolver implemented?";
    const auto hop0_results = pipeline.search("helper"); // Only helper matches

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);
    const auto final_results = investigator.investigate(query, hop0_results);

    const auto bundle = assembler.assemble(query, final_results);
    const auto query_rep = amoeba::retrieval::QueryUnderstanding::analyze(query);
    const auto sufficiency = amoeba::evidence::EvidenceSufficiencyChecker::check(query_rep, bundle);

    EXPECT_FALSE(sufficiency.is_sufficient);
    EXPECT_FALSE(sufficiency.reason.empty());
}

TEST_F(BoundedInvestigationServiceTest, MultiUnitEvidenceAssemblesCallerAndCalleeCorrectly) {
    const auto file1 = create_file("src/config.rs", R"(
        pub struct Config {
            pub default_terminal: String,
        }
    )");

    const auto file2 = create_file("src/providers/arch.rs", R"(
        use crate::config::Config;
        pub struct ArchProvider {
            config: Config,
        }
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;
    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));

    auto p2 = parser_->parse_file(file2);
    index.add_parsed_file(p2);
    parsed_files.push_back(std::move(p2));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::graph::RelationshipEvidenceResolver rel_resolver(graph, index);
    amoeba::evidence::EvidenceAssembler assembler(*snippet_reader_, rel_resolver);

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);

    const std::string query = "How to define default terminal for ArchProvider with Config?";
    const auto hop0_results = pipeline.search("ArchProvider");

    const auto investigated_results = investigator.investigate(query, hop0_results);
    const auto bundle = assembler.assemble(query, investigated_results);

    const auto query_rep = amoeba::retrieval::QueryUnderstanding::analyze(query);
    const auto sufficiency = amoeba::evidence::EvidenceSufficiencyChecker::check(query_rep, bundle);

    EXPECT_TRUE(sufficiency.is_sufficient);
}

TEST_F(BoundedInvestigationServiceTest, Hop2CanDiscoverGenuinelyUsefulTarget) {
    // 2-hop chain: handleRequest -> dispatchService -> executeDatabaseQuery
    const auto file1 = create_file("router.cpp", R"(
        void handleRequest() {
            dispatchService();
        }
    )");

    const auto file2 = create_file("dispatcher.cpp", R"(
        void dispatchService() {
            executeDatabaseQuery();
        }
    )");

    const auto file3 = create_file("database.cpp", R"(
        void executeDatabaseQuery() {}
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;

    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));

    auto p2 = parser_->parse_file(file2);
    index.add_parsed_file(p2);
    parsed_files.push_back(std::move(p2));

    auto p3 = parser_->parse_file(file3);
    index.add_parsed_file(p3);
    parsed_files.push_back(std::move(p3));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);

    amoeba::retrieval::PrimarySearchOptions search_opts;
    search_opts.max_results = 1; // Isolate primary seed to handleRequest
    const auto hop0_results = pipeline.search("handleRequest", search_opts);
    ASSERT_FALSE(hop0_results.empty());

    amoeba::investigation::InvestigationOptions opts;
    opts.enabled = true;
    opts.max_hops = 2;
    opts.max_seeds = 1;
    opts.max_discovered_per_seed = 3;
    opts.max_discovered_per_hop1 = 2;

    amoeba::investigation::InvestigationMetrics metrics;
    const auto candidates = investigator.investigate_candidates(
        "Where does handleRequest execute query?", hop0_results, opts, &metrics);

    EXPECT_TRUE(metrics.investigation_triggered);
    EXPECT_GT(metrics.hop1_candidates_discovered, 0);
    EXPECT_GT(metrics.hop2_candidates_discovered, 0);

    bool found_db_executor = false;
    for (const auto& c : candidates) {
        if (c.result.unit.primary_element.name == "executeDatabaseQuery") {
            found_db_executor = true;
            EXPECT_EQ(c.hop_depth, 2u);
            EXPECT_TRUE(c.is_hop2_discovered);
            EXPECT_EQ(c.role, amoeba::investigation::InvestigationCandidateRole::Hop2Relationship);
        }
    }
    EXPECT_TRUE(found_db_executor);
}

TEST_F(BoundedInvestigationServiceTest, Hop2RespectsMaximumDepth) {
    // 3-hop chain: NodeA -> NodeB -> NodeC -> NodeD
    const auto file1 = create_file("chain.cpp", R"(
        void funcA() { funcB(); }
        void funcB() { funcC(); }
        void funcC() { funcD(); }
        void funcD() {}
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;
    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);

    amoeba::retrieval::PrimarySearchOptions search_opts;
    search_opts.max_results = 1; // Only funcA in Hop-0
    const auto hop0_results = pipeline.search("funcA", search_opts);
    ASSERT_FALSE(hop0_results.empty());

    amoeba::investigation::InvestigationOptions opts;
    opts.enabled = true;
    opts.max_hops = 2; // Strictly 2 hops
    opts.max_seeds = 1;

    amoeba::investigation::InvestigationMetrics metrics;
    const auto candidates = investigator.investigate_candidates("funcA", hop0_results, opts, &metrics);

    bool found_funcB = false;
    bool found_funcC = false;
    bool found_funcD = false;

    for (const auto& c : candidates) {
        if (c.result.unit.primary_element.name == "funcB") found_funcB = true;
        if (c.result.unit.primary_element.name == "funcC") found_funcC = true;
        if (c.result.unit.primary_element.name == "funcD") found_funcD = true;
        EXPECT_LE(c.hop_depth, 2u);
    }

    EXPECT_TRUE(found_funcB);
    EXPECT_TRUE(found_funcC);
    // funcD is at hop-3, so it MUST NOT be discovered under max_hops = 2
    EXPECT_FALSE(found_funcD);

    for (const auto& step : metrics.steps) {
        EXPECT_LE(step.step_number, 2u);
    }
}

TEST_F(BoundedInvestigationServiceTest, CandidateLimitRemains15) {
    const auto file1 = create_file("multi_nodes.cpp", R"(
        void seedNode() {
            h1_a(); h1_b(); h1_c(); h1_d();
        }
        void h1_a() { h2_a1(); h2_a2(); h2_a3(); }
        void h1_b() { h2_b1(); h2_b2(); h2_b3(); }
        void h1_c() { h2_c1(); h2_c2(); h2_c3(); }
        void h1_d() { h2_d1(); h2_d2(); h2_d3(); }
        void h2_a1() {} void h2_a2() {} void h2_a3() {}
        void h2_b1() {} void h2_b2() {} void h2_b3() {}
        void h2_c1() {} void h2_c2() {} void h2_c3() {}
        void h2_d1() {} void h2_d2() {} void h2_d3() {}
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;
    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);

    const auto hop0_results = pipeline.search("seedNode");

    amoeba::investigation::InvestigationOptions opts;
    opts.max_hops = 2;
    opts.max_seeds = 5;
    opts.max_discovered_per_seed = 5;
    opts.max_discovered_per_hop1 = 5;
    opts.max_total_candidates = 15;

    amoeba::investigation::InvestigationMetrics metrics;
    const auto results = investigator.investigate("seedNode", hop0_results, opts, &metrics);

    EXPECT_LE(results.size(), 15u);
    EXPECT_LE(metrics.final_candidate_count, 15u);
}

TEST_F(BoundedInvestigationServiceTest, SeedLimitRemains5) {
    const auto file1 = create_file("seeds.cpp", R"(
        void s1() {} void s2() {} void s3() {} void s4() {}
        void s5() {} void s6() {} void s7() {} void s8() {}
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;
    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);

    const auto hop0_results = pipeline.search("s");

    amoeba::investigation::InvestigationOptions opts;
    opts.max_seeds = 5;

    amoeba::investigation::InvestigationMetrics metrics;
    const auto res = investigator.investigate("s", hop0_results, opts, &metrics);
    (void)res;

    EXPECT_LE(metrics.seeds_investigated, 5u);
    EXPECT_LE(metrics.selected_seed_names.size(), 5u);
}

TEST_F(BoundedInvestigationServiceTest, DeterministicOrderingAcrossExecutions) {
    const auto file1 = create_file("det.cpp", R"(
        class Alpha { void run() { Beta::process(); Gamma::process(); } };
        class Beta { static void process() { Delta::finish(); } };
        class Gamma { static void process() { Epsilon::finish(); } };
        class Delta { static void finish() {} };
        class Epsilon { static void finish() {} };
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;
    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);

    const auto hop0_results = pipeline.search("Alpha");

    amoeba::investigation::InvestigationOptions opts;
    opts.max_hops = 2;

    const auto run1 = investigator.investigate_candidates("Alpha", hop0_results, opts);
    const auto run2 = investigator.investigate_candidates("Alpha", hop0_results, opts);

    ASSERT_EQ(run1.size(), run2.size());
    for (std::size_t i = 0; i < run1.size(); ++i) {
        EXPECT_EQ(run1[i].result.unit.primary_element.name, run2[i].result.unit.primary_element.name);
        EXPECT_EQ(run1[i].role, run2[i].role);
        EXPECT_EQ(run1[i].hop_depth, run2[i].hop_depth);
        EXPECT_DOUBLE_EQ(run1[i].result.hybrid_score, run2[i].result.hybrid_score);
    }
}

TEST_F(BoundedInvestigationServiceTest, DuplicateCandidatesAreRemovedAcrossHops) {
    // Multi-path diamond DAG: startNode -> hop1_A, startNode -> hop1_B; both call sharedHop2
    const auto file1 = create_file("diamond.cpp", R"(
        void startNode() { hop1_A(); hop1_B(); }
        void hop1_A() { sharedHop2(); }
        void hop1_B() { sharedHop2(); }
        void sharedHop2() {}
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;
    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);

    amoeba::retrieval::PrimarySearchOptions search_opts;
    search_opts.max_results = 1;
    const auto hop0_results = pipeline.search("startNode", search_opts);

    amoeba::investigation::InvestigationOptions opts;
    opts.max_hops = 2;

    const auto candidates = investigator.investigate_candidates("startNode", hop0_results, opts);

    int shared_hop2_count = 0;
    for (const auto& c : candidates) {
        if (c.result.unit.primary_element.name == "sharedHop2") {
            shared_hop2_count++;
        }
    }
    EXPECT_EQ(shared_hop2_count, 1);
}

TEST_F(BoundedInvestigationServiceTest, ProvenanceIsPreservedInBundleAssembly) {
    const auto file1 = create_file("prov1.cpp", R"(
        void clientHandler() { authBridge(); }
    )");
    const auto file2 = create_file("prov2.cpp", R"(
        void authBridge() { tokenValidator(); }
    )");
    const auto file3 = create_file("prov3.cpp", R"(
        void tokenValidator() {}
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;
    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));
    auto p2 = parser_->parse_file(file2);
    index.add_parsed_file(p2);
    parsed_files.push_back(std::move(p2));
    auto p3 = parser_->parse_file(file3);
    index.add_parsed_file(p3);
    parsed_files.push_back(std::move(p3));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::graph::RelationshipEvidenceResolver rel_resolver(graph, index);
    amoeba::evidence::EvidenceAssembler assembler(*snippet_reader_, rel_resolver);

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);

    amoeba::retrieval::PrimarySearchOptions search_opts;
    search_opts.max_results = 1;
    const auto hop0_results = pipeline.search("clientHandler", search_opts);
    amoeba::investigation::InvestigationOptions opts;
    opts.max_hops = 2;

    const auto candidates = investigator.investigate_candidates("clientHandler", hop0_results, opts);
    const auto bundle = investigator.assemble_bundle("clientHandler", candidates, assembler);

    bool found_primary = false;
    bool found_hop1 = false;
    bool found_hop2 = false;

    for (const auto& item : bundle.items) {
        if (item.primary_element().name == "clientHandler") {
            found_primary = true;
            EXPECT_FALSE(item.is_expanded_relationship);
            EXPECT_EQ(item.expansion_depth, 0u);
        } else if (item.primary_element().name == "authBridge") {
            found_hop1 = true;
            EXPECT_TRUE(item.is_expanded_relationship);
            EXPECT_EQ(item.expansion_depth, 1u);
        } else if (item.primary_element().name == "tokenValidator") {
            found_hop2 = true;
            EXPECT_TRUE(item.is_expanded_relationship);
            EXPECT_EQ(item.expansion_depth, 2u);
        }
    }

    EXPECT_TRUE(found_primary);
    EXPECT_TRUE(found_hop1);
    EXPECT_TRUE(found_hop2);
}

TEST_F(BoundedInvestigationServiceTest, RuleA1RemainsIntactWithHop2Evidence) {
    // Primary retrieval matches an unrelated helper, while 2-hop connects to target
    const auto file1 = create_file("unrelated_root.cpp", R"(
        void helperRoot() { BridgeHop1::call(); }
    )");
    const auto file2 = create_file("bridge.cpp", R"(
        class BridgeHop1 { public: static void call() { TargetAuth::login(); } };
    )");
    const auto file3 = create_file("target_auth.cpp", R"(
        class TargetAuth { public: static void login() {} };
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;
    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));
    auto p2 = parser_->parse_file(file2);
    index.add_parsed_file(p2);
    parsed_files.push_back(std::move(p2));
    auto p3 = parser_->parse_file(file3);
    index.add_parsed_file(p3);
    parsed_files.push_back(std::move(p3));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::graph::RelationshipEvidenceResolver rel_resolver(graph, index);
    amoeba::evidence::EvidenceAssembler assembler(*snippet_reader_, rel_resolver);

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);

    // Query for TargetAuth login, but primary search only seeded with helperRoot
    const std::string query = "Where is TargetAuth login implemented?";
    amoeba::retrieval::PrimarySearchOptions search_opts;
    search_opts.max_results = 1;
    const auto hop0_results = pipeline.search("helperRoot", search_opts);

    amoeba::investigation::InvestigationOptions opts;
    opts.max_hops = 2;

    const auto candidates = investigator.investigate_candidates(query, hop0_results, opts);
    const auto bundle = investigator.assemble_bundle(query, candidates, assembler);

    const auto query_rep = amoeba::retrieval::QueryUnderstanding::analyze(query);
    const auto sufficiency = amoeba::evidence::EvidenceSufficiencyChecker::check(query_rep, bundle);

    // Rule A.1: Expanded (Hop-1/Hop-2) evidence cannot independently manufacture primary sufficiency!
    EXPECT_FALSE(sufficiency.is_sufficient);
    EXPECT_FALSE(sufficiency.reason.empty());
}

TEST_F(BoundedInvestigationServiceTest, RuleERemainsIntactWithHop2Evidence) {
    // Primary retrieval matches generic logger, 2-hop connects to specialized domain
    const auto file1 = create_file("logger.cpp", R"(
        void logTrace() { AuditPipe::pass(); }
    )");
    const auto file2 = create_file("pipe.cpp", R"(
        class AuditPipe { public: static void pass() { DomainPayment::process(); } };
    )");
    const auto file3 = create_file("payment.cpp", R"(
        class DomainPayment { public: static void process() {} };
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;
    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));
    auto p2 = parser_->parse_file(file2);
    index.add_parsed_file(p2);
    parsed_files.push_back(std::move(p2));
    auto p3 = parser_->parse_file(file3);
    index.add_parsed_file(p3);
    parsed_files.push_back(std::move(p3));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::graph::RelationshipEvidenceResolver rel_resolver(graph, index);
    amoeba::evidence::EvidenceAssembler assembler(*snippet_reader_, rel_resolver);

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);

    const std::string query = "Where is DomainPayment transaction logic implemented?";
    amoeba::retrieval::PrimarySearchOptions search_opts;
    search_opts.max_results = 1;
    const auto hop0_results = pipeline.search("logTrace", search_opts);

    amoeba::investigation::InvestigationOptions opts;
    opts.max_hops = 2;

    const auto candidates = investigator.investigate_candidates(query, hop0_results, opts);
    const auto bundle = investigator.assemble_bundle(query, candidates, assembler);

    const auto query_rep = amoeba::retrieval::QueryUnderstanding::analyze(query);
    const auto sufficiency = amoeba::evidence::EvidenceSufficiencyChecker::check(query_rep, bundle);

    // Rule E: Primary candidate does not match distinguishing subject terms -> must reject!
    EXPECT_FALSE(sufficiency.is_sufficient);
}

TEST_F(BoundedInvestigationServiceTest, NegativeUnsupportedQueryRemainsRejected) {
    const auto file1 = create_file("cache.cpp", R"(
        class MemoryCache {
        public:
            void put(const std::string& key, const std::string& val) {
                EvictionManager::check_eviction();
            }
        };
    )");
    const auto file2 = create_file("eviction.cpp", R"(
        class EvictionManager {
        public:
            static void check_eviction() {
                StorageDevice::sync();
            }
        };
    )");
    const auto file3 = create_file("storage.cpp", R"(
        class StorageDevice {
        public:
            static void sync() {}
        };
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;
    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));
    auto p2 = parser_->parse_file(file2);
    index.add_parsed_file(p2);
    parsed_files.push_back(std::move(p2));
    auto p3 = parser_->parse_file(file3);
    index.add_parsed_file(p3);
    parsed_files.push_back(std::move(p3));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::graph::RelationshipEvidenceResolver rel_resolver(graph, index);
    amoeba::evidence::EvidenceAssembler assembler(*snippet_reader_, rel_resolver);

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);

    // Completely unsupported negative question
    const std::string negative_query = "Where is the QuantumCryptographicKeyExchange algorithm implemented in this repo?";
    const auto hop0_results = pipeline.search("MemoryCache");

    amoeba::investigation::InvestigationOptions opts;
    opts.max_hops = 2;

    const auto candidates = investigator.investigate_candidates(negative_query, hop0_results, opts);
    const auto bundle = investigator.assemble_bundle(negative_query, candidates, assembler);

    const auto query_rep = amoeba::retrieval::QueryUnderstanding::analyze(negative_query);
    const auto sufficiency = amoeba::evidence::EvidenceSufficiencyChecker::check(query_rep, bundle);

    EXPECT_FALSE(sufficiency.is_sufficient);
}

TEST_F(BoundedInvestigationServiceTest, Hop2DoesNotExecuteWhenExistingEvidenceIsSufficient) {
    const auto file1 = create_file("auth_service.cpp", R"(
        class AuthenticationService {
        public:
            static bool verify_token() {
                return true;
            }
        };
    )");

    const auto file2 = create_file("token_helper.cpp", R"(
        class TokenHelper {
        public:
            static bool check() {
                return true;
            }
        };
    )");

    std::vector<amoeba::parser::ParsedFile> parsed_files;
    amoeba::index::InvertedIndex index;
    auto p1 = parser_->parse_file(file1);
    index.add_parsed_file(p1);
    parsed_files.push_back(std::move(p1));
    auto p2 = parser_->parse_file(file2);
    index.add_parsed_file(p2);
    parsed_files.push_back(std::move(p2));

    amoeba::graph::RelationshipGraph graph;
    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index, *embedding_provider_);

    amoeba::graph::RelationshipEvidenceResolver rel_resolver(graph, index);
    amoeba::evidence::EvidenceAssembler assembler(*snippet_reader_, rel_resolver);

    amoeba::investigation::BoundedInvestigationService investigator(index, graph, pipeline, *snippet_reader_);

    const std::string query = "Where is AuthenticationService verify_token implemented?";
    amoeba::retrieval::PrimarySearchOptions search_opts;
    search_opts.max_results = 1;
    const auto hop0_results = pipeline.search("AuthenticationService", search_opts);

    // 1. Hop-1 Investigation
    amoeba::investigation::InvestigationOptions hop1_opts;
    hop1_opts.max_hops = 1;

    amoeba::investigation::InvestigationMetrics hop1_metrics;
    const auto hop1_candidates =
        investigator.investigate_candidates(query, hop0_results, hop1_opts, &hop1_metrics);
    const auto hop1_bundle = investigator.assemble_bundle(query, hop1_candidates, assembler);

    const auto query_rep = amoeba::retrieval::QueryUnderstanding::analyze(query);
    const auto hop1_sufficiency =
        amoeba::evidence::EvidenceSufficiencyChecker::check(query_rep, hop1_bundle);

    // Hop-1 is already sufficient!
    EXPECT_TRUE(hop1_sufficiency.is_sufficient);

    // Conditional gating: Because Hop-1 is sufficient, Hop-2 is NOT executed
    bool hop2_executed = false;
    if (!hop1_sufficiency.is_sufficient) {
        hop2_executed = true;
        amoeba::investigation::InvestigationOptions hop2_opts;
        hop2_opts.max_hops = 2;
        const auto hop2_candidates = investigator.expand_to_hop2(hop1_candidates, hop2_opts);
        (void)hop2_candidates;
    }

    EXPECT_FALSE(hop2_executed);
}
