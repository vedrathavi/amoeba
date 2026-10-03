#include "amoeba/graph/relationship_expander.hpp"

#include "amoeba/index/code_tokenizer.hpp"
#include "amoeba/retrieval/query_understanding.hpp"

#include <algorithm>
#include <cctype>
#include <queue>
#include <tuple>
#include <unordered_map>

namespace amoeba::graph {

namespace {

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

[[nodiscard]] double get_base_relationship_weight(RelationshipKind kind) noexcept {
    switch (kind) {
    case RelationshipKind::Calls:
        return 1.0;
    case RelationshipKind::Contains:
        return 0.9;
    case RelationshipKind::InheritsFrom:
        return 0.75;
    case RelationshipKind::Implements:
        return 0.75;
    case RelationshipKind::References:
        return 0.4;
    case RelationshipKind::Imports:
        return 0.3;
    case RelationshipKind::Includes:
        return 0.3;
    }
    return 0.1;
}

struct FrontierNode {
    index::ElementId element_id{0};
    uint32_t depth{0};
    index::ElementId seed_element_id{0};
    std::string seed_symbol_name;
    double seed_hybrid_score{0.0};
};

bool compare_candidates_deterministic(const ExpandedCandidate& a, const ExpandedCandidate& b) {
    // 1. Higher relevance score first
    if (std::abs(a.relevance_score - b.relevance_score) > 1e-6) {
        return a.relevance_score > b.relevance_score;
    }
    // 2. Lower depth first (1-hop before 2-hop)
    if (a.depth != b.depth) {
        return a.depth < b.depth;
    }
    // 3. Deterministic file path ordering
    const auto a_path = a.file_path.generic_string();
    const auto b_path = b.file_path.generic_string();
    if (a_path != b_path) {
        return a_path < b_path;
    }
    // 4. Source line ordering
    if (a.location.start.line != b.location.start.line) {
        return a.location.start.line < b.location.start.line;
    }
    if (a.location.start.column != b.location.start.column) {
        return a.location.start.column < b.location.start.column;
    }
    // 5. Symbol name
    if (a.name != b.name) {
        return a.name < b.name;
    }
    // 6. ElementId
    return a.element_id < b.element_id;
}

}  // namespace

RelationshipExpander::RelationshipExpander(const RelationshipGraph& graph,
                                           const index::InvertedIndex& index,
                                           const source::SourceSnippetReader& snippet_reader)
    : graph_(graph), index_(index), snippet_reader_(snippet_reader) {}

double RelationshipExpander::compute_candidate_score(
    const ExpandedCandidate& cand,
    const std::vector<std::string>& query_subjects,
    double seed_hybrid_score) const {

    double score = get_base_relationship_weight(cand.relationship_kind);

    // Direction weight
    if (cand.direction == RelationshipDirection::Incoming) {
        score *= 0.95;
    }

    // Depth decay penalty: 1-hop = 1.0, 2-hop = 0.25
    if (cand.depth > 0) {
        score /= static_cast<double>(cand.depth * cand.depth);
    }

    // Seed hybrid score bonus
    score += 0.1 * seed_hybrid_score;

    // Query subject / entity overlap boost
    if (!query_subjects.empty()) {
        const std::string file_str = cand.file_path.generic_string();
        for (const auto& term : query_subjects) {
            if (term.empty()) continue;
            if (contains_icase(cand.name, term)) {
                score += 2.5;
            } else if (contains_icase(file_str, term)) {
                score += 1.5;
            } else if (!cand.parent_context.empty() && contains_icase(cand.parent_context, term)) {
                score += 1.0;
            } else if (!cand.detail.empty() && contains_icase(cand.detail, term)) {
                score += 0.8;
            }
        }
    }

    return score;
}

std::vector<ExpandedCandidate>
RelationshipExpander::expand(std::string_view query,
                             std::span<const retrieval::PrimarySearchResult> seed_results,
                             const ExpansionConfig& config) const {
    if (config.max_depth == 0 || config.max_units == 0 || seed_results.empty() ||
        index_.element_count() == 0 || graph_.relationship_count() == 0) {
        return {};
    }

    // 1. Extract query subjects and key tokens for relevance ranking
    std::vector<std::string> query_subjects;
    if (config.prioritize_query_relevance && !query.empty()) {
        const auto query_rep = retrieval::QueryUnderstanding::analyze(query);
        if (!query_rep.distinguishing_subject_terms.empty()) {
            query_subjects = query_rep.distinguishing_subject_terms;
            for (const auto& gen : query_rep.generic_component_terms) {
                if (std::find(query_subjects.begin(), query_subjects.end(), gen) == query_subjects.end()) {
                    query_subjects.push_back(gen);
                }
            }
        } else if (!query_rep.subject_terms.empty()) {
            query_subjects = query_rep.subject_terms;
        } else {
            for (const auto& cat : query_rep.categorized_terms) {
                query_subjects.push_back(cat.normalized_stem);
            }
        }
    }

    // 2. Initialize visited sets and BFS queue with primary retrieval seeds
    std::unordered_set<index::ElementId> visited_element_ids;
    std::queue<FrontierNode> frontier;

    for (const auto& sr : seed_results) {
        auto seed_id = sr.unit.primary_element_id;
        visited_element_ids.insert(seed_id);

        FrontierNode node{
            .element_id = seed_id,
            .depth = 0,
            .seed_element_id = seed_id,
            .seed_symbol_name = sr.unit.primary_element.name.empty()
                                    ? sr.unit.file_path.filename().string()
                                    : sr.unit.primary_element.name,
            .seed_hybrid_score = sr.hybrid_score,
        };
        frontier.push(std::move(node));
    }

    std::vector<ExpandedCandidate> candidate_pool;

    // 3. Bounded BFS traversal across graph up to config.max_depth
    while (!frontier.empty()) {
        auto current = frontier.front();
        frontier.pop();

        if (current.depth >= config.max_depth) {
            continue;
        }

        const uint32_t next_depth = current.depth + 1;

        // A. Outgoing relationships
        auto out_edges = graph_.outgoing_relationships(current.element_id);
        for (const auto& rel : out_edges) {
            if (!config.allowed_kinds.contains(rel.kind)) {
                continue;
            }
            if (rel.target >= index_.element_count() || visited_element_ids.contains(rel.target)) {
                continue;
            }

            visited_element_ids.insert(rel.target);
            const auto& ie = index_.get_element(rel.target);
            const auto& f = index_.get_file(ie.file_id);

            ExpandedCandidate cand;
            cand.element_id = rel.target;
            cand.name = ie.element.name;
            cand.kind = ie.element.kind;
            cand.file_path = f.file_path;
            cand.location = ie.element.location;
            cand.parent_context = ie.element.parent_context;
            cand.detail = ie.element.detail;

            cand.seed_element_id = current.seed_element_id;
            cand.seed_symbol_name = current.seed_symbol_name;
            cand.relationship_kind = rel.kind;
            cand.direction = RelationshipDirection::Outgoing;
            cand.depth = next_depth;

            cand.relevance_score =
                compute_candidate_score(cand, query_subjects, current.seed_hybrid_score);

            candidate_pool.push_back(cand);

            if (next_depth < config.max_depth) {
                frontier.push(FrontierNode{
                    .element_id = rel.target,
                    .depth = next_depth,
                    .seed_element_id = current.seed_element_id,
                    .seed_symbol_name = current.seed_symbol_name,
                    .seed_hybrid_score = current.seed_hybrid_score,
                });
            }
        }

        // B. Incoming relationships
        auto in_edges = graph_.incoming_relationships(current.element_id);
        for (const auto& rel : in_edges) {
            if (!config.allowed_kinds.contains(rel.kind)) {
                continue;
            }
            if (rel.source >= index_.element_count() || visited_element_ids.contains(rel.source)) {
                continue;
            }

            visited_element_ids.insert(rel.source);
            const auto& ie = index_.get_element(rel.source);
            const auto& f = index_.get_file(ie.file_id);

            ExpandedCandidate cand;
            cand.element_id = rel.source;
            cand.name = ie.element.name;
            cand.kind = ie.element.kind;
            cand.file_path = f.file_path;
            cand.location = ie.element.location;
            cand.parent_context = ie.element.parent_context;
            cand.detail = ie.element.detail;

            cand.seed_element_id = current.seed_element_id;
            cand.seed_symbol_name = current.seed_symbol_name;
            cand.relationship_kind = rel.kind;
            cand.direction = RelationshipDirection::Incoming;
            cand.depth = next_depth;

            cand.relevance_score =
                compute_candidate_score(cand, query_subjects, current.seed_hybrid_score);

            candidate_pool.push_back(cand);

            if (next_depth < config.max_depth) {
                frontier.push(FrontierNode{
                    .element_id = rel.source,
                    .depth = next_depth,
                    .seed_element_id = current.seed_element_id,
                    .seed_symbol_name = current.seed_symbol_name,
                    .seed_hybrid_score = current.seed_hybrid_score,
                });
            }
        }
    }

    if (candidate_pool.empty()) {
        return {};
    }

    // 4. Deterministic candidate ranking
    std::sort(candidate_pool.begin(), candidate_pool.end(), compare_candidates_deterministic);

    // 5. Enforce strict unit and source character budgets
    std::vector<ExpandedCandidate> selected;
    selected.reserve(std::min(candidate_pool.size(), config.max_units));

    std::size_t accumulated_chars = 0;

    for (auto& cand : candidate_pool) {
        if (selected.size() >= config.max_units) {
            break;
        }

        // Lazily extract source code excerpt
        auto exc = snippet_reader_.try_read_range(cand.file_path, cand.location,
                                                  config.snippet_context_lines);
        if (exc.has_value() && !exc->text.empty()) {
            cand.source_excerpt_text = exc->text;
        }

        const std::size_t excerpt_len = cand.source_excerpt_text.size();

        // If not the first unit and exceeds character budget, stop
        if (!selected.empty() &&
            accumulated_chars + excerpt_len > config.max_source_characters) {
            continue;
        }

        accumulated_chars += excerpt_len;
        selected.push_back(std::move(cand));
    }

    return selected;
}

}  // namespace amoeba::graph
