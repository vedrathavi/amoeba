#pragma once

#include "amoeba/graph/relationship.hpp"
#include "amoeba/graph/relationship_kind.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace amoeba::graph {

/**
 * @brief In-memory representation of a focused subgraph around one or more focal code elements.
 *
 * Designed to provide contextual slices (call hierarchy, type hierarchy, file dependencies)
 * without duplicating AST tokens or repository text.
 */
struct FocusedSubgraph {
    ElementId root_node{0};           ///< Primary focal element (if centered around a root)
    std::vector<ElementId> nodes;     ///< All unique node ElementIds in the subgraph (sorted)
    std::vector<Relationship> edges;  ///< All directed relationship edges in the subgraph

    [[nodiscard]] std::size_t node_count() const noexcept { return nodes.size(); }
    [[nodiscard]] std::size_t edge_count() const noexcept { return edges.size(); }
    [[nodiscard]] bool empty() const noexcept { return nodes.empty() && edges.empty(); }

    [[nodiscard]] bool contains_node(ElementId id) const noexcept {
        return std::binary_search(nodes.begin(), nodes.end(), id);
    }

    [[nodiscard]] bool contains_edge(const Relationship& rel) const noexcept {
        return std::find(edges.begin(), edges.end(), rel) != edges.end();
    }

    [[nodiscard]] std::vector<Relationship> outgoing_edges(ElementId source) const {
        std::vector<Relationship> out;
        for (const auto& edge : edges) {
            if (edge.source == source) {
                out.push_back(edge);
            }
        }
        return out;
    }

    [[nodiscard]] std::vector<Relationship> incoming_edges(ElementId target) const {
        std::vector<Relationship> in;
        for (const auto& edge : edges) {
            if (edge.target == target) {
                in.push_back(edge);
            }
        }
        return in;
    }

    [[nodiscard]] std::vector<Relationship> edges_by_kind(RelationshipKind kind) const {
        std::vector<Relationship> filtered;
        for (const auto& edge : edges) {
            if (edge.kind == kind) {
                filtered.push_back(edge);
            }
        }
        return filtered;
    }
};

}  // namespace amoeba::graph
