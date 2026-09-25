#include "amoeba/graph/relationship_graph.hpp"

#include <algorithm>
#include <queue>

namespace amoeba::graph {

bool RelationshipGraph::add_relationship(const Relationship& rel) {
    auto [it, inserted] = edges_.insert(rel);
    if (!inserted) {
        return false;
    }

    outgoing_adj_[rel.source].push_back(rel);
    incoming_adj_[rel.target].push_back(rel);

    node_degree_[rel.source]++;
    node_degree_[rel.target]++;

    return true;
}

bool RelationshipGraph::add_relationship(ElementId source, ElementId target,
                                         RelationshipKind kind) {
    return add_relationship(Relationship{source, target, kind});
}

bool RelationshipGraph::remove_relationship(const Relationship& rel) {
    auto it = edges_.find(rel);
    if (it == edges_.end()) {
        return false;
    }

    edges_.erase(it);

    // Remove from outgoing adjacency
    if (auto out_it = outgoing_adj_.find(rel.source); out_it != outgoing_adj_.end()) {
        std::erase(out_it->second, rel);
        if (out_it->second.empty()) {
            outgoing_adj_.erase(out_it);
        }
    }

    // Remove from incoming adjacency
    if (auto in_it = incoming_adj_.find(rel.target); in_it != incoming_adj_.end()) {
        std::erase(in_it->second, rel);
        if (in_it->second.empty()) {
            incoming_adj_.erase(in_it);
        }
    }

    // Decrement degrees
    if (auto deg_it = node_degree_.find(rel.source); deg_it != node_degree_.end()) {
        if (--(deg_it->second) == 0) {
            node_degree_.erase(deg_it);
        }
    }
    if (auto deg_it = node_degree_.find(rel.target); deg_it != node_degree_.end()) {
        if (--(deg_it->second) == 0) {
            node_degree_.erase(deg_it);
        }
    }

    return true;
}

bool RelationshipGraph::remove_relationship(ElementId source, ElementId target,
                                            RelationshipKind kind) {
    return remove_relationship(Relationship{source, target, kind});
}

bool RelationshipGraph::has_relationship(const Relationship& rel) const noexcept {
    return edges_.contains(rel);
}

bool RelationshipGraph::has_relationship(ElementId source, ElementId target,
                                         RelationshipKind kind) const noexcept {
    return has_relationship(Relationship{source, target, kind});
}

std::vector<Relationship> RelationshipGraph::outgoing_relationships(ElementId source) const {
    auto it = outgoing_adj_.find(source);
    if (it == outgoing_adj_.end()) {
        return {};
    }
    return it->second;
}

std::vector<Relationship> RelationshipGraph::outgoing_relationships(ElementId source,
                                                                    RelationshipKind kind) const {
    auto it = outgoing_adj_.find(source);
    if (it == outgoing_adj_.end()) {
        return {};
    }

    std::vector<Relationship> results;
    for (const auto& rel : it->second) {
        if (rel.kind == kind) {
            results.push_back(rel);
        }
    }
    return results;
}

std::vector<Relationship> RelationshipGraph::incoming_relationships(ElementId target) const {
    auto it = incoming_adj_.find(target);
    if (it == incoming_adj_.end()) {
        return {};
    }
    return it->second;
}

std::vector<Relationship> RelationshipGraph::incoming_relationships(ElementId target,
                                                                    RelationshipKind kind) const {
    auto it = incoming_adj_.find(target);
    if (it == incoming_adj_.end()) {
        return {};
    }

    std::vector<Relationship> results;
    for (const auto& rel : it->second) {
        if (rel.kind == kind) {
            results.push_back(rel);
        }
    }
    return results;
}

std::vector<Relationship> RelationshipGraph::relationships_between(ElementId source,
                                                                   ElementId target) const {
    auto it = outgoing_adj_.find(source);
    if (it == outgoing_adj_.end()) {
        return {};
    }

    std::vector<Relationship> results;
    for (const auto& rel : it->second) {
        if (rel.target == target) {
            results.push_back(rel);
        }
    }
    return results;
}

std::size_t RelationshipGraph::relationship_count() const noexcept {
    return edges_.size();
}

std::size_t RelationshipGraph::relationship_count(RelationshipKind kind) const noexcept {
    std::size_t count = 0;
    for (const auto& rel : edges_) {
        if (rel.kind == kind) {
            count++;
        }
    }
    return count;
}

std::size_t RelationshipGraph::node_count() const noexcept {
    return node_degree_.size();
}

bool RelationshipGraph::has_node(ElementId id) const noexcept {
    return node_degree_.contains(id);
}

std::vector<ElementId> RelationshipGraph::all_nodes() const {
    std::vector<ElementId> nodes;
    nodes.reserve(node_degree_.size());
    for (const auto& [node_id, _] : node_degree_) {
        nodes.push_back(node_id);
    }
    std::sort(nodes.begin(), nodes.end());
    return nodes;
}

bool RelationshipGraph::empty() const noexcept {
    return edges_.empty();
}

std::vector<ElementId> RelationshipGraph::outgoing_neighbors(ElementId source) const {
    auto it = outgoing_adj_.find(source);
    if (it == outgoing_adj_.end()) {
        return {};
    }

    std::vector<ElementId> neighbors;
    std::unordered_set<ElementId> seen;
    for (const auto& rel : it->second) {
        if (seen.insert(rel.target).second) {
            neighbors.push_back(rel.target);
        }
    }
    return neighbors;
}

std::vector<ElementId> RelationshipGraph::outgoing_neighbors(ElementId source,
                                                             RelationshipKind kind) const {
    auto it = outgoing_adj_.find(source);
    if (it == outgoing_adj_.end()) {
        return {};
    }

    std::vector<ElementId> neighbors;
    std::unordered_set<ElementId> seen;
    for (const auto& rel : it->second) {
        if (rel.kind == kind && seen.insert(rel.target).second) {
            neighbors.push_back(rel.target);
        }
    }
    return neighbors;
}

std::vector<ElementId> RelationshipGraph::incoming_neighbors(ElementId target) const {
    auto it = incoming_adj_.find(target);
    if (it == incoming_adj_.end()) {
        return {};
    }

    std::vector<ElementId> neighbors;
    std::unordered_set<ElementId> seen;
    for (const auto& rel : it->second) {
        if (seen.insert(rel.source).second) {
            neighbors.push_back(rel.source);
        }
    }
    return neighbors;
}

std::vector<ElementId> RelationshipGraph::incoming_neighbors(ElementId target,
                                                             RelationshipKind kind) const {
    auto it = incoming_adj_.find(target);
    if (it == incoming_adj_.end()) {
        return {};
    }

    std::vector<ElementId> neighbors;
    std::unordered_set<ElementId> seen;
    for (const auto& rel : it->second) {
        if (rel.kind == kind && seen.insert(rel.source).second) {
            neighbors.push_back(rel.source);
        }
    }
    return neighbors;
}

std::vector<TraversalStep> RelationshipGraph::traverse(ElementId start_node,
                                                       const TraversalOptions& options) const {
    if (!has_node(start_node)) {
        return {};
    }

    std::vector<TraversalStep> steps;
    std::unordered_set<ElementId> visited;
    std::queue<TraversalStep> queue;

    visited.insert(start_node);
    steps.push_back(TraversalStep{start_node, 0});
    queue.push(TraversalStep{start_node, 0});

    while (!queue.empty()) {
        auto [current_node, current_depth] = queue.front();
        queue.pop();

        if (current_depth >= options.max_depth) {
            continue;
        }

        if (options.direction == TraversalDirection::Outgoing) {
            auto it = outgoing_adj_.find(current_node);
            if (it != outgoing_adj_.end()) {
                for (const auto& rel : it->second) {
                    if (options.kind_filter.has_value() && rel.kind != *options.kind_filter) {
                        continue;
                    }
                    if (visited.insert(rel.target).second) {
                        steps.push_back(TraversalStep{rel.target, current_depth + 1});
                        queue.push(TraversalStep{rel.target, current_depth + 1});
                    }
                }
            }
        } else {
            auto it = incoming_adj_.find(current_node);
            if (it != incoming_adj_.end()) {
                for (const auto& rel : it->second) {
                    if (options.kind_filter.has_value() && rel.kind != *options.kind_filter) {
                        continue;
                    }
                    if (visited.insert(rel.source).second) {
                        steps.push_back(TraversalStep{rel.source, current_depth + 1});
                        queue.push(TraversalStep{rel.source, current_depth + 1});
                    }
                }
            }
        }
    }

    return steps;
}

std::vector<ElementId> RelationshipGraph::reachable_nodes(ElementId start_node,
                                                          const TraversalOptions& options) const {
    auto steps = traverse(start_node, options);
    std::vector<ElementId> reachable;
    reachable.reserve(steps.size());
    for (const auto& step : steps) {
        if (step.depth > 0 || options.max_depth == 0) {
            reachable.push_back(step.node_id);
        }
    }
    return reachable;
}

void RelationshipGraph::clear() noexcept {
    edges_.clear();
    outgoing_adj_.clear();
    incoming_adj_.clear();
    node_degree_.clear();
}

}  // namespace amoeba::graph
