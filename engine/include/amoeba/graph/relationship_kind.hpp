#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace amoeba::graph {

/**
 * @brief Fundamental relationship kinds between code elements.
 * Higher-level patterns (e.g., MVC, Factory) are derived rather than represented as primitive
 * edges.
 */
enum class RelationshipKind : uint8_t {
    Contains = 0,  ///< Structural hierarchy (e.g., Class contains Method, File contains Function)
    Imports,       ///< Module or package import (e.g., import in JS/Python/Java/Go)
    Includes,      ///< Header or file inclusion (e.g., #include in C/C++)
    Calls,         ///< Function/method invocation
    References,    ///< Variable, type, or symbol reference/usage
    InheritsFrom,  ///< Class or struct inheritance / subtyping
    Implements     ///< Interface or abstract contract implementation
};

/**
 * @brief Returns the string representation of a RelationshipKind.
 */
[[nodiscard]] constexpr std::string_view to_string(RelationshipKind kind) noexcept {
    switch (kind) {
    case RelationshipKind::Contains:
        return "CONTAINS";
    case RelationshipKind::Imports:
        return "IMPORTS";
    case RelationshipKind::Includes:
        return "INCLUDES";
    case RelationshipKind::Calls:
        return "CALLS";
    case RelationshipKind::References:
        return "REFERENCES";
    case RelationshipKind::InheritsFrom:
        return "INHERITS_FROM";
    case RelationshipKind::Implements:
        return "IMPLEMENTS";
    }
    return "UNKNOWN";
}

/**
 * @brief Parses a string into a RelationshipKind.
 */
[[nodiscard]] constexpr std::optional<RelationshipKind>
relationship_kind_from_string(std::string_view str) noexcept {
    if (str == "CONTAINS" || str == "Contains" || str == "contains") {
        return RelationshipKind::Contains;
    }
    if (str == "IMPORTS" || str == "Imports" || str == "imports") {
        return RelationshipKind::Imports;
    }
    if (str == "INCLUDES" || str == "Includes" || str == "includes") {
        return RelationshipKind::Includes;
    }
    if (str == "CALLS" || str == "Calls" || str == "calls") {
        return RelationshipKind::Calls;
    }
    if (str == "REFERENCES" || str == "References" || str == "references") {
        return RelationshipKind::References;
    }
    if (str == "INHERITS_FROM" || str == "InheritsFrom" || str == "inherits_from") {
        return RelationshipKind::InheritsFrom;
    }
    if (str == "IMPLEMENTS" || str == "Implements" || str == "implements") {
        return RelationshipKind::Implements;
    }
    return std::nullopt;
}

}  // namespace amoeba::graph
