#include "amoeba/tools/find_symbol_tool.hpp"

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

[[nodiscard]] std::optional<parser::ElementKind> parse_kind_filter(std::string_view kind_str) {
    const std::string lower = to_lower_string(kind_str);
    if (lower == "function")
        return parser::ElementKind::Function;
    if (lower == "method")
        return parser::ElementKind::Method;
    if (lower == "class")
        return parser::ElementKind::Class;
    if (lower == "struct")
        return parser::ElementKind::Struct;
    if (lower == "interface")
        return parser::ElementKind::Interface;
    if (lower == "hook")
        return parser::ElementKind::Hook;
    if (lower == "component" || lower == "jsxcomponent")
        return parser::ElementKind::JSXComponent;
    if (lower == "route")
        return parser::ElementKind::Route;
    return std::nullopt;
}

struct SymbolMatch {
    std::string name;
    parser::ElementKind kind;
    std::string file_path;
    parser::SourceRange location;
    std::string parent_context;
    std::string detail;
    bool is_exact{false};

    bool operator==(const SymbolMatch&) const = default;
};

}  // namespace

ToolDescription FindSymbolTool::description() const {
    return ToolDescription{
        .name = "find_symbol",
        .description = "Locates exact or partial symbol declarations (functions, classes, methods, "
                       "hooks, components) across the repository.",
        .parameters = {
            ToolParameter{
                .name = "name",
                .type = "string",
                .description = "Symbol name or identifier to find (e.g. 'InvertedIndex', "
                               "'useCalendar', 'parse_file').",
                .required = true,
                .default_value = "",
            },
            ToolParameter{
                .name = "kind",
                .type = "string",
                .description = "Optional element kind filter ('class', 'function', 'method', "
                               "'struct', 'hook', 'component', 'route').",
                .required = false,
                .default_value = "",
            },
            ToolParameter{
                .name = "limit",
                .type = "integer",
                .description = "Maximum number of symbol matches to return (1-30).",
                .required = false,
                .default_value = "10",
            },
        },
    };
}

ToolResult FindSymbolTool::execute(const ToolRequest& request,
                                   const ToolExecutionContext& context) const {
    ToolResult res;
    res.tool_name = name();

    const auto name_opt = request.get_arg("name");
    if (!name_opt.has_value() || name_opt->empty()) {
        res.status = ToolResultStatus::InvalidRequest;
        res.error_code = "MISSING_ARGUMENT";
        res.summary = "Missing or empty required argument 'name'.";
        return res;
    }

    const std::string_view target_name = *name_opt;
    const std::string lower_target = to_lower_string(target_name);

    const auto kind_str = request.get_arg_or("kind", "");
    const auto kind_filter = kind_str.empty() ? std::nullopt : parse_kind_filter(kind_str);

    const std::string limit_str = request.get_arg_or("limit", "10");
    std::size_t limit = 10;
    try {
        limit = std::stoul(limit_str);
    } catch (...) {
        limit = 10;
    }
    limit = std::clamp(limit, std::size_t{1}, std::size_t{30});

    std::vector<SymbolMatch> matches;

    // Search across parsed_files if available
    if (context.parsed_files != nullptr) {
        for (const auto& file : *context.parsed_files) {
            for (const auto& elem : file.elements) {
                if (elem.name.empty()) {
                    continue;
                }
                if (kind_filter.has_value() && elem.kind != *kind_filter) {
                    continue;
                }

                const std::string lower_elem_name = to_lower_string(elem.name);
                const bool exact = (elem.name == target_name || lower_elem_name == lower_target);
                const bool partial = (lower_elem_name.find(lower_target) != std::string::npos);

                if (exact || partial) {
                    std::filesystem::path f_path = file.file_path;
                    if (!context.repository_root.empty()) {
                        std::error_code ec;
                        auto rel = std::filesystem::relative(f_path, context.repository_root, ec);
                        if (!ec && !rel.empty() && !rel.string().starts_with("..")) {
                            f_path = rel;
                        }
                    }
                    matches.push_back(SymbolMatch{
                        .name = elem.name,
                        .kind = elem.kind,
                        .file_path = f_path.generic_string(),
                        .location = elem.location,
                        .parent_context = elem.parent_context,
                        .detail = elem.detail,
                        .is_exact = exact,
                    });
                }
            }
        }
    } else if (context.index != nullptr) {
        // Fall back to inverted index element lookup
        for (index::ElementId id = 0; id < context.index->element_count(); ++id) {
            const auto& indexed_elem = context.index->get_element(id);
            const auto& elem = indexed_elem.element;
            if (elem.name.empty()) {
                continue;
            }
            if (kind_filter.has_value() && elem.kind != *kind_filter) {
                continue;
            }

            const std::string lower_elem_name = to_lower_string(elem.name);
            const bool exact = (elem.name == target_name || lower_elem_name == lower_target);
            const bool partial = (lower_elem_name.find(lower_target) != std::string::npos);

            if (exact || partial) {
                std::filesystem::path fpath;
                if (indexed_elem.file_id < context.index->file_count()) {
                    const auto& f = context.index->get_file(indexed_elem.file_id);
                    fpath = f.file_path;
                    if (!context.repository_root.empty()) {
                        std::error_code ec;
                        auto rel = std::filesystem::relative(fpath, context.repository_root, ec);
                        if (!ec && !rel.empty() && !rel.string().starts_with("..")) {
                            fpath = rel;
                        }
                    }
                }

                matches.push_back(SymbolMatch{
                    .name = elem.name,
                    .kind = elem.kind,
                    .file_path = fpath.generic_string(),
                    .location = elem.location,
                    .parent_context = elem.parent_context,
                    .detail = elem.detail,
                    .is_exact = exact,
                });
            }
        }
    } else {
        res.status = ToolResultStatus::Error;
        res.error_code = "CAPABILITY_UNAVAILABLE";
        res.summary = "Neither parsed_files nor InvertedIndex is available in execution context.";
        return res;
    }

    if (matches.empty()) {
        res.status = ToolResultStatus::NotFound;
        res.error_code = "SYMBOL_NOT_FOUND";
        res.summary = "Symbol not found: \"" + std::string(target_name) + "\".";
        return res;
    }

    // Deterministic sorting: Exact matches first, then by name, file path, start line
    std::sort(matches.begin(), matches.end(), [](const SymbolMatch& a, const SymbolMatch& b) {
        if (a.is_exact != b.is_exact) {
            return a.is_exact > b.is_exact;
        }
        if (a.name != b.name) {
            return a.name < b.name;
        }
        if (a.file_path != b.file_path) {
            return a.file_path < b.file_path;
        }
        return a.location.start.line < b.location.start.line;
    });

    if (matches.size() > limit) {
        matches.resize(limit);
    }

    res.status = ToolResultStatus::Success;
    res.summary = "Found " + std::to_string(matches.size()) + " symbol declaration(s).";
    res.metadata.push_back({"symbol", std::string(target_name)});
    res.metadata.push_back({"match_count", std::to_string(matches.size())});

    std::ostringstream ss;
    for (std::size_t i = 0; i < matches.size(); ++i) {
        const auto& m = matches[i];
        ss << "[" << (i + 1) << "] " << parser::to_string(m.kind) << " `" << m.name << "`\n";
        ss << "    File: " << m.file_path;
        if (m.location.start.line > 0) {
            ss << ":" << m.location.start.line << ":" << m.location.start.column << " - "
               << m.location.end.line << ":" << m.location.end.column;
        }
        ss << "\n";
        if (!m.parent_context.empty()) {
            ss << "    Parent: `" << m.parent_context << "`\n";
        }
        if (!m.detail.empty()) {
            ss << "    Detail: `" << m.detail << "`\n";
        }
        ss << "\n";
    }

    res.content = ss.str();
    return res;
}

}  // namespace amoeba::tools
