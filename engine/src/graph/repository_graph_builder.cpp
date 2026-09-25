#include "amoeba/graph/repository_graph_builder.hpp"

#include <unordered_map>
#include <vector>

namespace amoeba::graph {

GraphBuildResult RepositoryGraphBuilder::build_repository_graph(const index::InvertedIndex& index,
                                                                RelationshipGraph& graph) {
    GraphBuildResult result;

    if (index.file_count() == 0 || index.element_count() == 0) {
        return result;
    }

    // 1. Compute total source bytes across indexed files
    std::size_t total_source_bytes = 0;
    std::unordered_map<index::FileId, std::size_t> max_byte_per_file;
    for (ElementId id = 0; id < index.element_count(); ++id) {
        const auto& elem = index.get_element(id);
        std::size_t end_byte = elem.element.location.end.byte_offset;
        if (end_byte == 0) {
            end_byte = elem.element.location.end.line * 35;
        }
        max_byte_per_file[elem.file_id] = std::max(max_byte_per_file[elem.file_id], end_byte);
    }
    for (const auto& [_, bytes] : max_byte_per_file) {
        total_source_bytes += bytes;
    }
    if (total_source_bytes == 0 && index.element_count() > 0) {
        total_source_bytes = index.element_count() * 50;
    }
    result.metrics.total_source_bytes = total_source_bytes;
    result.metrics.total_files = index.file_count();
    result.metrics.total_elements = index.element_count();

    // 2. Extract structural CONTAINS hierarchy (Class -> Method/Field, Namespace -> Class)
    std::unordered_map<index::FileId, std::unordered_map<std::string, ElementId>>
        containers_by_file;
    for (ElementId id = 0; id < index.element_count(); ++id) {
        const auto& elem = index.get_element(id);
        if (elem.element.kind == parser::ElementKind::Class ||
            elem.element.kind == parser::ElementKind::Struct ||
            elem.element.kind == parser::ElementKind::Interface ||
            elem.element.kind == parser::ElementKind::Component) {
            containers_by_file[elem.file_id][elem.element.name] = id;
        }
    }

    for (ElementId id = 0; id < index.element_count(); ++id) {
        const auto& elem = index.get_element(id);
        if (elem.element.kind == parser::ElementKind::Method ||
            elem.element.kind == parser::ElementKind::Function ||
            elem.element.kind == parser::ElementKind::Property ||
            elem.element.kind == parser::ElementKind::Hook) {
            if (!elem.element.parent_context.empty()) {
                auto file_it = containers_by_file.find(elem.file_id);
                if (file_it != containers_by_file.end()) {
                    auto parent_it = file_it->second.find(elem.element.parent_context);
                    if (parent_it != file_it->second.end()) {
                        graph.add_relationship(parent_it->second, id, RelationshipKind::Contains);
                    }
                }
            }
        }
    }

    // 3. Extract Imports & Includes (Phase 5.2)
    result.import_result = ImportExtractor::extract_and_populate(index, graph);

    // 4. Extract Inheritance & Implements (Phase 5.3)
    result.inheritance_result = InheritanceExtractor::extract_and_populate(index, graph);

    // 5. Extract Calls & References (Phase 5.4)
    result.call_result = CallExtractor::extract_and_populate(index, graph);

    // 6. Aggregate detailed metrics
    result.metrics.contains_relationships = graph.relationship_count(RelationshipKind::Contains);
    result.metrics.import_relationships = graph.relationship_count(RelationshipKind::Imports);
    result.metrics.include_relationships = graph.relationship_count(RelationshipKind::Includes);
    result.metrics.inheritance_relationships =
        graph.relationship_count(RelationshipKind::InheritsFrom);
    result.metrics.implements_relationships =
        graph.relationship_count(RelationshipKind::Implements);
    result.metrics.call_relationships = graph.relationship_count(RelationshipKind::Calls);
    result.metrics.reference_relationships = graph.relationship_count(RelationshipKind::References);

    result.metrics.file_level_relationships = result.metrics.contains_relationships +
                                              result.metrics.import_relationships +
                                              result.metrics.include_relationships;

    result.metrics.symbol_level_relationships =
        result.metrics.inheritance_relationships + result.metrics.implements_relationships +
        result.metrics.call_relationships + result.metrics.reference_relationships;

    result.metrics.total_relationships = graph.relationship_count();
    result.metrics.estimated_graph_memory_bytes = graph.estimate_memory_bytes();

    if (total_source_bytes > 0) {
        result.metrics.graph_to_source_ratio =
            static_cast<double>(result.metrics.estimated_graph_memory_bytes) /
            static_cast<double>(total_source_bytes);
    } else {
        result.metrics.graph_to_source_ratio = 0.0;
    }

    return result;
}

GraphBuildResult
RepositoryGraphBuilder::build_repository_graph(const std::vector<parser::ParsedFile>& files,
                                               RelationshipGraph& graph) {
    index::InvertedIndex index;
    for (const auto& file : files) {
        index.add_parsed_file(file);
    }
    return build_repository_graph(index, graph);
}

}  // namespace amoeba::graph
