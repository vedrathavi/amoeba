#include "amoeba/tools/tool_evidence_adapter.hpp"
#include "amoeba/graph/relationship_kind.hpp"

#include <algorithm>
#include <sstream>

namespace amoeba::tools {

namespace {

parser::ElementKind parse_element_kind_str(std::string_view str) {
    if (str == "Class" || str == "class") return parser::ElementKind::Class;
    if (str == "Struct" || str == "struct") return parser::ElementKind::Struct;
    if (str == "Interface" || str == "interface") return parser::ElementKind::Interface;
    if (str == "Function" || str == "function") return parser::ElementKind::Function;
    if (str == "Method" || str == "method") return parser::ElementKind::Method;
    if (str == "Component" || str == "component") return parser::ElementKind::Component;
    if (str == "Hook" || str == "hook") return parser::ElementKind::Hook;
    if (str == "Route" || str == "route") return parser::ElementKind::Route;
    if (str == "Include" || str == "include") return parser::ElementKind::Include;
    if (str == "Call" || str == "call") return parser::ElementKind::Call;
    return parser::ElementKind::Unknown;
}

std::optional<std::string> get_metadata_value(const ToolResult& res, std::string_view key) {
    for (const auto& [k, v] : res.metadata) {
        if (k == key) {
            return v;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<evidence::EvidenceItem>
ToolEvidenceAdapter::to_evidence_item(const ToolResult& result) {
    auto items = to_evidence_items(result);
    if (items.empty()) {
        return std::nullopt;
    }
    return std::move(items.front());
}

std::vector<evidence::EvidenceItem>
ToolEvidenceAdapter::to_evidence_items(const ToolResult& result) {
    std::vector<evidence::EvidenceItem> items;
    if (result.status != ToolResultStatus::Success || result.content.empty()) {
        return items;
    }

    if (result.tool_name == "read_file") {
        evidence::EvidenceItem item;

        std::filesystem::path rel_path;
        uint32_t start_line = 1;
        uint32_t end_line = 1;

        auto meta_path = get_metadata_value(result, "file_path");
        auto meta_start = get_metadata_value(result, "start_line");
        auto meta_end = get_metadata_value(result, "end_line");

        if (meta_path.has_value() && meta_start.has_value() && meta_end.has_value()) {
            rel_path = *meta_path;
            try {
                start_line = static_cast<uint32_t>(std::stoul(*meta_start));
                end_line = static_cast<uint32_t>(std::stoul(*meta_end));
            } catch (...) {}
        } else if (result.summary.rfind("File: ", 0) == 0) {
            std::size_t bracket_pos = result.summary.find(" [lines ");
            if (bracket_pos != std::string::npos) {
                rel_path = result.summary.substr(6, bracket_pos - 6);
                std::string range_str = result.summary.substr(bracket_pos + 8);
                std::size_t dash_pos = range_str.find('-');
                if (dash_pos != std::string::npos) {
                    try {
                        start_line = static_cast<uint32_t>(std::stoul(range_str.substr(0, dash_pos)));
                        end_line = static_cast<uint32_t>(std::stoul(range_str.substr(dash_pos + 1)));
                    } catch (...) {}
                }
            } else {
                rel_path = result.summary.substr(6);
            }
        }

        source::SourceExcerpt excerpt;
        excerpt.file_path = rel_path;
        excerpt.text = result.content;
        excerpt.start_line = start_line;
        excerpt.end_line = end_line;
        excerpt.requested_range = parser::SourceRange{
            .start = parser::SourceLocation{.line = start_line, .column = 1, .byte_offset = 0},
            .end = parser::SourceLocation{.line = end_line, .column = 1, .byte_offset = 0}
        };
        item.source_excerpt = excerpt;

        item.primary_result.unit.file_path = rel_path;
        item.primary_result.unit.role = retrieval::RetrievalUnitRole::Primary;
        item.primary_result.unit.primary_element.name = rel_path.stem().string();
        item.primary_result.unit.primary_element.kind = parser::ElementKind::Class;
        item.primary_result.unit.primary_element.location = excerpt.requested_range;
        item.primary_result.hybrid_score = 1.0;
        item.primary_result.provenance = retrieval::RetrievalProvenance::LexicalOnly;

        items.push_back(std::move(item));
    } else if (result.tool_name == "find_symbol") {
        std::istringstream stream(result.content);
        std::string line;

        std::string current_name;
        std::string current_kind;
        std::filesystem::path current_path;
        uint32_t current_start = 1;
        uint32_t current_end = 1;
        bool in_record = false;

        auto flush_record = [&]() {
            if (in_record && !current_name.empty()) {
                evidence::EvidenceItem item;
                item.primary_result.unit.file_path = current_path;
                item.primary_result.unit.role = retrieval::RetrievalUnitRole::Primary;
                item.primary_result.unit.primary_element.name = current_name;
                item.primary_result.unit.primary_element.kind = parse_element_kind_str(current_kind);
                item.primary_result.unit.primary_element.location = parser::SourceRange{
                    .start = parser::SourceLocation{.line = current_start, .column = 1, .byte_offset = 0},
                    .end = parser::SourceLocation{.line = current_end, .column = 1, .byte_offset = 0}
                };
                item.primary_result.hybrid_score = 1.0;
                item.primary_result.provenance = retrieval::RetrievalProvenance::LexicalOnly;
                items.push_back(std::move(item));
            }
            in_record = false;
            current_name.clear();
            current_kind.clear();
            current_path.clear();
            current_start = 1;
            current_end = 1;
        };

        while (std::getline(stream, line)) {
            // Header style: "[1] Function `calculate_total`"
            if (line.rfind("[", 0) == 0 && line.find('`') != std::string::npos) {
                flush_record();
                std::size_t close_b = line.find("] ");
                if (close_b != std::string::npos) {
                    std::string rest = line.substr(close_b + 2);
                    std::size_t b1 = rest.find('`');
                    std::size_t b2 = rest.rfind('`');
                    if (b1 != std::string::npos && b2 > b1) {
                        current_kind = rest.substr(0, b1);
                        while (!current_kind.empty() && (current_kind.back() == ' ' || current_kind.back() == ':')) {
                            current_kind.pop_back();
                        }
                        current_name = rest.substr(b1 + 1, b2 - b1 - 1);
                        in_record = true;
                    }
                }
            } else if (line.find("File: ") != std::string::npos) {
                std::size_t f_pos = line.find("File: ") + 6;
                std::string loc_full = line.substr(f_pos);
                std::size_t c_pos = std::string::npos;
                for (std::size_t idx = 0; idx < loc_full.size(); ++idx) {
                    if (loc_full[idx] == ':' && (idx + 1) < loc_full.size() &&
                        std::isdigit(static_cast<unsigned char>(loc_full[idx + 1]))) {
                        if (idx == 1 && std::isalpha(static_cast<unsigned char>(loc_full[0]))) {
                            continue; // Windows drive letter like D:
                        }
                        c_pos = idx;
                        break;
                    }
                }
                if (c_pos != std::string::npos) {
                    current_path = loc_full.substr(0, c_pos);
                    std::string range_part = loc_full.substr(c_pos + 1);
                    std::size_t dash_pos = range_part.find('-');
                    if (dash_pos != std::string::npos) {
                        try {
                            current_start = static_cast<uint32_t>(std::stoul(range_part.substr(0, dash_pos)));
                            std::string after_dash = range_part.substr(dash_pos + 1);
                            while (!after_dash.empty() && after_dash.front() == ' ') after_dash.erase(0, 1);
                            current_end = static_cast<uint32_t>(std::stoul(after_dash));
                        } catch (...) {}
                    } else {
                        try {
                            current_start = static_cast<uint32_t>(std::stoul(range_part));
                            current_end = current_start;
                        } catch (...) {}
                    }
                } else {
                    current_path = loc_full;
                }
            } else if (line.rfind("Symbol: ", 0) == 0) {
                // Legacy / fallback style: "Symbol: <name> | Kind: <kind> | Location: <path>:<start>-<end>"
                flush_record();
                std::size_t k_pos = line.find(" | Kind: ");
                std::size_t l_pos = line.find(" | Location: ");
                if (k_pos != std::string::npos && l_pos != std::string::npos) {
                    current_name = line.substr(8, k_pos - 8);
                    current_kind = line.substr(k_pos + 9, l_pos - (k_pos + 9));
                    std::string loc_str = line.substr(l_pos + 13);
                    std::size_t colon = std::string::npos;
                    for (std::size_t idx = 0; idx < loc_str.size(); ++idx) {
                        if (loc_str[idx] == ':' && (idx + 1) < loc_str.size() &&
                            std::isdigit(static_cast<unsigned char>(loc_str[idx + 1]))) {
                            if (idx == 1 && std::isalpha(static_cast<unsigned char>(loc_str[0]))) {
                                continue;
                            }
                            colon = idx;
                            break;
                        }
                    }
                    if (colon != std::string::npos) {
                        current_path = loc_str.substr(0, colon);
                        std::string range = loc_str.substr(colon + 1);
                        std::size_t dash = range.find('-');
                        if (dash != std::string::npos) {
                            try {
                                current_start = static_cast<uint32_t>(std::stoul(range.substr(0, dash)));
                                current_end = static_cast<uint32_t>(std::stoul(range.substr(dash + 1)));
                            } catch (...) {}
                        }
                    } else {
                        current_path = loc_str;
                    }
                    in_record = true;
                    flush_record();
                }
            }
        }
        flush_record();
    } else if (result.tool_name == "get_relationships") {
        evidence::EvidenceItem item;
        item.primary_result.unit.role = retrieval::RetrievalUnitRole::Primary;
        item.primary_result.hybrid_score = 1.0;

        std::istringstream stream(result.content);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.rfind("- ", 0) != 0) {
                continue;
            }
            std::string body = line.substr(2);

            std::string kind_part;
            std::size_t open_b = body.find('[');
            std::size_t close_b = body.find(']');
            if (open_b != std::string::npos && close_b != std::string::npos && close_b > open_b) {
                kind_part = body.substr(open_b + 1, close_b - open_b - 1);
            } else {
                std::size_t arrow_pos = body.find("→");
                if (arrow_pos == std::string::npos) arrow_pos = body.find("←");
                if (arrow_pos == std::string::npos) arrow_pos = body.find("->");
                if (arrow_pos != std::string::npos) {
                    kind_part = body.substr(0, arrow_pos);
                    while (!kind_part.empty() && (kind_part.back() == ' ' || kind_part.back() == ']')) {
                        kind_part.pop_back();
                    }
                    while (!kind_part.empty() && (kind_part.front() == ' ' || kind_part.front() == '[')) {
                        kind_part.erase(0, 1);
                    }
                }
            }

            auto opt_kind = graph::relationship_kind_from_string(kind_part);
            if (opt_kind.has_value()) {
                graph::RelationshipEvidence rel;
                rel.kind = *opt_kind;

                std::size_t b1 = body.find('`');
                std::size_t b2 = body.rfind('`');
                if (b1 != std::string::npos && b2 > b1) {
                    rel.related_name = body.substr(b1 + 1, b2 - b1 - 1);
                }
                item.direct_relationships.push_back(rel);
            }
        }
        if (!item.direct_relationships.empty()) {
            items.push_back(std::move(item));
        }
    } else if (result.tool_name == "search_code") {
        std::istringstream stream(result.content);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.rfind("[", 0) == 0 && line.find('`') != std::string::npos) {
                std::size_t close_b = line.find("] ");
                if (close_b != std::string::npos) {
                    std::string rest = line.substr(close_b + 2);
                    std::size_t backtick_start = rest.find('`');
                    std::size_t backtick_end = rest.rfind('`');
                    if (backtick_start != std::string::npos && backtick_end > backtick_start) {
                        std::string kind_part = rest.substr(0, backtick_start);
                        while (!kind_part.empty() && (kind_part.back() == ' ' || kind_part.back() == ':')) {
                            kind_part.pop_back();
                        }
                        std::string sym_name = rest.substr(backtick_start + 1, backtick_end - backtick_start - 1);

                        std::string next_line;
                        std::filesystem::path path;
                        uint32_t start_line = 1;

                        if (std::getline(stream, next_line) && next_line.find("File: ") != std::string::npos) {
                            std::size_t f_pos = next_line.find("File: ") + 6;
                            std::string loc_full = next_line.substr(f_pos);
                            std::size_t c_pos = std::string::npos;
                            for (std::size_t idx = 0; idx < loc_full.size(); ++idx) {
                                if (loc_full[idx] == ':' && (idx + 1) < loc_full.size() &&
                                    std::isdigit(static_cast<unsigned char>(loc_full[idx + 1]))) {
                                    if (idx == 1 && std::isalpha(static_cast<unsigned char>(loc_full[0]))) {
                                        continue;
                                    }
                                    c_pos = idx;
                                    break;
                                }
                            }
                            if (c_pos != std::string::npos) {
                                path = loc_full.substr(0, c_pos);
                                try {
                                    start_line = static_cast<uint32_t>(std::stoul(loc_full.substr(c_pos + 1)));
                                } catch (...) {}
                            } else {
                                path = loc_full;
                            }
                        }

                        evidence::EvidenceItem item;
                        item.primary_result.unit.file_path = path;
                        item.primary_result.unit.role = retrieval::RetrievalUnitRole::Primary;
                        item.primary_result.unit.primary_element.name = sym_name;
                        item.primary_result.unit.primary_element.kind = parse_element_kind_str(kind_part);
                        item.primary_result.unit.primary_element.location = parser::SourceRange{
                            .start = parser::SourceLocation{.line = start_line, .column = 1, .byte_offset = 0},
                            .end = parser::SourceLocation{.line = start_line + 10, .column = 1, .byte_offset = 0}
                        };
                        item.primary_result.hybrid_score = 1.0;
                        item.primary_result.provenance = retrieval::RetrievalProvenance::HybridBoth;
                        items.push_back(std::move(item));
                    }
                }
            }
        }
    }

    return items;
}

bool ToolEvidenceAdapter::integrate_into_bundle(evidence::EvidenceBundle& bundle,
                                               const ToolResult& result) {
    auto items = to_evidence_items(result);
    if (items.empty()) {
        return false;
    }
    for (auto& item : items) {
        bundle.items.push_back(std::move(item));
    }
    return true;
}

} // namespace amoeba::tools
