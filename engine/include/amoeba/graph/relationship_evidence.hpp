#pragma once

#include "amoeba/graph/relationship.hpp"
#include "amoeba/graph/relationship_kind.hpp"
#include "amoeba/parser/code_element.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace amoeba::graph {

/**
 * @brief Direction of the graph relationship relative to the focal primary symbol.
 */
enum class RelationshipDirection : uint8_t {
    Outgoing =
        0,  ///< Primary symbol is source (e.g. Primary CALLS Target, Primary INHERITS_FROM Target)
    Incoming =
        1  ///< Primary symbol is target (e.g. Target CALLS Primary, Target INHERITS_FROM Primary)
};

[[nodiscard]] constexpr std::string_view to_string(RelationshipDirection dir) noexcept {
    switch (dir) {
    case RelationshipDirection::Outgoing:
        return "Outgoing";
    case RelationshipDirection::Incoming:
        return "Incoming";
    }
    return "Outgoing";
}

/**
 * @brief Structured, model-independent representation of a direct 1-hop graph relationship.
 *
 * Encapsulates the relationship kind, direction, and resolved target symbol metadata
 * without exposing internal graph storage details or raw pointers.
 */
struct RelationshipEvidence {
    ElementId primary_element_id{0};  ///< The focal primary element ID
    ElementId related_element_id{0};  ///< The adjacent related element ID in the graph
    RelationshipKind kind{RelationshipKind::References};
    RelationshipDirection direction{RelationshipDirection::Outgoing};

    // Resolved metadata of the adjacent related element (from InvertedIndex)
    std::string related_name;
    parser::ElementKind related_kind{parser::ElementKind::Unknown};
    std::filesystem::path related_file_path;
    parser::SourceRange related_location;
    std::string related_parent_context;
    std::string related_detail;

    [[nodiscard]] bool operator==(const RelationshipEvidence&) const = default;
};

}  // namespace amoeba::graph
