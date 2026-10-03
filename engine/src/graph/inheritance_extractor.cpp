#include "amoeba/graph/inheritance_extractor.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace amoeba::graph {

namespace {

struct ClassLookupTarget {
    ElementId element_id{0};
    index::FileId file_id{0};
    std::string name;
    std::string parent_context;
    parser::ElementKind kind{parser::ElementKind::Class};
};

std::string clean_type_name(std::string_view raw) {
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t' || raw.front() == '\r' ||
                            raw.front() == '\n')) {
        raw.remove_prefix(1);
    }
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '\r' ||
                            raw.back() == '\n' || raw.back() == ';')) {
        raw.remove_suffix(1);
    }

    std::string str(raw);
    // Strip generic/template parameters e.g., Base<T> or List<String>
    if (auto angle = str.find('<'); angle != std::string::npos) {
        str = str.substr(0, angle);
    }

    while (!str.empty() && str.back() == ' ') {
        str.pop_back();
    }
    return str;
}

struct ClassLookupIndex {
    std::vector<ClassLookupTarget> lookup_table;
    std::unordered_map<std::string, std::vector<const ClassLookupTarget*>> by_name;
};

std::optional<ElementId> resolve_target_class(const std::string& target_name,
                                              index::FileId source_file_id,
                                              const std::string& source_parent_context,
                                              const ClassLookupIndex& class_index) {
    if (target_name.empty() || class_index.lookup_table.empty()) {
        return std::nullopt;
    }

    std::string clean_target = clean_type_name(target_name);
    std::string stem_target = clean_target;
    if (auto last_colon = stem_target.rfind("::"); last_colon != std::string::npos) {
        stem_target = stem_target.substr(last_colon + 2);
    }
    if (auto last_dot = stem_target.rfind('.'); last_dot != std::string::npos) {
        stem_target = stem_target.substr(last_dot + 1);
    }

    auto get_candidates = [&](const std::string& name) -> const std::vector<const ClassLookupTarget*>* {
        auto it = class_index.by_name.find(name);
        if (it != class_index.by_name.end()) return &it->second;
        return nullptr;
    };

    const auto* clean_cands = get_candidates(clean_target);
    const auto* stem_cands = (clean_target != stem_target) ? get_candidates(stem_target) : nullptr;

    // 1. Same-file resolution
    if (clean_cands) {
        for (const auto* entry : *clean_cands) {
            if (entry->file_id == source_file_id) return entry->element_id;
        }
    }
    if (stem_cands) {
        for (const auto* entry : *stem_cands) {
            if (entry->file_id == source_file_id) return entry->element_id;
        }
    }

    // 2. Same parent context / namespace resolution
    if (!source_parent_context.empty()) {
        if (clean_cands) {
            for (const auto* entry : *clean_cands) {
                if (entry->parent_context == source_parent_context) return entry->element_id;
            }
        }
        if (stem_cands) {
            for (const auto* entry : *stem_cands) {
                if (entry->parent_context == source_parent_context) return entry->element_id;
            }
        }
    }

    // 3. Exact qualified or simple name match across the repository
    if (clean_cands && !clean_cands->empty()) {
        return clean_cands->front()->element_id;
    }

    // 4. Stem match across repository
    if (stem_cands && !stem_cands->empty()) {
        return stem_cands->front()->element_id;
    }

    return std::nullopt;
}

}  // namespace

InheritanceExtractionResult
InheritanceExtractor::extract_and_populate(const index::InvertedIndex& index,
                                           RelationshipGraph& graph) {
    InheritanceExtractionResult result;

    if (index.file_count() == 0 || index.element_count() == 0) {
        return result;
    }

    // Build class and interface lookup table
    ClassLookupIndex class_index;
    class_index.lookup_table.reserve(index.element_count());

    for (ElementId id = 0; id < index.element_count(); ++id) {
        const auto& elem = index.get_element(id);
        if (elem.element.kind == parser::ElementKind::Class ||
            elem.element.kind == parser::ElementKind::Struct ||
            elem.element.kind == parser::ElementKind::Interface) {
            class_index.lookup_table.push_back(ClassLookupTarget{
                .element_id = id,
                .file_id = elem.file_id,
                .name = elem.element.name,
                .parent_context = elem.element.parent_context,
                .kind = elem.element.kind,
            });
        }
    }

    for (const auto& entry : class_index.lookup_table) {
        class_index.by_name[entry.name].push_back(&entry);
    }

    // Process all classes, structs, and interfaces
    for (ElementId id = 0; id < index.element_count(); ++id) {
        const auto& elem = index.get_element(id);
        if (elem.element.kind != parser::ElementKind::Class &&
            elem.element.kind != parser::ElementKind::Struct &&
            elem.element.kind != parser::ElementKind::Interface) {
            continue;
        }

        if (elem.element.detail.empty()) {
            continue;
        }

        // Parse clauses separated by ';'
        std::stringstream ss(elem.element.detail);
        std::string segment;
        while (std::getline(ss, segment, ';')) {
            while (!segment.empty() && segment.front() == ' ')
                segment.erase(segment.begin());
            while (!segment.empty() && segment.back() == ' ')
                segment.pop_back();

            if (segment.empty())
                continue;

            RelationshipKind kind = RelationshipKind::InheritsFrom;
            std::string targets_str;

            if (segment.starts_with("extends:")) {
                kind = RelationshipKind::InheritsFrom;
                targets_str = segment.substr(8);
            } else if (segment.starts_with("implements:")) {
                kind = RelationshipKind::Implements;
                targets_str = segment.substr(11);
            } else {
                continue;
            }

            // Parse comma-separated list of target types
            std::stringstream target_ss(targets_str);
            std::string target_name;
            while (std::getline(target_ss, target_name, ',')) {
                target_name = clean_type_name(target_name);
                if (target_name.empty())
                    continue;

                result.total_clauses_found++;

                InheritanceResolution res{
                    .source_element_id = id,
                    .source_class_name = elem.element.name,
                    .raw_target_name = target_name,
                    .kind = kind,
                    .target_element_id = std::nullopt,
                    .is_resolved = false,
                };

                if (auto match = resolve_target_class(target_name, elem.file_id,
                                                      elem.element.parent_context, class_index)) {
                    res.target_element_id = *match;
                    res.is_resolved = true;
                    graph.add_relationship(id, *match, kind);
                    result.resolved_inheritances++;
                } else {
                    result.unresolved_inheritances++;
                }

                result.resolutions.push_back(std::move(res));
            }
        }
    }

    return result;
}

InheritanceExtractionResult
InheritanceExtractor::extract_and_populate(const std::vector<parser::ParsedFile>& files,
                                           RelationshipGraph& graph) {
    index::InvertedIndex index;
    for (const auto& file : files) {
        index.add_parsed_file(file);
    }
    return extract_and_populate(index, graph);
}

}  // namespace amoeba::graph
