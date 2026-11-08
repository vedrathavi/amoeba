#include "amoeba/context/context_builder.hpp"
#include "amoeba/engine.hpp"
#include "amoeba/evidence/evidence_assembler.hpp"
#include "amoeba/evidence/evidence_sufficiency.hpp"
#include "amoeba/evidence/semantic_evidence_support.hpp"
#include "amoeba/graph/relationship_evidence_resolver.hpp"
#include "amoeba/graph/repository_graph_builder.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/investigation/bounded_investigation_service.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/reasoning/local_llm_runtime.hpp"
#include "amoeba/reasoning/reasoning_service.hpp"
#include "amoeba/reasoning/response_sink.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/retrieval/primary_search_result.hpp"
#include "amoeba/retrieval/query_expansion_service.hpp"
#include "amoeba/retrieval/retrieval_unit.hpp"
#include "amoeba/scanner/repository_scanner.hpp"
#include "amoeba/semantic/pretrained_embedding_provider.hpp"
#include "amoeba/source/source_snippet_reader.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace std;
using namespace std::filesystem;

[[nodiscard]] unique_ptr<amoeba::semantic::EmbeddingProvider>
create_embedding_provider([[maybe_unused]] string_view model_name) {
    return make_unique<amoeba::semantic::PretrainedEmbeddingProvider>();
}

[[nodiscard]] vector<string> parse_comma_separated(string_view s) {
    vector<string> items;
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(',', start);
        if (end == string_view::npos) {
            end = s.size();
        }
        string_view token = s.substr(start, end - start);
        while (!token.empty() && isspace(static_cast<unsigned char>(token.front()))) {
            token.remove_prefix(1);
        }
        while (!token.empty() && isspace(static_cast<unsigned char>(token.back()))) {
            token.remove_suffix(1);
        }
        if (!token.empty()) {
            items.emplace_back(token);
        }
        start = end + 1;
    }
    return items;
}

void print_header() {
    cout << amoeba::get_name() << "\n";
    cout << amoeba::get_description() << "\n\n";
}

void print_usage(string_view program_name) {
    cout << "Usage:\n";
    cout << "  " << program_name
         << " index <repository-path> [--exclude=<dirs>]\n"
         << "                                        Scan a repository for source files\n";
    cout << "  " << program_name
         << " parse <file-path>                 Parse a source file and display structure\n";
    cout << "  " << program_name << " inspect <file-path>               Alias for parse\n";
    cout << "  " << program_name << " search <repository-path> <query> [options]\n"
         << "                                        Search for primary symbols\n"
         << "                                        Modes: code_aware, bm25, baseline, semantic, hybrid\n";
    cout << "  " << program_name
         << " ask <repository-path> <question> [options]\n"
         << "                                        Ask a grounded question using local LLM\n\n";
    cout << "Configuration Options:\n"
         << "  --reservoir=<n>         Candidate reservoir size for initial retrieval (default: 50)\n"
         << "  --candidate-depth=<n>   Alias for --reservoir\n"
         << "  --max-investigation=<n> Maximum candidates explored in bounded investigation (default: 15)\n"
         << "  --max-results=<n>       Maximum results to display or pass to context (default: 5)\n"
         << "  --alpha=<float>         Lexical vs semantic weight in [0.0, 1.0] (default: 0.8)\n"
         << "  --exclude=<dirs>        Comma-separated directory names to exclude during scanning\n"
         << "  --show-skipped          Show explainability details for every skipped file\n"
         << "  --include-large-files   Include files between 2MB and 20MB for controlled indexing\n"
         << "  --max-file-size=<bytes> Normal file size threshold in bytes (default: 2097152 / 2MB)\n"
         << "  --expand-query          Enable context-aware LLM query expansion (disabled by default)\n"
         << "  --model=<name>          LLM model name (default: qwen2.5-coder-7b-instruct)\n"
         << "  --endpoint=<url>        LLM server endpoint (default: http://127.0.0.1:11434/v1)\n"
         << "  --embedding-model=<name>Embedding model name (default: minilm)\n";
}

void handle_index_command(const path& repo_path,
                          const amoeba::scanner::ScanOptions& scan_opts = {}) {
    amoeba::scanner::RepositoryScanner scanner;

    try {
        const auto result = scanner.scan(repo_path, scan_opts);

        cout << "Repository:\n";
        cout << "  " << repo_path.string() << "\n\n";
        cout << "Scan complete.\n\n";
        cout << "Files discovered: " << result.total_files_discovered << "\n";
        cout << "Files included:   " << result.total_files_included() << "\n";
        cout << "Files ignored:    " << result.total_files_ignored << "\n";

        if (result.total_files_ignored > 0) {
            cout << "\nSkip Breakdown:\n";
            if (result.total_files_git_ignored > 0) {
                cout << "  Git ignored (.gitignore):      " << result.total_files_git_ignored << "\n";
            }
            if (result.total_files_amoeba_ignored > 0) {
                cout << "  Amoeba ignored (.amoebaignore): " << result.total_files_amoeba_ignored << "\n";
            }
            if (result.total_files_skipped_above_normal > 0) {
                cout << "  Deferred (> 2MB normal limit): " << result.total_files_skipped_above_normal << "\n";
            }
            const size_t hard_limit_skips =
                (result.total_files_skipped_size >= result.total_files_skipped_above_normal)
                    ? (result.total_files_skipped_size - result.total_files_skipped_above_normal)
                    : 0;
            if (hard_limit_skips > 0) {
                cout << "  Exceeds hard ceiling (> 20MB): " << hard_limit_skips << "\n";
            }
            if (result.total_files_skipped_binary > 0) {
                cout << "  Binary files:                  " << result.total_files_skipped_binary << "\n";
            }
        }

        if (scan_opts.collect_skipped_details && !result.skipped_details.empty()) {
            cout << "\nSkipped Files Details (" << result.skipped_details.size() << " items):\n";
            for (const auto& item : result.skipped_details) {
                cout << "  [" << amoeba::scanner::RepositoryScanner::skip_reason_to_string(item.reason)
                     << "] " << item.relative_path.generic_string()
                     << " (" << item.file_size << " bytes)"
                     << " via " << item.rule_source
                     << (item.matching_rule.empty() ? "" : (" [" + item.matching_rule + "]"))
                     << "\n";
            }
        }

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
                           const amoeba::retrieval::PrimarySearchOptions& search_opts,
                           const amoeba::scanner::ScanOptions& scan_opts = {},
                           string_view embedding_model_name = "minilm") {
    try {
        const auto start_index_time = chrono::high_resolution_clock::now();

        amoeba::scanner::RepositoryScanner scanner;
        const auto scan_result = scanner.scan(repo_path, scan_opts);

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

        auto embedding_provider = create_embedding_provider(embedding_model_name);
        amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index,
                                                             *embedding_provider);

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
                        const amoeba::reasoning::LocalLLMConfig& llm_config,
                        const amoeba::scanner::ScanOptions& scan_opts,
                        const amoeba::retrieval::PrimarySearchOptions& search_opts,
                        const amoeba::investigation::InvestigationOptions& invest_opts,
                        const amoeba::context::ContextBuilderOptions& ctx_opts,
                        const amoeba::retrieval::QueryExpansionOptions& exp_opts,
                        string_view embedding_model_name = "minilm") {
    try {
        // 1. Scan repository
        amoeba::scanner::RepositoryScanner scanner;
        const auto scan_result = scanner.scan(repo_path, scan_opts);

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

        // 4. Initialize pipeline, assembler, context builder, and LLM runtime
        auto embedding_provider = create_embedding_provider(embedding_model_name);
        amoeba::retrieval::PrimaryRetrievalPipeline pipeline(parsed_files, index,
                                                             *embedding_provider);
        amoeba::graph::RelationshipEvidenceResolver rel_resolver(graph, index);
        amoeba::evidence::EvidenceAssembler assembler(rel_resolver);
        amoeba::context::ContextBuilder context_builder(ctx_opts);

        amoeba::reasoning::LocalLLMRuntime runtime(llm_config);
        amoeba::retrieval::QueryExpansionService exp_service(&runtime);

        // 5. Execute retrieval (with optional context-aware query expansion)
        const auto start_retrieval = chrono::high_resolution_clock::now();
        amoeba::retrieval::QueryExpansionResult exp_res;
        const auto hop0_results = exp_service.execute_dual_retrieval(
            pipeline, question, search_opts, exp_opts, &exp_res);

        if (exp_res.expansion_applied) {
            cout << "[Query Expansion]: \"" << exp_res.expanded_query << "\"\n\n";
        }

        // 5.1 Bounded Hop-1 Investigation
        amoeba::source::SourceSnippetReader snippet_reader;
        amoeba::investigation::BoundedInvestigationService investigator(
            index, graph, pipeline, snippet_reader);
        const auto candidates =
            investigator.investigate_candidates(question, hop0_results, invest_opts);

        // 6. Assemble evidence
        const auto bundle = investigator.assemble_bundle(question, candidates, assembler);

        // 7. Evidence Sufficiency Gate with Semantic Evidence Support
        const auto query_rep = amoeba::retrieval::QueryUnderstanding::analyze(question);
        amoeba::evidence::SemanticEvidenceSupport sem_support(*embedding_provider,
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

        if (command == "index" || command == "scan") {
            if (argc < 3) {
                cerr << "Error: Repository path is required.\n\n";
                print_usage(argv[0]);
                return 1;
            }

            const path repo_path(argv[2]);
            amoeba::scanner::ScanOptions scan_opts;
            for (int i = 3; i < argc; ++i) {
                const string_view arg = argv[i];
                if (arg.rfind("--exclude=", 0) == 0) {
                    scan_opts.custom_exclusions = parse_comma_separated(arg.substr(10));
                } else if (arg == "--show-skipped") {
                    scan_opts.collect_skipped_details = true;
                } else if (arg == "--include-large-files") {
                    scan_opts.include_large_files = true;
                } else if (arg.rfind("--max-file-size=", 0) == 0) {
                    scan_opts.normal_file_size_bytes = stoull(string(arg.substr(16)));
                } else if (arg.rfind("--hard-max-file-size=", 0) == 0) {
                    scan_opts.hard_max_file_size_bytes = stoull(string(arg.substr(21)));
                }
            }
            handle_index_command(repo_path, scan_opts);
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
                cerr << "Usage: " << argv[0] << " search <repository-path> <query> [options]\n"
                     << "       Modes: code_aware, bm25, baseline, semantic, hybrid\n\n";
                return 1;
            }

            const path repo_path(argv[2]);
            const string_view query = argv[3];

            amoeba::retrieval::PrimarySearchOptions search_opts{
                .alpha = 0.8,
                .lexical_ranker = amoeba::index::RankerType::CodeAware,
                .max_results = 10,
            };
            amoeba::scanner::ScanOptions scan_opts;
            string embedding_model = "minilm";
            string mode_str = "CodeAware";

            for (int i = 4; i < argc; ++i) {
                const string_view arg = argv[i];
                if (arg.rfind("--embedding-model=", 0) == 0) {
                    embedding_model = string(arg.substr(18));
                } else if (arg.rfind("--embedding=", 0) == 0) {
                    embedding_model = string(arg.substr(12));
                } else if (arg.rfind("--alpha=", 0) == 0) {
                    search_opts.alpha = stod(string(arg.substr(8)));
                } else if (arg.rfind("--max-results=", 0) == 0) {
                    search_opts.max_results = stoull(string(arg.substr(14)));
                } else if (arg.rfind("--reservoir=", 0) == 0) {
                    search_opts.max_results = stoull(string(arg.substr(12)));
                } else if (arg.rfind("--candidate-depth=", 0) == 0) {
                    search_opts.max_results = stoull(string(arg.substr(18)));
                } else if (arg.rfind("--exclude=", 0) == 0) {
                    scan_opts.custom_exclusions = parse_comma_separated(arg.substr(10));
                } else if (arg == "--show-skipped") {
                    scan_opts.collect_skipped_details = true;
                } else if (arg == "--include-large-files") {
                    scan_opts.include_large_files = true;
                } else if (arg.rfind("--max-file-size=", 0) == 0) {
                    scan_opts.normal_file_size_bytes = stoull(string(arg.substr(16)));
                } else if (arg == "--ranker=baseline" || arg == "baseline") {
                    search_opts.alpha = 1.0;
                    search_opts.lexical_ranker = amoeba::index::RankerType::Baseline;
                    mode_str = "Baseline";
                } else if (arg == "--ranker=bm25" || arg == "bm25") {
                    search_opts.alpha = 1.0;
                    search_opts.lexical_ranker = amoeba::index::RankerType::BM25;
                    mode_str = "BM25";
                } else if (arg == "--ranker=code_aware" || arg == "code_aware") {
                    search_opts.alpha = 1.0;
                    search_opts.lexical_ranker = amoeba::index::RankerType::CodeAware;
                    mode_str = "CodeAware";
                } else if (arg == "--ranker=semantic" || arg == "semantic" || arg == "--semantic") {
                    search_opts.alpha = 0.0;
                    mode_str = "Semantic";
                } else if (arg == "--ranker=hybrid" || arg == "hybrid" || arg == "--hybrid") {
                    search_opts.alpha = 0.8;
                    search_opts.lexical_ranker = amoeba::index::RankerType::CodeAware;
                    search_opts.adaptive_fusion = false;
                    mode_str = "Hybrid";
                }
            }

            handle_search_command(repo_path, query, mode_str, search_opts, scan_opts,
                                  embedding_model);
            return 0;
        }

        if (command == "ask") {
            if (argc < 4) {
                cerr << "Error: Repository path and question are required.\n";
                cerr << "Usage: " << argv[0]
                     << " ask <repository-path> <question> [options]\n\n";
                return 1;
            }

            const path repo_path(argv[2]);
            const string_view question = argv[3];

            amoeba::reasoning::LocalLLMConfig llm_config;
            amoeba::retrieval::PrimarySearchOptions search_opts{
                .alpha = 0.8,
                .lexical_ranker = amoeba::index::RankerType::CodeAware,
                .max_results = 50,  // Candidate reservoir depth
                .adaptive_fusion = false,
            };
            amoeba::investigation::InvestigationOptions invest_opts{
                .max_total_candidates = 15,  // Investigation limit
            };
            amoeba::context::ContextBuilderOptions ctx_opts{
                .max_primary_items = 5,  // Final display / context limit
                .max_source_lines_per_item = 50,
                .max_relationships_per_item = 5,
                .max_character_budget = 4000,
            };
            amoeba::retrieval::QueryExpansionOptions exp_opts{
                .enabled = false,  // Disabled by default
            };
            amoeba::scanner::ScanOptions scan_opts;
            string embedding_model = "minilm";

            for (int i = 4; i < argc; ++i) {
                const string_view arg = argv[i];
                if (arg.rfind("--model=", 0) == 0) {
                    llm_config.model_name = string(arg.substr(8));
                } else if (arg.rfind("--endpoint=", 0) == 0) {
                    llm_config.endpoint = string(arg.substr(11));
                } else if (arg.rfind("--embedding-model=", 0) == 0) {
                    embedding_model = string(arg.substr(18));
                } else if (arg.rfind("--embedding=", 0) == 0) {
                    embedding_model = string(arg.substr(12));
                } else if (arg.rfind("--alpha=", 0) == 0) {
                    search_opts.alpha = stod(string(arg.substr(8)));
                } else if (arg.rfind("--reservoir=", 0) == 0) {
                    search_opts.max_results = stoull(string(arg.substr(12)));
                } else if (arg.rfind("--candidate-depth=", 0) == 0) {
                    search_opts.max_results = stoull(string(arg.substr(18)));
                } else if (arg.rfind("--max-investigation=", 0) == 0) {
                    invest_opts.max_total_candidates =
                        static_cast<uint32_t>(stoul(string(arg.substr(20))));
                } else if (arg.rfind("--max-results=", 0) == 0) {
                    ctx_opts.max_primary_items = stoull(string(arg.substr(14)));
                } else if (arg.rfind("--exclude=", 0) == 0) {
                    scan_opts.custom_exclusions = parse_comma_separated(arg.substr(10));
                } else if (arg == "--show-skipped") {
                    scan_opts.collect_skipped_details = true;
                } else if (arg == "--include-large-files") {
                    scan_opts.include_large_files = true;
                } else if (arg.rfind("--max-file-size=", 0) == 0) {
                    scan_opts.normal_file_size_bytes = stoull(string(arg.substr(16)));
                } else if (arg == "--expand-query" || arg == "--expand") {
                    exp_opts.enabled = true;
                } else if (arg == "--no-expand-query") {
                    exp_opts.enabled = false;
                } else if (arg == "--adaptive-fusion") {
                    search_opts.adaptive_fusion = true;
                } else if (arg == "--no-adaptive-fusion") {
                    search_opts.adaptive_fusion = false;
                }
            }

            handle_ask_command(repo_path, question, llm_config, scan_opts, search_opts,
                               invest_opts, ctx_opts, exp_opts, embedding_model);
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
