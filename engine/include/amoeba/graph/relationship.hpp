#pragma once

#include "amoeba/graph/relationship_kind.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace amoeba::graph {

using ElementId = uint32_t;

/**
 * @brief Directed relationship edge connecting two CodeElements by their ElementIds.
 * Does not duplicate source code or CodeElement objects.
 */
struct Relationship {
    ElementId source{0};
    ElementId target{0};
    RelationshipKind kind{RelationshipKind::References};

    constexpr bool operator==(const Relationship& other) const noexcept = default;
    constexpr auto operator<=>(const Relationship& other) const noexcept = default;
};

/**
 * @brief Hash functor for Relationship to allow storage in unordered containers.
 */
struct RelationshipHash {
    [[nodiscard]] std::size_t operator()(const Relationship& rel) const noexcept {
        // Boost-style hash combine
        std::size_t seed = std::hash<uint32_t>{}(rel.source);
        seed ^= std::hash<uint32_t>{}(rel.target) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= std::hash<uint8_t>{}(static_cast<uint8_t>(rel.kind)) + 0x9e3779b9 + (seed << 6) +
                (seed >> 2);
        return seed;
    }
};

}  // namespace amoeba::graph

namespace std {

template <> struct hash<amoeba::graph::Relationship> {
    [[nodiscard]] std::size_t operator()(const amoeba::graph::Relationship& rel) const noexcept {
        return amoeba::graph::RelationshipHash{}(rel);
    }
};

}  // namespace std
