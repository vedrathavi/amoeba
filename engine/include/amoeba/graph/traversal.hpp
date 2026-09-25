#pragma once

#include "amoeba/graph/relationship.hpp"
#include "amoeba/graph/relationship_kind.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace amoeba::graph {

/**
 * @brief Direction of graph edge traversal.
 */
enum class TraversalDirection : uint8_t {
    Outgoing = 0,  ///< Traverse along directed edges (source -> target)
    Incoming       ///< Traverse against directed edges (target -> source)
};

/**
 * @brief Configuration parameters for depth-bounded graph traversal.
 */
struct TraversalOptions {
    TraversalDirection direction{TraversalDirection::Outgoing};
    std::size_t max_depth{1};  ///< Maximum traversal search depth (hops)
    std::optional<RelationshipKind> kind_filter{
        std::nullopt};  ///< Optional filter by relationship kind
};

/**
 * @brief Representation of a node reached during traversal with its search depth.
 */
struct TraversalStep {
    ElementId node_id{0};
    std::size_t depth{0};

    constexpr bool operator==(const TraversalStep& other) const noexcept = default;
    constexpr auto operator<=>(const TraversalStep& other) const noexcept = default;
};

}  // namespace amoeba::graph
