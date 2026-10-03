#include "amoeba/graph/call_extractor.hpp"

#include "amoeba/graph/import_extractor.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace amoeba::graph {

namespace {

struct DeclarationInfo {
    ElementId element_id{0};
    index::FileId file_id{0};
    std::string name;
    std::string parent_context;
    parser::ElementKind kind{parser::ElementKind::Function};
    parser::SourceRange location;
};

std::string clean_symbol_name(std::string_view raw) {
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t' || raw.front() == '\r' ||
                            raw.front() == '\n')) {
        raw.remove_prefix(1);
    }
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '\r' ||
                            raw.back() == '\n' || raw.back() == ';' || raw.back() == ')')) {
        raw.remove_suffix(1);
    }

    std::string str(raw);
    // Strip trailing parenthesis if function call representation like "foo()"
    if (auto paren = str.find('('); paren != std::string::npos) {
        str = str.substr(0, paren);
    }
    // Strip generic/template parameters e.g., foo<T>
    if (auto angle = str.find('<'); angle != std::string::npos) {
        str = str.substr(0, angle);
    }
    while (!str.empty() && (str.back() == ' ' || str.back() == '\t')) {
        str.pop_back();
    }
    return str;
}

struct ParsedCallTarget {
    std::string full_name;
    std::string qualifier;
    std::string base_name;
    bool is_self_or_this{false};
};

ParsedCallTarget parse_call_target(std::string_view raw) {
    ParsedCallTarget result;
    result.full_name = clean_symbol_name(raw);

    if (result.full_name.empty()) {
        return result;
    }

    if (auto last_colon = result.full_name.rfind("::"); last_colon != std::string::npos) {
        result.qualifier = result.full_name.substr(0, last_colon);
        result.base_name = result.full_name.substr(last_colon + 2);
    } else if (auto last_dot = result.full_name.rfind('.'); last_dot != std::string::npos) {
        result.qualifier = result.full_name.substr(0, last_dot);
        result.base_name = result.full_name.substr(last_dot + 1);
    } else if (auto last_arrow = result.full_name.rfind("->"); last_arrow != std::string::npos) {
        result.qualifier = result.full_name.substr(0, last_arrow);
        result.base_name = result.full_name.substr(last_arrow + 2);
    } else {
        result.qualifier.clear();
        result.base_name = result.full_name;
    }

    if (result.qualifier == "this" || result.qualifier == "self") {
        result.is_self_or_this = true;
    }

    return result;
}

struct DeclarationIndex {
    std::vector<DeclarationInfo> all_declarations;
    std::unordered_map<ElementId, DeclarationInfo> by_id;
    std::unordered_map<index::FileId, std::vector<const DeclarationInfo*>> by_file;
    std::unordered_map<std::string, std::vector<const DeclarationInfo*>> by_name;
};

ElementId find_enclosing_caller(const parser::SourceRange& call_loc, index::FileId file_id,
                                const DeclarationIndex& decl_index,
                                ElementId fallback_id) {
    ElementId best_match = fallback_id;
    uint32_t min_line_span = UINT32_MAX;

    auto it = decl_index.by_file.find(file_id);
    if (it == decl_index.by_file.end()) {
        return fallback_id;
    }

    for (const auto* decl : it->second) {
        if (decl->kind != parser::ElementKind::Function &&
            decl->kind != parser::ElementKind::Method &&
            decl->kind != parser::ElementKind::Component && decl->kind != parser::ElementKind::Hook) {
            continue;
        }

        if (decl->location.start.line <= call_loc.start.line &&
            decl->location.end.line >= call_loc.end.line) {
            uint32_t span = decl->location.end.line - decl->location.start.line;
            if (span < min_line_span) {
                min_line_span = span;
                best_match = decl->element_id;
            }
        }
    }

    return best_match;
}

struct MatchResult {
    std::optional<ElementId> target_id{std::nullopt};
    ResolutionStatus status{ResolutionStatus::Unresolved};
    std::string reason;
};

MatchResult resolve_call_layered(const ParsedCallTarget& target, const DeclarationInfo& caller_info,
                                 const DeclarationIndex& decl_index,
                                 const std::unordered_set<index::FileId>& imported_file_ids) {

    if (target.base_name.empty()) {
        return MatchResult{.status = ResolutionStatus::Unresolved, .reason = "empty_target"};
    }

    // 1. Recursive call / self-call
    if (target.base_name == caller_info.name &&
        (target.qualifier.empty() || target.is_self_or_this ||
         target.qualifier == caller_info.parent_context)) {
        return MatchResult{
            .target_id = caller_info.element_id,
            .status = ResolutionStatus::Resolved,
            .reason = "recursive_self_call",
        };
    }

    auto name_it = decl_index.by_name.find(target.base_name);
    if (name_it == decl_index.by_name.end() || name_it->second.empty()) {
        return MatchResult{
            .target_id = std::nullopt,
            .status = ResolutionStatus::Unresolved,
            .reason = "unresolved_external_or_stdlib",
        };
    }

    const auto& name_matches = name_it->second;

    // 2. Enclosing class / method scope
    if (!caller_info.parent_context.empty()) {
        std::vector<ElementId> class_matches;
        for (const auto* decl : name_matches) {
            if (decl->file_id == caller_info.file_id &&
                decl->parent_context == caller_info.parent_context) {
                class_matches.push_back(decl->element_id);
            }
        }
        if (class_matches.size() == 1) {
            return MatchResult{
                .target_id = class_matches.front(),
                .status = ResolutionStatus::Resolved,
                .reason = "enclosing_class_scope",
            };
        }
        if (class_matches.size() > 1) {
            return MatchResult{
                .target_id = std::nullopt,
                .status = ResolutionStatus::Ambiguous,
                .reason =
                    "ambiguous_in_enclosing_class(" + std::to_string(class_matches.size()) + ")",
            };
        }
    }

    // 3. Same-file declarations
    {
        std::vector<const DeclarationInfo*> file_matches;
        for (const auto* decl : name_matches) {
            if (decl->file_id == caller_info.file_id) {
                if (!target.qualifier.empty() && !target.is_self_or_this) {
                    if (decl->parent_context != target.qualifier &&
                        !decl->parent_context.ends_with("::" + target.qualifier) &&
                        !decl->parent_context.ends_with("." + target.qualifier)) {
                        continue;
                    }
                }
                file_matches.push_back(decl);
            }
        }
        if (file_matches.size() == 1) {
            return MatchResult{
                .target_id = file_matches.front()->element_id,
                .status = ResolutionStatus::Resolved,
                .reason = "same_file_scope",
            };
        }
        if (file_matches.size() > 1) {
            // Check if all matches share the exact same parent_context (e.g. forward decl +
            // definition)
            bool same_context = true;
            for (size_t i = 1; i < file_matches.size(); ++i) {
                if (file_matches[i]->parent_context != file_matches[0]->parent_context) {
                    same_context = false;
                    break;
                }
            }
            if (same_context) {
                // Select the definition with the largest span (the implementation body)
                const DeclarationInfo* best = file_matches[0];
                uint32_t max_span = best->location.end.line - best->location.start.line;
                for (size_t i = 1; i < file_matches.size(); ++i) {
                    uint32_t span =
                        file_matches[i]->location.end.line - file_matches[i]->location.start.line;
                    if (span > max_span) {
                        max_span = span;
                        best = file_matches[i];
                    }
                }
                return MatchResult{
                    .target_id = best->element_id,
                    .status = ResolutionStatus::Resolved,
                    .reason = "same_file_scope_definition",
                };
            }

            return MatchResult{
                .target_id = std::nullopt,
                .status = ResolutionStatus::Ambiguous,
                .reason = "ambiguous_same_file(" + std::to_string(file_matches.size()) + ")",
            };
        }
    }

    // 4. Namespace / Qualified Context
    if (!target.qualifier.empty() && !target.is_self_or_this) {
        std::vector<ElementId> qualified_matches;
        for (const auto* decl : name_matches) {
            if (decl->parent_context == target.qualifier ||
                decl->parent_context.ends_with("::" + target.qualifier) ||
                decl->parent_context.ends_with("." + target.qualifier)) {
                qualified_matches.push_back(decl->element_id);
            }
        }
        if (qualified_matches.size() == 1) {
            return MatchResult{
                .target_id = qualified_matches.front(),
                .status = ResolutionStatus::Resolved,
                .reason = "qualified_namespace_match",
            };
        }
        if (qualified_matches.size() > 1) {
            return MatchResult{
                .target_id = std::nullopt,
                .status = ResolutionStatus::Ambiguous,
                .reason = "ambiguous_qualified_namespace(" +
                          std::to_string(qualified_matches.size()) + ")",
            };
        }
    }

    if (!caller_info.parent_context.empty()) {
        std::vector<ElementId> ns_matches;
        for (const auto* decl : name_matches) {
            if (decl->parent_context == caller_info.parent_context) {
                ns_matches.push_back(decl->element_id);
            }
        }
        if (ns_matches.size() == 1) {
            return MatchResult{
                .target_id = ns_matches.front(),
                .status = ResolutionStatus::Resolved,
                .reason = "caller_namespace_context",
            };
        }
        if (ns_matches.size() > 1) {
            return MatchResult{
                .target_id = std::nullopt,
                .status = ResolutionStatus::Ambiguous,
                .reason = "ambiguous_caller_namespace(" + std::to_string(ns_matches.size()) + ")",
            };
        }
    }

    // 5. Imported symbols
    if (!imported_file_ids.empty()) {
        std::vector<ElementId> import_matches;
        for (const auto* decl : name_matches) {
            if (imported_file_ids.contains(decl->file_id)) {
                if (!target.qualifier.empty() && !target.is_self_or_this) {
                    if (decl->parent_context != target.qualifier &&
                        !decl->parent_context.ends_with("::" + target.qualifier) &&
                        !decl->parent_context.ends_with("." + target.qualifier)) {
                        continue;
                    }
                }
                import_matches.push_back(decl->element_id);
            }
        }
        if (import_matches.size() == 1) {
            return MatchResult{
                .target_id = import_matches.front(),
                .status = ResolutionStatus::Resolved,
                .reason = "imported_symbol_match",
            };
        }
        if (import_matches.size() > 1) {
            return MatchResult{
                .target_id = std::nullopt,
                .status = ResolutionStatus::Ambiguous,
                .reason =
                    "ambiguous_imported_symbol(" + std::to_string(import_matches.size()) + ")",
            };
        }
    }

    // 6. Unique repository-wide declaration
    {
        if (name_matches.size() == 1) {
            return MatchResult{
                .target_id = name_matches.front()->element_id,
                .status = ResolutionStatus::Resolved,
                .reason = "unique_repo_declaration",
            };
        }
        if (name_matches.size() > 1) {
            return MatchResult{
                .target_id = std::nullopt,
                .status = ResolutionStatus::Ambiguous,
                .reason = "ambiguous_multiple_repo_declarations(" +
                          std::to_string(name_matches.size()) + ")",
            };
        }
    }

    // 7. Unresolved
    return MatchResult{
        .target_id = std::nullopt,
        .status = ResolutionStatus::Unresolved,
        .reason = "unresolved_external_or_stdlib",
    };
}

}  // namespace

CallExtractionResult CallExtractor::extract_and_populate(const index::InvertedIndex& index,
                                                         RelationshipGraph& graph) {
    CallExtractionResult result;

    if (index.file_count() == 0 || index.element_count() == 0) {
        return result;
    }

    // 1. Collect and index all declarations
    DeclarationIndex decl_index;
    decl_index.all_declarations.reserve(index.element_count());

    for (ElementId id = 0; id < index.element_count(); ++id) {
        const auto& elem = index.get_element(id);
        if (elem.element.kind == parser::ElementKind::Function ||
            elem.element.kind == parser::ElementKind::Method ||
            elem.element.kind == parser::ElementKind::Class ||
            elem.element.kind == parser::ElementKind::Struct ||
            elem.element.kind == parser::ElementKind::Interface ||
            elem.element.kind == parser::ElementKind::Component ||
            elem.element.kind == parser::ElementKind::Hook) {
            DeclarationInfo info{
                .element_id = id,
                .file_id = elem.file_id,
                .name = elem.element.name,
                .parent_context = elem.element.parent_context,
                .kind = elem.element.kind,
                .location = elem.element.location,
            };
            decl_index.all_declarations.push_back(info);
            decl_index.by_id[id] = info;
        }
    }

    for (const auto& decl : decl_index.all_declarations) {
        decl_index.by_file[decl.file_id].push_back(&decl);
        decl_index.by_name[decl.name].push_back(&decl);
    }

    // 2. Pre-extract file imports to populate imported_file_ids mapping
    RelationshipGraph import_graph;
    auto import_result = ImportExtractor::extract_and_populate(index, import_graph);

    std::unordered_map<index::FileId, std::unordered_set<index::FileId>> file_imports;
    for (const auto& res : import_result.resolutions) {
        if (res.is_resolved && res.target_file_id.has_value() &&
            res.source_element_id < index.element_count()) {
            index::FileId src_fid = index.get_element(res.source_element_id).file_id;
            file_imports[src_fid].insert(*res.target_file_id);
        }
    }

    // 3. Process all Call and Reference candidates
    for (ElementId id = 0; id < index.element_count(); ++id) {
        const auto& elem = index.get_element(id);

        const bool is_call = (elem.element.kind == parser::ElementKind::Call);
        const bool is_ref = (elem.element.kind == parser::ElementKind::JSXComponent);

        if (!is_call && !is_ref) {
            continue;
        }

        result.total_call_candidates++;

        // Find enclosing caller declaration
        ElementId caller_id =
            find_enclosing_caller(elem.element.location, elem.file_id, decl_index, id);

        DeclarationInfo caller_info{
            .element_id = caller_id,
            .file_id = elem.file_id,
            .name = elem.element.parent_context,
            .parent_context = "",
            .kind = parser::ElementKind::Function,
            .location = elem.element.location,
        };

        if (auto it = decl_index.by_id.find(caller_id); it != decl_index.by_id.end()) {
            caller_info = it->second;
        }

        ParsedCallTarget target = parse_call_target(elem.element.name);
        RelationshipKind edge_kind =
            is_call ? RelationshipKind::Calls : RelationshipKind::References;

        const auto& imported_set = file_imports[elem.file_id];
        MatchResult match = resolve_call_layered(target, caller_info, decl_index, imported_set);

        CallResolution resolution{
            .source_element_id = caller_id,
            .source_name = caller_info.name.empty() ? "(anonymous)" : caller_info.name,
            .raw_target_name = elem.element.name,
            .kind = edge_kind,
            .target_element_id = match.target_id,
            .status = match.status,
            .resolution_reason = match.reason,
        };

        if (match.status == ResolutionStatus::Resolved && match.target_id.has_value()) {
            graph.add_relationship(caller_id, *match.target_id, edge_kind);
            if (is_call) {
                result.resolved_calls++;
            } else {
                result.resolved_references++;
            }
        } else if (match.status == ResolutionStatus::Ambiguous) {
            result.ambiguous_calls++;
        } else {
            result.unresolved_calls++;
        }

        result.resolutions.push_back(std::move(resolution));
    }
    return result;
}

CallExtractionResult
CallExtractor::extract_and_populate(const std::vector<parser::ParsedFile>& files,
                                    RelationshipGraph& graph) {
    index::InvertedIndex index;
    for (const auto& file : files) {
        index.add_parsed_file(file);
    }
    return extract_and_populate(index, graph);
}

}  // namespace amoeba::graph
