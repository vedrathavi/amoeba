#include "amoeba/graph/relationship_evidence_resolver.hpp"

#include <algorithm>
#include <map>

namespace amoeba::graph {

namespace {

bool compare_evidence(const RelationshipEvidence& a, const RelationshipEvidence& b) {
    if (a.direction != b.direction) {
        return static_cast<uint8_t>(a.direction) < static_cast<uint8_t>(b.direction);
    }
    if (a.kind != b.kind) {
        return static_cast<uint8_t>(a.kind) < static_cast<uint8_t>(b.kind);
    }
    const auto a_path = a.related_file_path.generic_string();
    const auto b_path = b.related_file_path.generic_string();
    if (a_path != b_path) {
        return a_path < b_path;
    }
    if (a.related_location.start.line != b.related_location.start.line) {
        return a.related_location.start.line < b.related_location.start.line;
    }
    if (a.related_location.start.column != b.related_location.start.column) {
        return a.related_location.start.column < b.related_location.start.column;
    }
    if (a.related_name != b.related_name) {
        return a.related_name < b.related_name;
    }
    return a.related_element_id < b.related_element_id;
}

}  // namespace

RelationshipEvidenceResolver::RelationshipEvidenceResolver(const RelationshipGraph& graph,
                                                           const index::InvertedIndex& index)
    : graph_(graph), index_(index) {}

void RelationshipEvidenceResolver::populate_related_metadata(ElementId target_id,
                                                             RelationshipEvidence& evidence) const {
    if (target_id < index_.element_count()) {
        try {
            const auto& ie = index_.get_element(target_id);
            const auto& file = index_.get_file(ie.file_id);
            evidence.related_name = ie.element.name;
            evidence.related_kind = ie.element.kind;
            evidence.related_file_path = file.file_path;
            evidence.related_location = ie.element.location;
            evidence.related_parent_context = ie.element.parent_context;
            evidence.related_detail = ie.element.detail;
        } catch (...) {
            evidence.related_kind = parser::ElementKind::Unknown;
        }
    }
}

std::vector<RelationshipEvidence>
RelationshipEvidenceResolver::resolve(ElementId primary_element_id,
                                      const RelationshipEvidenceOptions& options) const {
    std::vector<RelationshipEvidence> result;

    if (!graph_.has_node(primary_element_id)) {
        return result;
    }

    // 1. Resolve outgoing relationships
    if (options.include_outgoing) {
        const auto out_edges = graph_.outgoing_relationships(primary_element_id);

        std::map<RelationshipKind, std::vector<RelationshipEvidence>> out_by_kind;
        for (const auto& rel : out_edges) {
            RelationshipEvidence ev;
            ev.primary_element_id = primary_element_id;
            ev.related_element_id = rel.target;
            ev.kind = rel.kind;
            ev.direction = RelationshipDirection::Outgoing;
            populate_related_metadata(rel.target, ev);
            out_by_kind[rel.kind].push_back(std::move(ev));
        }

        for (auto& [kind, items] : out_by_kind) {
            std::sort(items.begin(), items.end(), compare_evidence);
            const std::size_t count = std::min(items.size(), options.max_outgoing_per_kind);
            for (std::size_t i = 0; i < count; ++i) {
                result.push_back(std::move(items[i]));
            }
        }
    }

    // 2. Resolve incoming relationships
    if (options.include_incoming) {
        const auto in_edges = graph_.incoming_relationships(primary_element_id);

        std::map<RelationshipKind, std::vector<RelationshipEvidence>> in_by_kind;
        for (const auto& rel : in_edges) {
            RelationshipEvidence ev;
            ev.primary_element_id = primary_element_id;
            ev.related_element_id = rel.source;
            ev.kind = rel.kind;
            ev.direction = RelationshipDirection::Incoming;
            populate_related_metadata(rel.source, ev);
            in_by_kind[rel.kind].push_back(std::move(ev));
        }

        for (auto& [kind, items] : in_by_kind) {
            std::sort(items.begin(), items.end(), compare_evidence);
            const std::size_t count = std::min(items.size(), options.max_incoming_per_kind);
            for (std::size_t i = 0; i < count; ++i) {
                result.push_back(std::move(items[i]));
            }
        }
    }

    std::sort(result.begin(), result.end(), compare_evidence);
    return result;
}

std::vector<RelationshipEvidence>
RelationshipEvidenceResolver::resolve(const retrieval::RetrievalUnit& unit,
                                      const RelationshipEvidenceOptions& options) const {
    return resolve(unit.primary_element_id, options);
}

std::vector<RelationshipEvidence>
RelationshipEvidenceResolver::resolve(const retrieval::PrimarySearchResult& result,
                                      const RelationshipEvidenceOptions& options) const {
    return resolve(result.unit.primary_element_id, options);
}

}  // namespace amoeba::graph
