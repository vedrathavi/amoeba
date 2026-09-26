#include "amoeba/context/context_builder.hpp"
#include "amoeba/engine.hpp"
#include "amoeba/evidence/evidence_assembler.hpp"
#include "amoeba/evidence/evidence_sufficiency.hpp"
#include "amoeba/evidence/semantic_evidence_support.hpp"
#include "amoeba/graph/relationship_evidence_resolver.hpp"
#include "amoeba/graph/repository_graph_builder.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/reasoning/local_llm_runtime.hpp"
#include "amoeba/reasoning/reasoning_service.hpp"
#include "amoeba/reasoning/response_sink.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/retrieval/primary_search_result.hpp"
#include "amoeba/retrieval/retrieval_unit.hpp"
#include "amoeba/scanner/repository_scanner.hpp"
#include "amoeba/semantic/pretrained_embedding_provider.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace std;
using namespace std::filesystem;

void print_header() {
    cout << amoeba::get_name() << "\n";
    cout << amoeba::get_description() << "\n\n";
}

void print_usage(string_view program_name) {
    cout << "Usage:\n";
    cout << "  " << program_name
         << " index <repository-path>           Scan a repository for source files\n";
    cout << "  " << program_name
         << " parse <file-path>                 Parse a source file and display structure\n";
    cout << "  " << program_name << " inspect <file-path>               Alias for parse\n";
    cout << "  " << program_name << " search <repository-path> <query> [mode]\n"
         << "                                        Search for primary symbols\n"
         << "                                        Modes: code_aware, bm25, baseline, "
            "semantic, hybrid\n";
    cout << "  " << program_name
         << " ask <repository-path> <question> [--model=<name>] [--endpoint=<url>]\n"
         << "                                        Ask a grounded question using local LLM\n";
}

void handle_index_command(const path& repo_path) {
    amoeba::scanner::RepositoryScanner scanner;

    try {
        const auto result = scanner.scan(repo_path);

        cout << "Repository:\n";
        cout << "  " << repo_path.string() << "\n\n";
        cout << "Scan complete.\n\n";
        cout << "Files discovered: " << result.total_files_discovered << "\n";
        cout << "Files included:   " << result.total_files_included() << "\n";
        cout << "Files ignored:    " << result.total_files_ignored << "\n";

        if (!result.files.empty()) {
            map<string_view, size_t> language_counts;
            for (const auto& file : result.files) {
                const auto lang =
                    amoeba::scanner::RepositoryScanner::get_language_name(file.extension);
                language_counts[lang]++;
            }

            vector<pair<string_view, size_t>> sorted_counts(language_counts.begin(),
                                                            language_counts.end());
            ranges::sort(sorted_counts, [](const auto& a, const auto& b) {
                if (a.second != b.second) {
                    return a.second > b.second;
                }
                return a.first < b.first;
            });

            cout << "\nLanguage Breakdown:\n";
            for (const auto& [lang, count] : sorted_counts) {
                cout << "  " << lang << ": " << count << "\n";
            }
        }
    } catch (const invalid_argument& ex) {
        cerr << "Error: " << ex.what() << "\n";
    } catch (const exception& ex) {
        cerr << "Unexpected error during scanning: " << ex.what() << "\n";
    }
}

void handle_parse_command(const path& file_path) {
    amoeba::parser::SourceParser parser;

    try {
        const auto parsed = parser.parse_file(file_path);

        cout << "File:\n";
        cout << "  " << file_path.string() << "\n\n";
        cout << "Language:\n";
        cout << "  " << parsed.language << "\n\n";
        cout << "Parsing:\n";
        cout << "  " << (parsed.success ? "success" : "failed");
        if (parsed.has_syntax_errors) {
            cout << " (with syntax errors)";
        }
        cout << "\n\n";

        if (parsed.elements.empty()) {
            cout << "No structural elements identified.\n";
            return;
        }

        cout << "Structural elements:\n";

        auto print_kind_group = [&](amoeba::parser::ElementKind kind, string_view label) {
            const auto group = parsed.get_elements_by_kind(kind);
            if (group.empty()) {
                return;
            }
            cout << "  " << label << ":\n";
            for (const auto& elem : group) {
                cout << "    " << elem.name;
                if (!elem.parent_context.empty()) {
                    cout << " (in " << elem.parent_context << ")";
                }
                if (!elem.detail.empty()) {
                    cout << " [" << elem.detail << "]";
                }
                cout << " (Line " << elem.location.start.line << ")\n";
            }
            cout << "\n";
        };

        print_kind_group(amoeba::parser::ElementKind::Route, "Route / Page");
        print_kind_group(amoeba::parser::ElementKind::Class, "Class");
        print_kind_group(amoeba::parser::ElementKind::Struct, "Struct");
        print_kind_group(amoeba::parser::ElementKind::Interface, "Interface / Type");
        print_kind_group(amoeba::parser::ElementKind::Component, "Component");
        print_kind_group(amoeba::parser::ElementKind::Function, "Function");
        print_kind_group(amoeba::parser::ElementKind::Method, "Method");
        print_kind_group(amoeba::parser::ElementKind::Hook, "Hook");
        print_kind_group(amoeba::parser::ElementKind::Include, "Include / Import");
        print_kind_group(amoeba::parser::ElementKind::Call, "Call");
        print_kind_group(amoeba::parser::ElementKind::JSXComponent, "JSX Component");
        print_kind_group(amoeba::parser::ElementKind::JSXElement, "JSX / HTML Element");
        print_kind_group(amoeba::parser::ElementKind::Attribute, "Attribute / Prop");
        print_kind_group(amoeba::parser::ElementKind::UtilityClass, "Utility Class");
        print_kind_group(amoeba::parser::ElementKind::Selector, "CSS Selector");
        print_kind_group(amoeba::parser::ElementKind::Property, "CSS Property");

    } catch (const invalid_argument& ex) {
        cerr << "Error: " << ex.what() << "\n";
    } catch (const exception& ex) {
        cerr << "Unexpected error during parsing: " << ex.what() << "\n";
    }
}

void handle_search_command(const path& repo_path, string_view query, string_view mode_str,
                           const amoeba::retrieval::PrimarySearchOptions& search_opts) {
    try {
        const auto start_index_time = chrono::high_resolution_clock::now();

        amoeba::scanner::RepositoryScanner scanner;
        const auto scan_result = scanner.scan(repo_path);

        amoeba::parser::SourceParser parser;
        amoeba::index::InvertedIndex index;
        vector<amoeba::parser::ParsedFile> parsed_files;
        parsed_files.reserve(scan_result.files.size());

        for (const auto& file_info : scan_result.files) {
            try {
                auto parsed = parser.parse_file(file_info.path);
                if (parsed.success) {
                    index.add_parsed_file(parsed);
                    parsed_files.push_back(std::move(parsed));
                }
            } catch (...) {
                // Safely skip unreadable individual files during batch scan
            }
        }

        amoeba::semantic::PretrainedEmbeddingProvider embedding_provider;
        amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index,
                                                             embedding_provider);

        const auto end_index_time = chrono::high_resolution_clock::now();
        const auto index_duration_ms =
            chrono::duration_cast<chrono::milliseconds>(end_index_time - start_index_time).count();

        cout << "Repository:\n";
        cout << "  " << repo_path.string() << "\n\n";
        cout << "Query:\n";
        cout << "  \"" << query << "\" [Mode: " << mode_str << "]\n\n";
        cout << "Index statistics:\n";
        cout << "  Files indexed:    " << index.file_count() << "\n";
        cout << "  Symbols indexed:  " << index.element_count() << "\n";
        cout << "  Primary units:    " << pipeline.primary_unit_count() << "\n";
        cout << "  Supporting items: " << pipeline.supporting_element_count() << "\n";
        cout << "  Unique terms:     " << index.term_count() << "\n";
        cout << "  Total postings:   " << index.posting_count() << "\n";
        cout << "  Pipeline build:   " << index_duration_ms << " ms\n\n";

        const auto start_search_time = chrono::high_resolution_clock::now();
        amoeba::retrieval::PipelineMetrics metrics;
        const auto results = pipeline.search_with_metrics(query, search_opts, metrics);
        const auto end_search_time = chrono::high_resolution_clock::now();
        const auto search_duration_us =
            chrono::duration_cast<chrono::microseconds>(end_search_time - start_search_time)
                .count();

        if (results.empty()) {
            cout << "No matching structural elements found.\n";
            return;
        }

        cout << "Search results (" << results.size() << " primary matches found in "
             << search_duration_us << " us):\n\n";

        for (size_t i = 0; i < results.size(); ++i) {
            const auto& res = results[i];
            cout << "[" << (i + 1) << "] "
                 << amoeba::parser::to_string(res.unit.primary_element.kind) << ": "
                 << res.unit.primary_element.name;
            if (!res.unit.primary_element.parent_context.empty()) {
                cout << " (in " << res.unit.primary_element.parent_context << ")";
            }
            if (!res.unit.primary_element.detail.empty()) {
                cout << " [" << res.unit.primary_element.detail << "]";
            }
            cout << " [score: " << res.hybrid_score
                 << ", provenance: " << amoeba::retrieval::to_string(res.provenance) << "]\n";
            cout << "    File: " << res.unit.file_path.string() << ":"
                 << res.unit.primary_element.location.start.line << ":"
                 << res.unit.primary_element.location.start.column << " (" << res.unit.language
                 << ")\n";

            if (!res.unit.supporting_elements.empty()) {
                cout << "    Supporting evidence:\n";
                for (const auto& ev : res.unit.supporting_elements) {
                    cout << "      - " << amoeba::parser::to_string(ev.kind) << ": " << ev.name;
                    if (!ev.detail.empty()) {
                        cout << " [" << ev.detail << "]";
                    }
                    cout << " (Line " << ev.location.start.line << ")\n";
                }
            }
            cout << "\n";
        }

    } catch (const invalid_argument& ex) {
        cerr << "Error: " << ex.what() << "\n";
    } catch (const exception& ex) {
        cerr << "Unexpected error during search: " << ex.what() << "\n";
    }
}

void handle_ask_command(const path& repo_path, string_view question,
                        const amoeba::reasoning::LocalLLMConfig& llm_config) {
    try {
        // 1. Scan repository
        amoeba::scanner::RepositoryScanner scanner;
        const auto scan_result = scanner.scan(repo_path);

        // 2. Parse source files & populate index
        amoeba::parser::SourceParser parser;
        amoeba::index::InvertedIndex index;
        vector<amoeba::parser::ParsedFile> parsed_files;
        parsed_files.reserve(scan_result.files.size());

        for (const auto& file_info : scan_result.files) {
            try {
                auto parsed = parser.parse_file(file_info.path);
                if (parsed.success) {
                    index.add_parsed_file(parsed);
                    parsed_files.push_back(std::move(parsed));
                }
            } catch (...) {
            }
        }

        // 3. Build cross-file relationship graph
        amoeba::graph::RelationshipGraph graph;
        amoeba::graph::RepositoryGraphBuilder::build_repository_graph(parsed_files, graph);

        // 4. Initialize pipeline, assembler, and context builder
        amoeba::semantic::PretrainedEmbeddingProvider embedding_provider;
        amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index,
                                                             embedding_provider);
        amoeba::graph::RelationshipEvidenceResolver rel_resolver(graph, index);
        amoeba::evidence::EvidenceAssembler assembler(rel_resolver);

        amoeba::context::ContextBuilderOptions ctx_opts;
        ctx_opts.max_primary_items = 3;
        ctx_opts.max_source_lines_per_item = 50;
        ctx_opts.max_relationships_per_item = 5;
        ctx_opts.max_character_budget = 4000;
        amoeba::context::ContextBuilder context_builder(ctx_opts);

        // 5. Execute retrieval
        const auto start_retrieval = chrono::high_resolution_clock::now();
        amoeba::retrieval::PrimarySearchOptions search_opts{
            .alpha = 0.5,
            .lexical_ranker = amoeba::index::RankerType::CodeAware,
            .max_results = 5,
            .adaptive_fusion = true,
        };
        const auto results = pipeline.search(question, search_opts);

        // 6. Assemble evidence
        const auto bundle = assembler.assemble(question, results);

        // 7. Evidence Sufficiency Gate with Semantic Evidence Support
        const auto query_rep = amoeba::retrieval::QueryUnderstanding::analyze(question);
        amoeba::evidence::SemanticEvidenceSupport sem_support(embedding_provider,
                                                              &pipeline.semantic_index());
        const auto sem_result = sem_support.evaluate(query_rep, bundle);

        const auto sufficiency =
            amoeba::evidence::EvidenceSufficiencyChecker::check(query_rep, bundle, sem_result);
        if (!sufficiency.is_sufficient) {
            const auto end_check = chrono::high_resolution_clock::now();
            const auto check_duration_ms =
                chrono::duration_cast<chrono::milliseconds>(end_check - start_retrieval).count();

            cout << "Repository:\n";
            cout << "  " << repo_path.string() << "\n\n";
            cout << "Question:\n";
            cout << "  \"" << question << "\"\n\n";
            cout << "Grounding Context:\n";
            cout << "  Evaluated " << bundle.size() << " candidate code units in "
                 << check_duration_ms << " ms [Status: Insufficient Evidence]\n\n";
            cout << "Answer:\n";
            cout << sufficiency.format_grounded_refusal(question) << "\n";
            return;
        }

        // 8. Build context for sufficient evidence
        const auto context_package = context_builder.build(bundle);
        const auto end_context = chrono::high_resolution_clock::now();

        const auto retrieval_duration_ms =
            chrono::duration_cast<chrono::milliseconds>(end_context - start_retrieval).count();

        cout << "Repository:\n";
        cout << "  " << repo_path.string() << "\n\n";
        cout << "Question:\n";
        cout << "  \"" << question << "\"\n\n";
        cout << "Grounding Context:\n";
        cout << "  Retrieved " << context_package.selected_item_count << " primary code units ("
             << context_package.used_characters << " chars) in " << retrieval_duration_ms
             << " ms\n";
        cout << "  Local LLM Model: " << llm_config.model_name << " (" << llm_config.endpoint
             << ")\n\n";
        cout << "Answer:\n";

        // 9. Stream answer from LocalLLMRuntime
        amoeba::reasoning::LocalLLMRuntime runtime(llm_config);
        amoeba::reasoning::ReasoningService reasoning_service(runtime);

        const auto start_llm = chrono::high_resolution_clock::now();
        bool has_output = false;

        amoeba::reasoning::CallbackResponseSink sink(
            [&](const amoeba::reasoning::ReasoningEvent& event) {
                if (event.type == amoeba::reasoning::ReasoningEventType::TextChunk) {
                    cout << event.text_chunk;
                    cout.flush();
                    has_output = true;
                } else if (event.type == amoeba::reasoning::ReasoningEventType::Error) {
                    cerr << "\n[Error] " << event.error_message << "\n";
                }
            });

        reasoning_service.answer_stream(string(question), context_package, sink);
        const auto end_llm = chrono::high_resolution_clock::now();
        const auto llm_duration_ms =
            chrono::duration_cast<chrono::milliseconds>(end_llm - start_llm).count();

        if (has_output) {
            cout << "\n\n";
        }
        cout << "[Inference time: " << llm_duration_ms << " ms]\n\n";

    } catch (const invalid_argument& ex) {
        cerr << "Error: " << ex.what() << "\n";
    } catch (const exception& ex) {
        cerr << "Unexpected error during reasoning: " << ex.what() << "\n";
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        print_header();

        if (argc < 2) {
            print_usage(argc > 0 ? argv[0] : "amoeba");
            return 0;
        }

        const string_view command = argv[1];

        if (command == "help" || command == "--help" || command == "-h") {
            print_usage(argv[0]);
            return 0;
        }

        if (command == "index") {
            if (argc < 3) {
                cerr << "Error: Repository path is required.\n\n";
                print_usage(argv[0]);
                return 1;
            }

            const path repo_path(argv[2]);
            handle_index_command(repo_path);
            return 0;
        }

        if (command == "parse" || command == "inspect") {
            if (argc < 3) {
                cerr << "Error: File path is required.\n\n";
                print_usage(argv[0]);
                return 1;
            }

            const path file_path(argv[2]);
            handle_parse_command(file_path);
            return 0;
        }

        if (command == "search") {
            if (argc < 4) {
                cerr << "Error: Repository path and query string are required.\n";
                cerr << "Usage: " << argv[0] << " search <repository-path> <query> [mode]\n"
                     << "       Modes: code_aware, bm25, baseline, semantic, hybrid\n\n";
                return 1;
            }

            const path repo_path(argv[2]);
            const string_view query = argv[3];

            amoeba::retrieval::PrimarySearchOptions search_opts{
                .alpha = 1.0,
                .lexical_ranker = amoeba::index::RankerType::CodeAware,
            };
            string mode_str = "CodeAware";

            if (argc >= 5) {
                const string_view rank_arg = argv[4];
                if (rank_arg == "--ranker=baseline" || rank_arg == "baseline") {
                    search_opts.alpha = 1.0;
                    search_opts.lexical_ranker = amoeba::index::RankerType::Baseline;
                    mode_str = "Baseline";
                } else if (rank_arg == "--ranker=bm25" || rank_arg == "bm25") {
                    search_opts.alpha = 1.0;
                    search_opts.lexical_ranker = amoeba::index::RankerType::BM25;
                    mode_str = "BM25";
                } else if (rank_arg == "--ranker=code_aware" || rank_arg == "code_aware") {
                    search_opts.alpha = 1.0;
                    search_opts.lexical_ranker = amoeba::index::RankerType::CodeAware;
                    mode_str = "CodeAware";
                } else if (rank_arg == "--ranker=semantic" || rank_arg == "semantic" ||
                           rank_arg == "--semantic") {
                    search_opts.alpha = 0.0;
                    mode_str = "Semantic";
                } else if (rank_arg == "--ranker=hybrid" || rank_arg == "hybrid" ||
                           rank_arg == "--hybrid") {
                    search_opts.alpha = 0.5;
                    search_opts.lexical_ranker = amoeba::index::RankerType::CodeAware;
                    search_opts.adaptive_fusion = true;
                    mode_str = "Hybrid";
                }
            }

            handle_search_command(repo_path, query, mode_str, search_opts);
            return 0;
        }

        if (command == "ask") {
            if (argc < 4) {
                cerr << "Error: Repository path and question are required.\n";
                cerr << "Usage: " << argv[0]
                     << " ask <repository-path> <question> [--model=<name>] [--endpoint=<url>]\n\n";
                return 1;
            }

            const path repo_path(argv[2]);
            const string_view question = argv[3];

            amoeba::reasoning::LocalLLMConfig llm_config;
            for (int i = 4; i < argc; ++i) {
                const string_view arg = argv[i];
                if (arg.rfind("--model=", 0) == 0) {
                    llm_config.model_name = string(arg.substr(8));
                } else if (arg.rfind("--endpoint=", 0) == 0) {
                    llm_config.endpoint = string(arg.substr(11));
                }
            }

            handle_ask_command(repo_path, question, llm_config);
            return 0;
        }

        cerr << "Error: Unknown command '" << command << "'.\n\n";
        print_usage(argv[0]);
        return 1;
    } catch (const exception& ex) {
        cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    } catch (...) {
        cerr << "Unknown fatal error occurred.\n";
        return 1;
    }
}
