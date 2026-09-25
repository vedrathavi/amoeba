#include "amoeba/graph/graph_query_service.hpp"

#include <algorithm>
#include <unordered_set>

namespace amoeba::graph {

std::vector<ElementId> GraphQueryService::callers(ElementId element_id) const {
    return graph_.incoming_neighbors(element_id, RelationshipKind::Calls);
}

std::vector<ElementId> GraphQueryService::callees(ElementId element_id) const {
    return graph_.outgoing_neighbors(element_id, RelationshipKind::Calls);
}

std::vector<ElementId> GraphQueryService::dependencies(ElementId element_id,
                                                       std::size_t depth) const {
    return graph_.reachable_nodes(element_id, TraversalOptions{
                                                  .direction = TraversalDirection::Outgoing,
                                                  .max_depth = depth,
                                                  .kind_filter = std::nullopt,
                                              });
}

std::vector<ElementId> GraphQueryService::dependents(ElementId element_id,
                                                     std::size_t depth) const {
    return graph_.reachable_nodes(element_id, TraversalOptions{
                                                  .direction = TraversalDirection::Incoming,
                                                  .max_depth = depth,
                                                  .kind_filter = std::nullopt,
                                              });
}

std::vector<ElementId> GraphQueryService::parents(ElementId element_id) const {
    return graph_.incoming_neighbors(element_id, RelationshipKind::Contains);
}

std::vector<ElementId> GraphQueryService::children(ElementId element_id) const {
    return graph_.outgoing_neighbors(element_id, RelationshipKind::Contains);
}

std::vector<ElementId> GraphQueryService::base_types(ElementId element_id) const {
    auto bases = graph_.outgoing_neighbors(element_id, RelationshipKind::InheritsFrom);
    auto ifaces = graph_.outgoing_neighbors(element_id, RelationshipKind::Implements);
    bases.insert(bases.end(), ifaces.begin(), ifaces.end());
    std::sort(bases.begin(), bases.end());
    bases.erase(std::unique(bases.begin(), bases.end()), bases.end());
    return bases;
}

std::vector<ElementId> GraphQueryService::derived_types(ElementId element_id) const {
    auto derived = graph_.incoming_neighbors(element_id, RelationshipKind::InheritsFrom);
    auto impls = graph_.incoming_neighbors(element_id, RelationshipKind::Implements);
    derived.insert(derived.end(), impls.begin(), impls.end());
    std::sort(derived.begin(), derived.end());
    derived.erase(std::unique(derived.begin(), derived.end()), derived.end());
    return derived;
}

FocusedSubgraph GraphQueryService::focused_subgraph(ElementId root_node,
                                                    const TraversalOptions& options) const {
    FocusedSubgraph sub;
    sub.root_node = root_node;

    auto reachable = graph_.reachable_nodes(root_node, options);
    std::vector<ElementId> node_list = {root_node};
    node_list.insert(node_list.end(), reachable.begin(), reachable.end());
    std::sort(node_list.begin(), node_list.end());
    node_list.erase(std::unique(node_list.begin(), node_list.end()), node_list.end());
    sub.nodes = node_list;

    std::unordered_set<ElementId> node_set(node_list.begin(), node_list.end());

    for (ElementId u : node_list) {
        auto outgoing = graph_.outgoing_relationships(u);
        for (const auto& edge : outgoing) {
            if (node_set.contains(edge.target)) {
                if (!options.kind_filter.has_value() || edge.kind == *options.kind_filter) {
                    sub.edges.push_back(edge);
                }
            }
        }
    }

    return sub;
}

FocusedSubgraph GraphQueryService::neighborhood_subgraph(ElementId root_node,
                                                         std::size_t depth) const {
    FocusedSubgraph sub;
    sub.root_node = root_node;

    auto out_nodes =
        graph_.reachable_nodes(root_node, TraversalOptions{
                                              .direction = TraversalDirection::Outgoing,
                                              .max_depth = depth,
                                          });
    auto in_nodes = graph_.reachable_nodes(root_node, TraversalOptions{
                                                          .direction = TraversalDirection::Incoming,
                                                          .max_depth = depth,
                                                      });

    std::vector<ElementId> node_list = {root_node};
    node_list.insert(node_list.end(), out_nodes.begin(), out_nodes.end());
    node_list.insert(node_list.end(), in_nodes.begin(), in_nodes.end());
    std::sort(node_list.begin(), node_list.end());
    node_list.erase(std::unique(node_list.begin(), node_list.end()), node_list.end());
    sub.nodes = node_list;

    std::unordered_set<ElementId> node_set(node_list.begin(), node_list.end());

    for (ElementId u : node_list) {
        auto outgoing = graph_.outgoing_relationships(u);
        for (const auto& edge : outgoing) {
            if (node_set.contains(edge.target)) {
                sub.edges.push_back(edge);
            }
        }
    }

    return sub;
}

FocusedSubgraph GraphQueryService::call_graph_subgraph(ElementId root_node,
                                                       std::size_t depth) const {
    FocusedSubgraph sub;
    sub.root_node = root_node;

    auto out_calls =
        graph_.reachable_nodes(root_node, TraversalOptions{
                                              .direction = TraversalDirection::Outgoing,
                                              .max_depth = depth,
                                              .kind_filter = RelationshipKind::Calls,
                                          });
    auto in_calls = graph_.reachable_nodes(root_node, TraversalOptions{
                                                          .direction = TraversalDirection::Incoming,
                                                          .max_depth = depth,
                                                          .kind_filter = RelationshipKind::Calls,
                                                      });

    std::vector<ElementId> node_list = {root_node};
    node_list.insert(node_list.end(), out_calls.begin(), out_calls.end());
    node_list.insert(node_list.end(), in_calls.begin(), in_calls.end());
    std::sort(node_list.begin(), node_list.end());
    node_list.erase(std::unique(node_list.begin(), node_list.end()), node_list.end());
    sub.nodes = node_list;

    std::unordered_set<ElementId> node_set(node_list.begin(), node_list.end());

    for (ElementId u : node_list) {
        auto outgoing = graph_.outgoing_relationships(u, RelationshipKind::Calls);
        for (const auto& edge : outgoing) {
            if (node_set.contains(edge.target)) {
                sub.edges.push_back(edge);
            }
        }
    }

    return sub;
}

FocusedSubgraph GraphQueryService::type_hierarchy_subgraph(ElementId root_node,
                                                           std::size_t depth) const {
    FocusedSubgraph sub;
    sub.root_node = root_node;

    auto out_inherit =
        graph_.reachable_nodes(root_node, TraversalOptions{
                                              .direction = TraversalDirection::Outgoing,
                                              .max_depth = depth,
                                              .kind_filter = RelationshipKind::InheritsFrom,
                                          });
    auto out_impl =
        graph_.reachable_nodes(root_node, TraversalOptions{
                                              .direction = TraversalDirection::Outgoing,
                                              .max_depth = depth,
                                              .kind_filter = RelationshipKind::Implements,
                                          });
    auto in_inherit =
        graph_.reachable_nodes(root_node, TraversalOptions{
                                              .direction = TraversalDirection::Incoming,
                                              .max_depth = depth,
                                              .kind_filter = RelationshipKind::InheritsFrom,
                                          });
    auto in_impl =
        graph_.reachable_nodes(root_node, TraversalOptions{
                                              .direction = TraversalDirection::Incoming,
                                              .max_depth = depth,
                                              .kind_filter = RelationshipKind::Implements,
                                          });

    std::vector<ElementId> node_list = {root_node};
    node_list.insert(node_list.end(), out_inherit.begin(), out_inherit.end());
    node_list.insert(node_list.end(), out_impl.begin(), out_impl.end());
    node_list.insert(node_list.end(), in_inherit.begin(), in_inherit.end());
    node_list.insert(node_list.end(), in_impl.begin(), in_impl.end());
    std::sort(node_list.begin(), node_list.end());
    node_list.erase(std::unique(node_list.begin(), node_list.end()), node_list.end());
    sub.nodes = node_list;

    std::unordered_set<ElementId> node_set(node_list.begin(), node_list.end());

    for (ElementId u : node_list) {
        auto outgoing = graph_.outgoing_relationships(u);
        for (const auto& edge : outgoing) {
            if ((edge.kind == RelationshipKind::InheritsFrom ||
                 edge.kind == RelationshipKind::Implements) &&
                node_set.contains(edge.target)) {
                sub.edges.push_back(edge);
            }
        }
    }

    return sub;
}

FocusedSubgraph
GraphQueryService::induced_subgraph(const std::vector<ElementId>& element_ids) const {
    FocusedSubgraph sub;
    std::vector<ElementId> sorted_ids = element_ids;
    std::sort(sorted_ids.begin(), sorted_ids.end());
    sorted_ids.erase(std::unique(sorted_ids.begin(), sorted_ids.end()), sorted_ids.end());
    sub.nodes = sorted_ids;

    std::unordered_set<ElementId> node_set(sorted_ids.begin(), sorted_ids.end());

    for (ElementId u : sorted_ids) {
        auto outgoing = graph_.outgoing_relationships(u);
        for (const auto& edge : outgoing) {
            if (node_set.contains(edge.target)) {
                sub.edges.push_back(edge);
            }
        }
    }

    return sub;
}

}  // namespace amoeba::graph
