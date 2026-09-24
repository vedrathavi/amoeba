#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace amoeba::parser {

using namespace std;

/**
 * @brief Categorization of code structures recognized during parsing.
 */
enum class ElementKind {
    Class,
    Struct,
    Interface,
    Function,
    Method,
    Include,
    Call,
    JSXElement,
    JSXComponent,
    Selector,
    Property,
    Attribute,
    UtilityClass,
    Component,
    Hook,
    Route,
    Unknown,
};

[[nodiscard]] constexpr string_view to_string(ElementKind kind) noexcept {
    switch (kind) {
    case ElementKind::Class:
        return "Class";
    case ElementKind::Struct:
        return "Struct";
    case ElementKind::Interface:
        return "Interface";
    case ElementKind::Function:
        return "Function";
    case ElementKind::Method:
        return "Method";
    case ElementKind::Include:
        return "Include";
    case ElementKind::Call:
        return "Call";
    case ElementKind::JSXElement:
        return "JSXElement";
    case ElementKind::JSXComponent:
        return "JSXComponent";
    case ElementKind::Selector:
        return "Selector";
    case ElementKind::Property:
        return "Property";
    case ElementKind::Attribute:
        return "Attribute";
    case ElementKind::UtilityClass:
        return "UtilityClass";
    case ElementKind::Component:
        return "Component";
    case ElementKind::Hook:
        return "Hook";
    case ElementKind::Route:
        return "Route";
    default:
        return "Unknown";
    }
}

/**
 * @brief 1-indexed source code point location.
 */
struct SourceLocation {
    uint32_t line{1};
    uint32_t column{1};
    uint32_t byte_offset{0};

    [[nodiscard]] bool operator==(const SourceLocation&) const = default;
};

/**
 * @brief Source range from start position to end position.
 */
struct SourceRange {
    SourceLocation start;
    SourceLocation end;

    [[nodiscard]] bool operator==(const SourceRange&) const = default;
};

/**
 * @brief Representation of a recognized syntactic code element.
 */
struct CodeElement {
    ElementKind kind{ElementKind::Unknown};
    string name;
    SourceRange location;
    string parent_context;
    string detail;

    [[nodiscard]] bool operator==(const CodeElement&) const = default;
};

}  // namespace amoeba::parser
