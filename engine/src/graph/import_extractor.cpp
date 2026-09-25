#include "amoeba/graph/import_extractor.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace amoeba::graph {

std::string ImportExtractor::normalize_import_path(std::string_view raw) {
    // 1. Trim outer whitespace
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t' || raw.front() == '\r' ||
                            raw.front() == '\n')) {
        raw.remove_prefix(1);
    }
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '\r' ||
                            raw.back() == '\n' || raw.back() == ';')) {
        raw.remove_suffix(1);
    }

    std::string text(raw);

    // 2. Extract target module / specifier from structured statement syntax

    // Python: from module import symbol
    if (text.starts_with("from ")) {
        auto import_pos = text.find(" import ");
        if (import_pos != std::string::npos) {
            text = text.substr(5, import_pos - 5);
        } else {
            text = text.substr(5);
        }
    }
    // JS/TS: import ... from 'target' or import ... from "target"
    else if (auto from_pos = text.find(" from "); from_pos != std::string::npos) {
        text = text.substr(from_pos + 6);
    } else if (text.starts_with("import ")) {
        text = text.substr(7);
    } else if (text.starts_with("use ")) {
        text = text.substr(4);
    } else if (text.starts_with("mod ")) {
        text = text.substr(4);
    } else if (text.starts_with("#include")) {
        text = text.substr(8);
    } else if (auto href_pos = text.find("href="); href_pos != std::string::npos) {
        text = text.substr(href_pos + 5);
        if (!text.empty() && (text.front() == '"' || text.front() == '\'')) {
            char quote = text.front();
            text = text.substr(1);
            if (auto q_end = text.find(quote); q_end != std::string::npos) {
                text = text.substr(0, q_end);
            }
        }
    } else if (auto req_pos = text.find("require("); req_pos != std::string::npos) {
        text = text.substr(req_pos + 8);
        if (auto close_p = text.rfind(')'); close_p != std::string::npos) {
            text = text.substr(0, close_p);
        }
    }

    // Trim again after prefix stripping
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '(' ||
                             text.front() == '\r' || text.front() == '\n')) {
        text.erase(text.begin());
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == ';' ||
                             text.back() == ')' || text.back() == '\r' || text.back() == '\n')) {
        text.pop_back();
    }

    // Strip enclosing quotes / angle brackets
    if (text.size() >= 2) {
        if ((text.front() == '"' && text.back() == '"') ||
            (text.front() == '\'' && text.back() == '\'') ||
            (text.front() == '<' && text.back() == '>')) {
            text = text.substr(1, text.size() - 2);
        }
    }

    // Normalize slashes
    for (char& c : text) {
        if (c == '\\') {
            c = '/';
        }
    }

    // Handle python comma separated imports (take first)
    if (auto comma_pos = text.find(','); comma_pos != std::string::npos) {
        text = text.substr(0, comma_pos);
        while (!text.empty() && text.back() == ' ') {
            text.pop_back();
        }
    }

    return text;
}

RelationshipKind ImportExtractor::determine_kind(std::string_view language,
                                                 std::string_view raw_target) {
    if (language == "C++" || language == "C" || language == "cpp" || language == "c" ||
        language == "HTML" || language == "CSS" || language == "html" || language == "css") {
        return RelationshipKind::Includes;
    }
    if (raw_target.starts_with("#include") || raw_target.starts_with("<link") ||
        raw_target.starts_with("@import")) {
        return RelationshipKind::Includes;
    }
    return RelationshipKind::Imports;
}

namespace {

struct FileLookupTarget {
    index::FileId file_id{0};
    ElementId primary_element_id{0};
    std::filesystem::path path;
    std::string generic_path;
    std::string filename;
    std::string stem;
    std::string parent_dir;
};

std::optional<FileLookupTarget>
find_matching_file(const std::filesystem::path& source_file_path, const std::string& clean_import,
                   const std::vector<FileLookupTarget>& lookup_table) {
    if (clean_import.empty() || lookup_table.empty()) {
        return std::nullopt;
    }

    std::string target = clean_import;

    // 1. Relative path resolution (./ or ../)
    if (target.starts_with("./") || target.starts_with("../")) {
        auto source_dir = source_file_path.parent_path();
        auto resolved_candidate = (source_dir / target).lexically_normal().generic_string();

        static const std::vector<std::string> kExtensions = {
            "",      ".ts",       ".tsx",       ".js",       ".jsx",       ".py",
            ".hpp",  ".h",        ".cpp",       ".c",        ".go",        ".rs",
            ".java", "/index.ts", "/index.tsx", "/index.js", "/index.jsx", "/mod.rs"};

        for (const auto& ext : kExtensions) {
            std::string candidate_with_ext = resolved_candidate + ext;
            for (const auto& entry : lookup_table) {
                if (entry.generic_path == candidate_with_ext ||
                    entry.generic_path.ends_with("/" + candidate_with_ext) ||
                    entry.path.lexically_normal().generic_string() == candidate_with_ext) {
                    return entry;
                }
            }
        }
    }

    // 2. Exact or suffix path match (e.g., "amoeba/scanner/repository_scanner.hpp")
    for (const auto& entry : lookup_table) {
        if (entry.generic_path == target || entry.generic_path.ends_with("/" + target)) {
            return entry;
        }
    }

    // 3. Package / folder prefix match (e.g., Go "pkg/logger" matching "pkg/logger/logger.go")
    for (const auto& entry : lookup_table) {
        if (entry.parent_dir == target || entry.parent_dir.ends_with("/" + target) ||
            entry.generic_path.starts_with(target + "/")) {
            return entry;
        }
    }

    // 4. Filename exact match (e.g., "repository_scanner.hpp" or "service.py")
    for (const auto& entry : lookup_table) {
        if (entry.filename == target) {
            return entry;
        }
    }

    // 5. Stem/module name match for languages without file extensions in imports (Python, Java, Go,
    // Rust) E.g. "import service" -> service.py E.g. "import com.example.UserService" ->
    // UserService.java
    std::string module_stem = target;
    if (auto last_dot = module_stem.rfind('.'); last_dot != std::string::npos) {
        module_stem = module_stem.substr(last_dot + 1);
    }
    if (auto last_slash = module_stem.rfind('/'); last_slash != std::string::npos) {
        module_stem = module_stem.substr(last_slash + 1);
    }
    if (auto last_colon = module_stem.rfind("::"); last_colon != std::string::npos) {
        module_stem = module_stem.substr(last_colon + 2);
    }

    if (!module_stem.empty()) {
        for (const auto& entry : lookup_table) {
            if (entry.stem == module_stem) {
                return entry;
            }
        }
    }

    return std::nullopt;
}

}  // namespace

ImportExtractionResult ImportExtractor::extract_and_populate(const index::InvertedIndex& index,
                                                             RelationshipGraph& graph) {
    ImportExtractionResult result;

    if (index.file_count() == 0 || index.element_count() == 0) {
        return result;
    }

    // Build file lookup table
    std::vector<FileLookupTarget> lookup_table;
    lookup_table.reserve(index.file_count());

    // Map file_id to its first/primary ElementId
    std::unordered_map<index::FileId, ElementId> file_primary_elem;

    for (ElementId id = 0; id < index.element_count(); ++id) {
        const auto& elem = index.get_element(id);
        if (!file_primary_elem.contains(elem.file_id)) {
            file_primary_elem[elem.file_id] = id;
        }
    }

    for (index::FileId fid = 0; fid < index.file_count(); ++fid) {
        const auto& file = index.get_file(fid);
        ElementId primary_elem = file_primary_elem.contains(fid) ? file_primary_elem[fid] : 0;

        lookup_table.push_back(FileLookupTarget{
            .file_id = fid,
            .primary_element_id = primary_elem,
            .path = file.file_path,
            .generic_path = file.file_path.generic_string(),
            .filename = file.file_path.filename().generic_string(),
            .stem = file.file_path.stem().generic_string(),
            .parent_dir = file.file_path.parent_path().generic_string(),
        });
    }

    // Process all Include/Import elements
    for (ElementId id = 0; id < index.element_count(); ++id) {
        const auto& elem = index.get_element(id);
        if (elem.element.kind != parser::ElementKind::Include) {
            continue;
        }

        result.total_imports_found++;

        const auto& source_file = index.get_file(elem.file_id);
        std::string clean_target = normalize_import_path(elem.element.name);
        RelationshipKind kind = determine_kind(source_file.language, elem.element.name);

        ImportResolution res{
            .source_element_id = id,
            .source_file_path = source_file.file_path,
            .raw_import_target = elem.element.name,
            .kind = kind,
            .target_element_id = std::nullopt,
            .target_file_id = std::nullopt,
            .resolved_file_path = {},
            .is_resolved = false,
        };

        if (auto match = find_matching_file(source_file.file_path, clean_target, lookup_table)) {
            res.target_element_id = match->primary_element_id;
            res.target_file_id = match->file_id;
            res.resolved_file_path = match->path;
            res.is_resolved = true;

            graph.add_relationship(id, match->primary_element_id, kind);
            result.resolved_imports++;
        } else {
            result.unresolved_imports++;
        }

        result.resolutions.push_back(std::move(res));
    }

    return result;
}

ImportExtractionResult
ImportExtractor::extract_and_populate(const std::vector<parser::ParsedFile>& files,
                                      RelationshipGraph& graph) {
    index::InvertedIndex index;
    for (const auto& file : files) {
        index.add_parsed_file(file);
    }
    return extract_and_populate(index, graph);
}

}  // namespace amoeba::graph
