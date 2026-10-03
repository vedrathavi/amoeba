// Amoeba — Phase 8.2.8 Forensic Accuracy Investigation & Evidence Expansion
//
// Performs deep investigation of all 70 benchmark questions across 10 repositories:
// - Audits current pipeline behavior
// - Classifies failures (A: Retrieval, B: Evidence/Sufficiency, C: Reasoning, D: External)
// - For Category B: evaluates 1-hop relationship expansion across all relationship kinds
// - Measures minimum required expansion (0, 1, 2, 3+ units), character/token budgets, noise
// - Evaluates negative/adversarial refusal safety
// - Measures latency overhead

#include "amoeba/context/context_builder.hpp"
#include "amoeba/evidence/evidence_assembler.hpp"
#include "amoeba/evidence/evidence_bundle.hpp"
#include "amoeba/evidence/evidence_sufficiency.hpp"
#include "amoeba/graph/relationship_evidence_resolver.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/graph/repository_graph_builder.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/scanner/repository_scanner.hpp"
#include "amoeba/semantic/deterministic_embedding_provider.hpp"
#include "amoeba/source/source_snippet_reader.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

struct QueryItem {
    std::string question_id;
    std::string repo_name;
    std::string source;
    std::string taxonomy;
    std::string difficulty;
    std::string question_text;
    std::vector<std::string> expected_files;
    std::vector<std::string> expected_symbols;
    std::vector<std::string> expected_relationships;
    std::vector<std::string> expected_concepts;
};

[[nodiscard]] std::string escape_json(std::string_view s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) <= 0x1f) {
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                      << static_cast<int>(c);
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

[[nodiscard]] bool path_matches(std::string_view retrieved, std::string_view expected) {
    if (retrieved.empty() || expected.empty()) return false;
    std::string r(retrieved);
    std::string e(expected);
    std::replace(r.begin(), r.end(), '\\', '/');
    std::replace(e.begin(), e.end(), '\\', '/');

    if (r == e) return true;
    if (r.ends_with(e) || e.ends_with(r)) return true;
    if (r.find(e) != std::string::npos || e.find(r) != std::string::npos) return true;

    auto r_fname = fs::path(r).filename().string();
    auto e_fname = fs::path(e).filename().string();
    return (!r_fname.empty() && r_fname == e_fname);
}

[[nodiscard]] bool symbol_matches(std::string_view retrieved, std::string_view expected) {
    if (retrieved.empty() || expected.empty()) return false;
    if (retrieved == expected) return true;
    if (retrieved.find(expected) != std::string::npos || expected.find(retrieved) != std::string::npos) return true;
    return false;
}

[[nodiscard]] bool contains_icase(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                          [](char a, char b) {
                              return std::tolower(static_cast<unsigned char>(a)) ==
                                     std::tolower(static_cast<unsigned char>(b));
                          });
    return it != haystack.end();
}

[[nodiscard]] std::vector<QueryItem> load_queries_tsv(const fs::path& tsv_path) {
    std::vector<QueryItem> queries;
    std::ifstream ifs(tsv_path);
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::stringstream ss(line);
        std::string qid, repo, src, tax, diff, qtext, files_str, syms_str, rels_str, concepts_str;
        if (!std::getline(ss, qid, '\t')) continue;
        if (!std::getline(ss, repo, '\t')) continue;
        if (!std::getline(ss, src, '\t')) continue;
        if (!std::getline(ss, tax, '\t')) continue;
        if (!std::getline(ss, diff, '\t')) continue;
        if (!std::getline(ss, qtext, '\t')) continue;
        std::getline(ss, files_str, '\t');
        std::getline(ss, syms_str, '\t');
        std::getline(ss, rels_str, '\t');
        std::getline(ss, concepts_str, '\t');

        QueryItem item;
        item.question_id = qid;
        item.repo_name = repo;
        item.source = src;
        item.taxonomy = tax;
        item.difficulty = diff;
        item.question_text = qtext;

        auto split_pipe = [](const std::string& str) {
            std::vector<std::string> out;
            std::stringstream pss(str);
            std::string tok;
            while (std::getline(pss, tok, '|')) {
                if (!tok.empty()) out.push_back(tok);
            }
            return out;
        };

        item.expected_files = split_pipe(files_str);
        item.expected_symbols = split_pipe(syms_str);
        item.expected_relationships = split_pipe(rels_str);
        item.expected_concepts = split_pipe(concepts_str);
        queries.push_back(std::move(item));
    }
    return queries;
}

struct OneHopNeighbor {
    amoeba::index::ElementId target_id{0};
    std::string name;
    amoeba::parser::ElementKind kind{amoeba::parser::ElementKind::Unknown};
    fs::path file_path;
    amoeba::graph::RelationshipKind rel_kind;
    amoeba::graph::RelationshipDirection direction;
    std::string excerpt;
    bool matches_missing_subject{false};
    bool matches_expected_concept{false};
    bool matches_expected_symbol{false};
};

struct QuestionAnalysisResult {
    std::string question_id;
    std::string repo_name;
    std::string failure_category; // "correct", "retrieval_failure", "evidence_failure", "reasoning_failure", "negative_refusal"
    bool is_negative{false};
    bool hit_at_10{false};
    bool base_sufficiency{false};
    std::string base_sufficiency_reason;

    // 1-hop expansion investigation (for Cat B & negatives)
    std::size_t total_1hop_neighbors{0};
    std::size_t useful_neighbors{0};
    std::size_t noisy_neighbors{0};
    std::size_t min_units_required{0}; // 0 = base sufficient, 1 = +1 unit, 2 = +2 units, 99 = unreachable
    bool expansion_solves_sufficiency{false};
    std::size_t added_source_chars{0};
    std::size_t added_elements{0};

    // Latency
    double retrieval_ms{0.0};
    double base_sufficiency_ms{0.0};
    double expansion_resolution_ms{0.0};
    double expanded_sufficiency_ms{0.0};

    // Relationship kind utility counts
    std::map<std::string, int> rel_kind_useful;
    std::map<std::string, int> rel_kind_noisy;

    std::vector<std::string> useful_neighbor_descriptions;
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: amoeba_accuracy_investigation <repo_dir> <queries_tsv_file> <output_json_file>\n";
        return 1;
    }

    fs::path repo_dir = argv[1];
    fs::path tsv_file = argv[2];
    fs::path out_file = argv[3];

    if (!fs::exists(repo_dir)) {
        std::cerr << "ERROR: Repo directory does not exist: " << repo_dir << "\n";
        return 1;
    }

    if (!fs::exists(tsv_file)) {
        std::cerr << "ERROR: Queries file does not exist: " << tsv_file << "\n";
        return 1;
    }

    auto queries = load_queries_tsv(tsv_file);
    std::cout << "Loaded " << queries.size() << " queries for " << repo_dir.filename().string() << "\n";

    amoeba::semantic::DeterministicEmbeddingProvider embedding_provider{32};
    amoeba::source::SourceSnippetReader snippet_reader;
    amoeba::parser::SourceParser parser;

    // 1. Scan & Parse
    amoeba::scanner::RepositoryScanner scanner;
    auto scan_res = scanner.scan(repo_dir);

    amoeba::index::InvertedIndex index;
    amoeba::graph::RelationshipGraph graph;
    std::vector<amoeba::parser::ParsedFile> parsed_files;

    for (const auto& f : scan_res.files) {
        auto pf = parser.parse_file(f.path);
        if (pf.success) {
            index.add_parsed_file(pf);
            parsed_files.push_back(std::move(pf));
        }
    }

    // 2. Build full relationship graph
    auto graph_result = amoeba::graph::RepositoryGraphBuilder::build_repository_graph(index, graph);
    std::cout << "  Indexed " << index.file_count() << " files, " << index.element_count()
              << " elements, " << graph.relationship_count() << " relationships.\n";

    // 3. Build Retrieval Pipeline & Evidence Resolver
    auto rel_resolver = std::make_unique<amoeba::graph::RelationshipEvidenceResolver>(graph, index);
    auto pipeline = std::make_unique<amoeba::retrieval::PrimaryRetrievalPipeline>(
        parsed_files, index, embedding_provider);
    amoeba::evidence::EvidenceAssembler assembler(snippet_reader, *rel_resolver);

    std::vector<QuestionAnalysisResult> all_results;
    std::map<std::string, int> global_rel_useful;
    std::map<std::string, int> global_rel_noisy;
    int total_positive = 0;
    int total_negative = 0;
    int cat_a_count = 0;
    int cat_b_count = 0;
    int cat_c_count = 0;
    int correct_count = 0;
    int cat_b_solved_by_1hop = 0;

        for (const auto& q : queries) {
            QuestionAnalysisResult qar;
            qar.question_id = q.question_id;
            qar.repo_name = q.repo_name;
            qar.is_negative = q.expected_files.empty() && q.expected_symbols.empty();

            if (qar.is_negative) total_negative++;
            else total_positive++;

            // Step A: Primary Retrieval
            auto t_ret_s = Clock::now();
            amoeba::retrieval::PrimarySearchOptions opts;
            opts.max_results = 10;
            auto search_results = pipeline->search(q.question_text, opts);
            qar.retrieval_ms = std::chrono::duration<double, std::milli>(Clock::now() - t_ret_s).count();

            // Check Hit@10
            bool hit_10 = false;
            std::vector<std::size_t> hit_indices;
            for (std::size_t i = 0; i < search_results.size(); ++i) {
                const auto& sr = search_results[i];
                auto f_str = sr.unit.file_path.generic_string();
                bool hit = false;
                for (const auto& exp_f : q.expected_files) {
                    if (path_matches(f_str, exp_f)) {
                        hit = true;
                        break;
                    }
                }
                if (!hit) {
                    for (const auto& exp_s : q.expected_symbols) {
                        if (symbol_matches(sr.unit.primary_element.name, exp_s)) {
                            hit = true;
                            break;
                        }
                    }
                }
                if (hit) {
                    hit_10 = true;
                    hit_indices.push_back(i);
                }
            }
            qar.hit_at_10 = hit_10;

            // Step B: Base Evidence Bundle & Base Sufficiency
            auto base_bundle = assembler.assemble(q.question_text, search_results);
            auto t_suff_s = Clock::now();
            auto base_suff = amoeba::evidence::EvidenceSufficiencyChecker::check(q.question_text, base_bundle);
            qar.base_sufficiency_ms = std::chrono::duration<double, std::milli>(Clock::now() - t_suff_s).count();
            qar.base_sufficiency = base_suff.is_sufficient;
            qar.base_sufficiency_reason = base_suff.reason;

            // Category assignment
            if (qar.is_negative) {
                qar.failure_category = (!base_suff.is_sufficient) ? "negative_refusal_correct" : "negative_refusal_false_pass";
                if (!base_suff.is_sufficient) correct_count++;
            } else {
                if (!hit_10) {
                    qar.failure_category = "retrieval_failure";
                    cat_a_count++;
                } else if (base_suff.is_sufficient) {
                    qar.failure_category = "correct";
                    correct_count++;
                } else {
                    qar.failure_category = "evidence_failure";
                    cat_b_count++;
                }
            }

            // Step C: 1-Hop Relationship Forensic Investigation (for ALL Category B and Negative questions)
            auto t_exp_s = Clock::now();
            std::vector<OneHopNeighbor> candidate_neighbors;
            std::set<amoeba::index::ElementId> seen_element_ids;

            // Mark already retrieved elements as seen
            for (const auto& sr : search_results) {
                seen_element_ids.insert(sr.unit.primary_element_id);
            }

            // Collect 1-hop neighbors for all retrieved candidates
            for (std::size_t rank = 0; rank < search_results.size(); ++rank) {
                const auto& sr = search_results[rank];
                auto elem_id = sr.unit.primary_element_id;

                // Outgoing edges
                auto out_edges = graph.outgoing_relationships(elem_id);
                for (const auto& rel : out_edges) {
                    if (seen_element_ids.count(rel.target)) continue;
                    seen_element_ids.insert(rel.target);

                    if (rel.target < index.element_count()) {
                        const auto& ie = index.get_element(rel.target);
                        const auto& f = index.get_file(ie.file_id);
                        OneHopNeighbor n;
                        n.target_id = rel.target;
                        n.name = ie.element.name;
                        n.kind = ie.element.kind;
                        n.file_path = f.file_path;
                        n.rel_kind = rel.kind;
                        n.direction = amoeba::graph::RelationshipDirection::Outgoing;
                        candidate_neighbors.push_back(std::move(n));
                    }
                }

                // Incoming edges
                auto in_edges = graph.incoming_relationships(elem_id);
                for (const auto& rel : in_edges) {
                    if (seen_element_ids.count(rel.source)) continue;
                    seen_element_ids.insert(rel.source);

                    if (rel.source < index.element_count()) {
                        const auto& ie = index.get_element(rel.source);
                        const auto& f = index.get_file(ie.file_id);
                        OneHopNeighbor n;
                        n.target_id = rel.source;
                        n.name = ie.element.name;
                        n.kind = ie.element.kind;
                        n.file_path = f.file_path;
                        n.rel_kind = rel.kind;
                        n.direction = amoeba::graph::RelationshipDirection::Incoming;
                        candidate_neighbors.push_back(std::move(n));
                    }
                }
            }
            qar.expansion_resolution_ms = std::chrono::duration<double, std::milli>(Clock::now() - t_exp_s).count();
            qar.total_1hop_neighbors = candidate_neighbors.size();

            // Analyze relevance of each neighbor
            std::vector<OneHopNeighbor> useful_list;
            std::vector<OneHopNeighbor> noisy_list;

            for (auto& n : candidate_neighbors) {
                std::string k_str = std::string(amoeba::graph::to_string(n.rel_kind));
                bool relevant = false;

                // Check if neighbor matches missing subjects from base sufficiency
                for (const auto& ms : base_suff.missing_subjects) {
                    if (contains_icase(n.name, ms) || contains_icase(n.file_path.string(), ms)) {
                        n.matches_missing_subject = true;
                        relevant = true;
                    }
                }

                // Check if neighbor matches expected concepts
                for (const auto& ec : q.expected_concepts) {
                    if (contains_icase(n.name, ec) || contains_icase(n.file_path.string(), ec)) {
                        n.matches_expected_concept = true;
                        relevant = true;
                    }
                }

                // Check if neighbor matches expected symbols
                for (const auto& es : q.expected_symbols) {
                    if (symbol_matches(n.name, es)) {
                        n.matches_expected_symbol = true;
                        relevant = true;
                    }
                }

                if (relevant) {
                    // Lazily fetch excerpt only for relevant neighbors
                    if (n.target_id < index.element_count()) {
                        const auto& ie = index.get_element(n.target_id);
                        const auto& f = index.get_file(ie.file_id);
                        auto exc = snippet_reader.try_read_range(f.file_path, ie.element.location, 5);
                        if (exc.has_value()) n.excerpt = exc->text;
                    }
                    useful_list.push_back(n);
                    qar.rel_kind_useful[k_str]++;
                    global_rel_useful[k_str]++;
                } else {
                    noisy_list.push_back(n);
                    qar.rel_kind_noisy[k_str]++;
                    global_rel_noisy[k_str]++;
                }
            }

            qar.useful_neighbors = useful_list.size();
            qar.noisy_neighbors = noisy_list.size();

            for (const auto& u : useful_list) {
                std::string desc = std::string(amoeba::graph::to_string(u.rel_kind)) + " -> " + u.name + " (" + u.file_path.filename().string() + ")";
                qar.useful_neighbor_descriptions.push_back(desc);
            }

            // Test Sufficiency with Bounded 1-Hop Expansion
            // Simulate expanding the bundle with top useful neighbors (or 1..N neighbors)
            auto t_esuff_s = Clock::now();
            amoeba::evidence::EvidenceBundle expanded_bundle = base_bundle;
            std::size_t units_needed = 99;

            // Test 1-by-1 useful expansion
            for (std::size_t ui = 0; ui < useful_list.size(); ++ui) {
                const auto& u = useful_list[ui];
                amoeba::evidence::EvidenceItem ei;
                ei.primary_result.unit.primary_element.name = u.name;
                ei.primary_result.unit.primary_element.kind = u.kind;
                ei.primary_result.unit.file_path = u.file_path;
                ei.primary_result.unit.primary_element_id = u.target_id;
                if (!u.excerpt.empty()) {
                    amoeba::source::SourceExcerpt sexc;
                    sexc.text = u.excerpt;
                    sexc.start_line = 1;
                    sexc.end_line = 10;
                    ei.source_excerpt = sexc;
                }
                expanded_bundle.items.push_back(std::move(ei));
                qar.added_source_chars += u.excerpt.size();
                qar.added_elements++;

                auto exp_suff = amoeba::evidence::EvidenceSufficiencyChecker::check(q.question_text, expanded_bundle);
                if (exp_suff.is_sufficient) {
                    units_needed = ui + 1;
                    qar.expansion_solves_sufficiency = true;
                    break;
                }
            }
            qar.expanded_sufficiency_ms = std::chrono::duration<double, std::milli>(Clock::now() - t_esuff_s).count();
            qar.min_units_required = qar.base_sufficiency ? 0 : units_needed;

            if (qar.failure_category == "evidence_failure" && qar.expansion_solves_sufficiency) {
                cat_b_solved_by_1hop++;
            }

            all_results.push_back(qar);
        }

    std::cout << "\n=================================================================\n";
    std::cout << "INVESTIGATION RESULTS: " << repo_dir.filename().string() << "\n";
    std::cout << "=================================================================\n\n";

    std::cout << "Queries Evaluated: " << all_results.size() << "\n";

    // Write full json report to out_file
    std::ofstream ofs(out_file);
    ofs << "[\n";
    for (std::size_t i = 0; i < all_results.size(); ++i) {
        const auto& r = all_results[i];
        ofs << "  {\n";
        ofs << "    \"question_id\": \"" << escape_json(r.question_id) << "\",\n";
        ofs << "    \"repository\": \"" << escape_json(r.repo_name) << "\",\n";
        ofs << "    \"failure_category\": \"" << escape_json(r.failure_category) << "\",\n";
        ofs << "    \"is_negative\": " << (r.is_negative ? "true" : "false") << ",\n";
        ofs << "    \"hit_at_10\": " << (r.hit_at_10 ? "true" : "false") << ",\n";
        ofs << "    \"base_sufficiency\": " << (r.base_sufficiency ? "true" : "false") << ",\n";
        ofs << "    \"base_sufficiency_reason\": \"" << escape_json(r.base_sufficiency_reason) << "\",\n";
        ofs << "    \"total_1hop_neighbors\": " << r.total_1hop_neighbors << ",\n";
        ofs << "    \"useful_neighbors\": " << r.useful_neighbors << ",\n";
        ofs << "    \"noisy_neighbors\": " << r.noisy_neighbors << ",\n";
        ofs << "    \"min_units_required\": " << r.min_units_required << ",\n";
        ofs << "    \"expansion_solves_sufficiency\": " << (r.expansion_solves_sufficiency ? "true" : "false") << ",\n";
        ofs << "    \"added_source_chars\": " << r.added_source_chars << ",\n";
        ofs << "    \"added_elements\": " << r.added_elements << ",\n";
        ofs << "    \"retrieval_ms\": " << r.retrieval_ms << ",\n";
        ofs << "    \"base_sufficiency_ms\": " << r.base_sufficiency_ms << ",\n";
        ofs << "    \"expansion_resolution_ms\": " << r.expansion_resolution_ms << ",\n";
        ofs << "    \"expanded_sufficiency_ms\": " << r.expanded_sufficiency_ms << ",\n";
        ofs << "    \"rel_kind_useful\": {\n";
        bool first_ru = true;
        for (const auto& [k, v] : r.rel_kind_useful) {
            if (!first_ru) ofs << ",\n";
            first_ru = false;
            ofs << "      \"" << k << "\": " << v;
        }
        ofs << "\n    },\n";
        ofs << "    \"rel_kind_noisy\": {\n";
        bool first_rn = true;
        for (const auto& [k, v] : r.rel_kind_noisy) {
            if (!first_rn) ofs << ",\n";
            first_rn = false;
            ofs << "      \"" << k << "\": " << v;
        }
        ofs << "\n    },\n";
        ofs << "    \"useful_descriptions\": [";
        for (std::size_t j = 0; j < r.useful_neighbor_descriptions.size(); ++j) {
            ofs << "\"" << escape_json(r.useful_neighbor_descriptions[j]) << "\"" << (j + 1 < r.useful_neighbor_descriptions.size() ? ", " : "");
        }
        ofs << "]\n";
        ofs << "  }" << (i + 1 < all_results.size() ? ",\n" : "\n");
    }
    ofs << "]\n";
    std::cout << "Detailed results written to " << out_file << "\n";
    return 0;
}

