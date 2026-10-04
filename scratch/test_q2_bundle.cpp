#include "amoeba/context/context_builder.hpp"
#include "amoeba/evidence/evidence_assembler.hpp"
#include "amoeba/evidence/evidence_bundle.hpp"
#include "amoeba/evidence/evidence_sufficiency.hpp"
#include "amoeba/graph/relationship_evidence_resolver.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/graph/repository_graph_builder.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/investigation/bounded_investigation_service.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/scanner/repository_scanner.hpp"
#include "amoeba/semantic/deterministic_embedding_provider.hpp"
#include "amoeba/source/source_snippet_reader.hpp"
#include <iostream>

int main() {
    std::filesystem::path repo_dir = "d:/amoeba/benchmarks/repoprobe_v1/repos/pkl";
    amoeba::scanner::RepositoryScanner scanner;
    auto scan_res = scanner.scan(repo_dir);

    amoeba::parser::SourceParser parser;
    amoeba::index::InvertedIndex index;
    amoeba::graph::RelationshipGraph graph;
    std::vector<amoeba::parser::ParsedFile> parsed_files;

    for (const auto& file_meta : scan_res.files) {
        auto pf = parser.parse_file(file_meta.path);
        if (pf.success) {
            index.add_parsed_file(pf);
            parsed_files.push_back(std::move(pf));
        }
    }

    amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);
    amoeba::source::SourceSnippetReader snippet_reader;
    amoeba::semantic::DeterministicEmbeddingProvider embedding_provider{32};
    auto rel_resolver = std::make_unique<amoeba::graph::RelationshipEvidenceResolver>(graph, index);
    auto pipeline = std::make_unique<amoeba::retrieval::PrimaryRetrievalPipeline>(
        parsed_files, index, embedding_provider);
    amoeba::investigation::BoundedInvestigationService investigator(
        index, graph, *pipeline, snippet_reader);
    amoeba::evidence::EvidenceAssembler assembler(snippet_reader, *rel_resolver);

    std::string qtext = "Where is the Spring Boot auto-configuration controller implemented in pkl?";
    amoeba::retrieval::PrimarySearchOptions search_opts;
    search_opts.max_results = 10;
    auto search_results = pipeline->search(qtext, search_opts);

    amoeba::investigation::InvestigationMetrics invest_metrics;
    amoeba::investigation::InvestigationOptions invest_opts;
    invest_opts.max_total_candidates = 10;
    auto investigated_candidates = investigator.investigate_candidates(qtext, search_results, invest_opts, &invest_metrics);
    auto bundle = investigator.assemble_bundle(qtext, investigated_candidates, assembler);

    std::cout << "Bundle items: " << bundle.items.size() << "\n";
    for (size_t i = 0; i < bundle.items.size(); ++i) {
        auto& it = bundle.items[i];
        std::cout << "  Item " << i << ": " << it.primary_element().name 
                  << " (expanded=" << (it.is_expanded_relationship ? "true" : "false") << ")\n";
    }

    auto suff = amoeba::evidence::EvidenceSufficiencyChecker::check(qtext, bundle);
    std::cout << "is_sufficient: " << (suff.is_sufficient ? "true" : "false") << "\n";
    std::cout << "reason: " << suff.reason << "\n";
    std::cout << "matched_subjects: "; for (auto& s : suff.matched_subjects) std::cout << s << " "; std::cout << "\n";
    std::cout << "missing_subjects: "; for (auto& s : suff.missing_subjects) std::cout << s << " "; std::cout << "\n";
    return 0;
}
