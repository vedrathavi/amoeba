#include "amoeba/tools/get_relationships_tool.hpp"

#include "amoeba/graph/relationship_evidence.hpp"
#include "amoeba/graph/relationship_evidence_resolver.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/code_element.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace amoeba::tools {

namespace {

[[nodiscard]] std::string to_lower_string(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

[[nodiscard]] std::optional<graph::RelationshipKind> parse_rel_kind(std::string_view s) {
    const std::string lower = to_lower_string(s);
    if (lower == "calls" || lower == "call")
        return graph::RelationshipKind::Calls;
    if (lower == "inherits" || lower == "inherits_from" || lower == "inheritance")
        return graph::RelationshipKind::InheritsFrom;
    if (lower == "implements" || lower == "implement")
        return graph::RelationshipKind::Implements;
    if (lower == "imports" || lower == "import")
        return graph::RelationshipKind::Imports;
    if (lower == "includes" || lower == "include")
        return graph::RelationshipKind::Includes;
    if (lower == "references" || lower == "reference")
        return graph::RelationshipKind::References;
    if (lower == "contains" || lower == "contain")
        return graph::RelationshipKind::Contains;
    return std::nullopt;
}

[[nodiscard]] std::string format_rel(const graph::RelationshipEvidence& rel) {
    std::ostringstream ss;
    using graph::RelationshipDirection;
    using graph::RelationshipKind;

    if (rel.direction == RelationshipDirection::Outgoing) {
        switch (rel.kind) {
        case RelationshipKind::Calls:
            ss << "Calls → ";
            break;
        case RelationshipKind::InheritsFrom:
            ss << "Inherits From → ";
            break;
        case RelationshipKind::Implements:
            ss << "Implements → ";
            break;
        case RelationshipKind::Imports:
            ss << "Imports → ";
            break;
        case RelationshipKind::Includes:
            ss << "Includes → ";
            break;
        case RelationshipKind::References:
            ss << "References → ";
            break;
        case RelationshipKind::Contains:
            ss << "Contains → ";
            break;
        }
    } else {
        switch (rel.kind) {
        case RelationshipKind::Calls:
            ss << "Called By ← ";
            break;
        case RelationshipKind::InheritsFrom:
            ss << "Inherited By ← ";
            break;
        case RelationshipKind::Implements:
            ss << "Implemented By ← ";
            break;
        case RelationshipKind::Imports:
            ss << "Imported By ← ";
            break;
        case RelationshipKind::Includes:
            ss << "Included By ← ";
            break;
        case RelationshipKind::References:
            ss << "Referenced By ← ";
            break;
        case RelationshipKind::Contains:
            ss << "Contained In ← ";
            break;
        }
    }

    if (!rel.related_name.empty()) {
        ss << "`" << rel.related_name << "`";
    } else {
        ss << "ID " << rel.related_element_id;
    }

    if (!rel.related_file_path.empty()) {
        ss << " (" << rel.related_file_path.generic_string();
        if (rel.related_location.start.line > 0) {
            ss << ":L" << rel.related_location.start.line;
        }
        ss << ")";
    }

    return ss.str();
}

}  // namespace

ToolDescription GetRelationshipsTool::description() const {
    return ToolDescription{
        .name = "get_relationships",
        .description = "Discovers direct 1-hop code relationships (calls, callers, inheritance, "
                       "imports, references) for a symbol.",
        .parameters = {
            ToolParameter{
                .name = "symbol",
                .type = "string",
                .description = "Symbol name or identifier to query relationships for.",
                .required = true,
                .default_value = "",
            },
            ToolParameter{
                .name = "kind",
                .type = "string",
                .description = "Optional relationship kind filter ('calls', 'inherits_from', "
                               "'implements', 'imports', 'includes', 'references', 'contains').",
                .required = false,
                .default_value = "",
            },
            ToolParameter{
                .name = "direction",
                .type = "string",
                .description = "Direction filter ('both', 'outgoing', 'incoming').",
                .required = false,
                .default_value = "both",
            },
            ToolParameter{
                .name = "limit",
                .type = "integer",
                .description = "Maximum number of relationships to return (1-25).",
                .required = false,
                .default_value = "10",
            },
        },
    };
}

ToolResult GetRelationshipsTool::execute(const ToolRequest& request,
                                         const ToolExecutionContext& context) const {
    ToolResult res;
    res.tool_name = name();

    const auto symbol_opt = request.get_arg("symbol");
    if (!symbol_opt.has_value() || symbol_opt->empty()) {
        res.status = ToolResultStatus::InvalidRequest;
        res.error_code = "MISSING_ARGUMENT";
        res.summary = "Missing or empty required argument 'symbol'.";
        return res;
    }

    const std::string_view symbol_name = *symbol_opt;
    const std::string lower_target = to_lower_string(symbol_name);

    if (context.index == nullptr || context.relationship_graph == nullptr) {
        res.status = ToolResultStatus::Error;
        res.error_code = "CAPABILITY_UNAVAILABLE";
        res.summary = "InvertedIndex or RelationshipGraph is not available in execution context.";
        return res;
    }

    // 1. Find element ID for the focal symbol
    index::ElementId focal_id = 0;
    std::string matched_exact_name;
    bool found_focal = false;
    for (index::ElementId id = 0; id < context.index->element_count(); ++id) {
        const auto& indexed_elem = context.index->get_element(id);
        const auto& elem = indexed_elem.element;
        if (!elem.name.empty()) {
            if (elem.name == symbol_name) {
                focal_id = id;
                matched_exact_name = elem.name;
                found_focal = true;
                break;
            }
            if (!found_focal && to_lower_string(elem.name) == lower_target) {
                focal_id = id;
                matched_exact_name = elem.name;
                found_focal = true;
            }
        }
    }

    if (!found_focal) {
        res.status = ToolResultStatus::NotFound;
        res.error_code = "SYMBOL_NOT_FOUND";
        res.summary = "Symbol not found in repository index: \"" + std::string(symbol_name) + "\".";
        return res;
    }

    // 2. Parse options
    const auto kind_str = request.get_arg_or("kind", "");
    const auto kind_filter = kind_str.empty() ? std::nullopt : parse_rel_kind(kind_str);

    const std::string dir_str = request.get_arg_or("direction", "both");
    const bool include_incoming = (dir_str == "both" || dir_str == "incoming");
    const bool include_outgoing = (dir_str == "both" || dir_str == "outgoing");

    const std::string limit_str = request.get_arg_or("limit", "10");
    std::size_t limit = 10;
    try {
        limit = std::stoul(limit_str);
    } catch (...) {
        limit = 10;
    }
    limit = std::clamp(limit, std::size_t{1}, std::size_t{25});

    // 3. Resolve relationships
    graph::RelationshipEvidenceResolver default_resolver(*context.relationship_graph, *context.index);
    const auto& resolver = (context.relationship_resolver != nullptr) ? *context.relationship_resolver
                                                                      : default_resolver;

    graph::RelationshipEvidenceOptions opts;
    opts.max_incoming_per_kind = limit;
    opts.max_outgoing_per_kind = limit;
    opts.include_incoming = include_incoming;
    opts.include_outgoing = include_outgoing;

    auto all_rels = resolver.resolve(focal_id, opts);

    // Apply kind filter if requested
    if (kind_filter.has_value()) {
        std::vector<graph::RelationshipEvidence> filtered;
        for (auto& r : all_rels) {
            if (r.kind == *kind_filter) {
                filtered.push_back(std::move(r));
            }
        }
        all_rels = std::move(filtered);
    }

    if (all_rels.empty()) {
        res.status = ToolResultStatus::NotFound;
        res.error_code = "NO_RELATIONSHIPS_FOUND";
        res.summary = "No 1-hop relationships found for `" + matched_exact_name + "`.";
        return res;
    }

    if (all_rels.size() > limit) {
        all_rels.resize(limit);
    }

    res.status = ToolResultStatus::Success;
    res.summary = "Discovered " + std::to_string(all_rels.size()) + " relationship(s) for `" +
                  matched_exact_name + "`.";
    res.metadata.push_back({"symbol", matched_exact_name});
    res.metadata.push_back({"relationship_count", std::to_string(all_rels.size())});

    std::ostringstream ss;
    ss << "Direct Relationships for `" << matched_exact_name << "` (" << all_rels.size()
       << "):\n";
    for (const auto& rel : all_rels) {
        ss << "- " << format_rel(rel) << "\n";
    }

    res.content = ss.str();
    return res;
}

}  // namespace amoeba::tools
