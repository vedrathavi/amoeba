#include "amoeba/search/relationship_aware_search.hpp"

#include <algorithm>
#include <unordered_set>

namespace amoeba::search {

namespace {

std::string format_explanation(const parser::CodeElement& related_elem,
                               graph::RelationshipKind kind, bool is_incoming) {
    std::string rel_name = related_elem.name.empty() ? "(anonymous)" : related_elem.name;
    if (!related_elem.parent_context.empty()) {
        rel_name = related_elem.parent_context + "::" + rel_name;
    }

    switch (kind) {
    case graph::RelationshipKind::Calls:
        return is_incoming ? ("Called by " + rel_name) : ("Calls " + rel_name);
    case graph::RelationshipKind::InheritsFrom:
        return is_incoming ? ("Extended by subclass " + rel_name)
                           : ("Inherits from base class " + rel_name);
    case graph::RelationshipKind::Implements:
        return is_incoming ? ("Implemented by " + rel_name) : ("Implements interface " + rel_name);
    case graph::RelationshipKind::References:
        return is_incoming ? ("Referenced by " + rel_name) : ("References symbol " + rel_name);
    case graph::RelationshipKind::Includes:
        return is_incoming ? ("Included by " + rel_name) : ("Includes file/header " + rel_name);
    case graph::RelationshipKind::Imports:
        return is_incoming ? ("Imported by " + rel_name) : ("Imports module " + rel_name);
    case graph::RelationshipKind::Contains:
        return is_incoming ? ("Contained in parent " + rel_name) : ("Contains member " + rel_name);
    }
    return is_incoming ? ("Connected to " + rel_name) : ("Connected from " + rel_name);
}

}  // namespace

RelationshipAwareSearchEngine::RelationshipAwareSearchEngine(const index::InvertedIndex& index,
                                                             const graph::RelationshipGraph& graph)
    : index_(index), graph_(graph), search_engine_(index_), query_service_(graph_) {}

std::optional<graph::ElementId>
RelationshipAwareSearchEngine::find_element_id(const index::SearchResult& result) const noexcept {
    for (graph::ElementId id = 0; id < index_.element_count(); ++id) {
        const auto& indexed = index_.get_element(id);
        if (indexed.element.location == result.element.location &&
            indexed.element.name == result.element.name &&
            indexed.element.kind == result.element.kind) {
            const auto& fmeta = index_.get_file(indexed.file_id);
            if (fmeta.file_path == result.file_path) {
                return id;
            }
        }
    }
    return std::nullopt;
}

RelationshipAwareSearchResult RelationshipAwareSearchEngine::expand_result(
    const index::SearchResult& result, graph::ElementId element_id,
    const RelationshipExpansionOptions& expansion_opts) const {
    RelationshipAwareSearchResult aware_result;
    aware_result.primary_result = result;
    aware_result.element_id = element_id;

    if (!expansion_opts.enable_expansion) {
        return aware_result;
    }

    aware_result.context_subgraph = query_service_.focused_subgraph(
        element_id, graph::TraversalOptions{
                        .direction = graph::TraversalDirection::Outgoing,
                        .max_depth = expansion_opts.max_depth,
                        .kind_filter = expansion_opts.kind_filter,
                    });

    std::unordered_set<graph::ElementId> seen_related;

    // 1. Process Outgoing Relationships (Callees, Base Types, Dependencies)
    auto outgoing = graph_.outgoing_relationships(element_id);
    for (const auto& rel : outgoing) {
        if (expansion_opts.kind_filter.has_value() && rel.kind != *expansion_opts.kind_filter) {
            continue;
        }
        if (rel.target >= index_.element_count() || seen_related.contains(rel.target)) {
            continue;
        }

        const auto& target_indexed = index_.get_element(rel.target);
        const auto& target_file = index_.get_file(target_indexed.file_id);

        std::string explanation = format_explanation(target_indexed.element, rel.kind, false);

        if (aware_result.related_elements.size() < expansion_opts.max_related_elements) {
            seen_related.insert(rel.target);
            aware_result.related_elements.push_back(RelatedElementContext{
                .element_id = rel.target,
                .element = target_indexed.element,
                .file_path = target_file.file_path,
                .relationship_kind = rel.kind,
                .is_incoming = false,
                .explanation = explanation,
            });
        }
        aware_result.structural_explanations.push_back(std::move(explanation));
    }

    // 2. Process Incoming Relationships (Callers, Subclasses, Dependents)
    auto incoming = graph_.incoming_relationships(element_id);
    for (const auto& rel : incoming) {
        if (expansion_opts.kind_filter.has_value() && rel.kind != *expansion_opts.kind_filter) {
            continue;
        }
        if (rel.source >= index_.element_count() || seen_related.contains(rel.source)) {
            continue;
        }

        const auto& source_indexed = index_.get_element(rel.source);
        const auto& source_file = index_.get_file(source_indexed.file_id);

        std::string explanation = format_explanation(source_indexed.element, rel.kind, true);

        if (aware_result.related_elements.size() < expansion_opts.max_related_elements) {
            seen_related.insert(rel.source);
            aware_result.related_elements.push_back(RelatedElementContext{
                .element_id = rel.source,
                .element = source_indexed.element,
                .file_path = source_file.file_path,
                .relationship_kind = rel.kind,
                .is_incoming = true,
                .explanation = explanation,
            });
        }
        aware_result.structural_explanations.push_back(std::move(explanation));
    }

    return aware_result;
}

std::vector<RelationshipAwareSearchResult>
RelationshipAwareSearchEngine::search(std::string_view query,
                                      const index::SearchOptions& search_opts,
                                      const RelationshipExpansionOptions& expansion_opts) const {
    auto raw_results = search_engine_.search(query, search_opts);

    std::vector<RelationshipAwareSearchResult> aware_results;
    aware_results.reserve(raw_results.size());

    for (const auto& raw : raw_results) {
        if (auto elem_id = find_element_id(raw)) {
            aware_results.push_back(expand_result(raw, *elem_id, expansion_opts));
        } else {
            RelationshipAwareSearchResult item;
            item.primary_result = raw;
            item.element_id = 0;
            aware_results.push_back(std::move(item));
        }
    }

    return aware_results;
}

}  // namespace amoeba::search
