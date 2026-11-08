#include "amoeba/context/context_builder.hpp"

#include "amoeba/graph/relationship_evidence.hpp"
#include "amoeba/parser/code_element.hpp"

#include <algorithm>
#include <sstream>
#include <string_view>

namespace amoeba::context {

namespace {

[[nodiscard]] std::string_view safe_utf8_prefix(std::string_view str,
                                                std::size_t max_bytes) noexcept {
    if (str.size() <= max_bytes) {
        return str;
    }
    std::size_t i = max_bytes;
    while (i > 0 && (static_cast<unsigned char>(str[i]) & 0xC0) == 0x80) {
        --i;
    }
    // If str[max_bytes] was a continuation byte, i is the index of the lead byte.
    // That multibyte character cannot fit in max_bytes, so truncate before it.
    if (i < max_bytes) {
        return str.substr(0, i);
    }
    return str.substr(0, max_bytes);
}

[[nodiscard]] std::string format_relationship(const graph::RelationshipEvidence& rel) {
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

    if (rel.related_name.empty() && rel.related_file_path.empty()) {
        ss << " (unresolved)";
    }

    return ss.str();
}

struct WindowSlice {
    std::string text;
    uint32_t start_line{0};
    uint32_t end_line{0};
    bool truncated{false};
    std::size_t truncated_lines_count{0};
};

[[nodiscard]] WindowSlice extract_anchor_centered_source(
    const source::SourceExcerpt& exc,
    const parser::CodeElement& elem,
    std::size_t max_lines) {

    if (max_lines == 0 || exc.text.empty()) {
        return WindowSlice{
            .text = exc.text,
            .start_line = exc.start_line,
            .end_line = exc.end_line,
            .truncated = false,
            .truncated_lines_count = 0,
        };
    }

    std::vector<std::string_view> lines;
    std::size_t pos = 0;
    while (pos < exc.text.size()) {
        std::size_t next_nl = exc.text.find('\n', pos);
        if (next_nl == std::string_view::npos) {
            lines.push_back(std::string_view(exc.text).substr(pos));
            break;
        }
        lines.push_back(std::string_view(exc.text).substr(pos, next_nl - pos));
        pos = next_nl + 1;
    }

    const uint32_t exc_start = exc.start_line;
    const uint32_t exc_end = exc.end_line;
    const std::size_t total_lines = lines.size();

    const uint32_t sym_start = elem.location.start.line;
    const uint32_t sym_end = elem.location.end.line;

    if (sym_start > 0 && sym_end >= sym_start) {
        const uint32_t sym_span = sym_end - sym_start + 1;

        // 1. If focal symbol fits in max_lines, extract complete definition
        if (sym_span <= max_lines) {
            if (sym_start >= exc_start && sym_start <= exc_end) {
                const std::size_t offset_start = sym_start - exc_start;
                const std::size_t count = std::min(
                    static_cast<std::size_t>(sym_span),
                    lines.size() > offset_start ? lines.size() - offset_start : 0);
                std::string result;
                for (std::size_t idx = 0; idx < count; ++idx) {
                    result.append(lines[offset_start + idx]);
                    result.push_back('\n');
                }
                return WindowSlice{
                    .text = std::move(result),
                    .start_line = sym_start,
                    .end_line = static_cast<uint32_t>(sym_start + count - 1),
                    .truncated = false,
                    .truncated_lines_count = 0,
                };
            }
        } else {
            // 2. Focal symbol exceeds max_lines: extract anchor-centered window [sym_start - 5, sym_start + max_lines - 5]
            const uint32_t win_start = (sym_start > 5) ? (sym_start - 5) : 1;
            const uint32_t win_end = win_start + static_cast<uint32_t>(max_lines) - 1;

            const uint32_t clamped_start = std::max(win_start, exc_start);
            const uint32_t clamped_end = std::min(win_end, exc_end);

            if (clamped_start <= clamped_end && clamped_start >= exc_start) {
                const std::size_t offset_start = clamped_start - exc_start;
                const std::size_t count = std::min(
                    static_cast<std::size_t>(clamped_end - clamped_start + 1),
                    lines.size() > offset_start ? lines.size() - offset_start : 0);
                std::string result;
                for (std::size_t idx = 0; idx < count; ++idx) {
                    result.append(lines[offset_start + idx]);
                    result.push_back('\n');
                }
                const std::size_t remaining = (total_lines > count) ? (total_lines - count) : 0;
                if (remaining > 0) {
                    result += "// ... [truncated " + std::to_string(remaining) + " remaining lines]";
                }
                return WindowSlice{
                    .text = std::move(result),
                    .start_line = clamped_start,
                    .end_line = clamped_end,
                    .truncated = (remaining > 0),
                    .truncated_lines_count = remaining,
                };
            }
        }
    }

    // Fallback: standard line truncation from top of excerpt
    if (lines.size() <= max_lines) {
        return WindowSlice{
            .text = std::string(exc.text),
            .start_line = exc.start_line,
            .end_line = exc.end_line,
            .truncated = false,
            .truncated_lines_count = 0,
        };
    }

    std::string result;
    for (std::size_t i = 0; i < max_lines; ++i) {
        result.append(lines[i]);
        result.push_back('\n');
    }
    const std::size_t remaining = lines.size() - max_lines;
    result += "// ... [truncated " + std::to_string(remaining) + " remaining lines]";
    return WindowSlice{
        .text = std::move(result),
        .start_line = exc.start_line,
        .end_line = static_cast<uint32_t>(exc.start_line + max_lines - 1),
        .truncated = true,
        .truncated_lines_count = remaining,
    };
}

[[nodiscard]] std::string render_compressed_item(const evidence::EvidenceItem& item,
                                                  std::size_t result_index) {
    std::ostringstream ss;
    const auto& elem = item.primary_element();
    const std::string symbol_name =
        elem.name.empty() ? item.file_path().filename().string() : elem.name;

    if (item.is_expanded_relationship) {
        ss << "## Supporting Evidence (Compressed): `" << symbol_name << "`\n";
    } else {
        ss << "## Result " << (result_index + 1) << " (Compressed): `" << symbol_name << "`\n";
    }
    ss << "- **Kind**: " << parser::to_string(elem.kind) << "\n";
    ss << "- **File**: " << item.file_path().generic_string() << "\n";
    if (elem.location.start.line > 0) {
        ss << "- **Range**: L" << elem.location.start.line << ":C" << elem.location.start.column
           << " - L" << elem.location.end.line << ":C" << elem.location.end.column << "\n";
    }
    if (!elem.parent_context.empty()) {
        ss << "- **Parent**: `" << elem.parent_context << "`\n";
    }
    if (!elem.detail.empty()) {
        ss << "- **Detail**: `" << elem.detail << "`\n";
    }
    if (!elem.signature.empty() && elem.signature != elem.detail) {
        ss << "- **Signature**: `" << elem.signature << "`\n";
    }
    if (!elem.documentation.empty()) {
        ss << "- **Doc**: " << elem.documentation << "\n";
    }
    ss << "\n---\n\n";
    return ss.str();
}

}  // namespace

ContextBuilder::ContextBuilder(const ContextBuilderOptions& options) : options_(options) {}

ContextPackage ContextBuilder::build(const evidence::EvidenceBundle& bundle) const {
    ContextPackage pkg;
    pkg.query = bundle.query;
    pkg.total_available_items = bundle.items.size();
    pkg.max_character_budget = options_.max_character_budget;

    if (bundle.items.empty()) {
        pkg.rendered_markdown =
            "# Context: " + bundle.query + "\n\nNo relevant code elements were retrieved.\n";
        pkg.used_characters = pkg.rendered_markdown.size();
        pkg.selected_item_count = 0;
        pkg.truncated = false;
        return pkg;
    }

    const std::size_t items_to_process = std::min(bundle.items.size(), options_.max_primary_items);
    if (bundle.items.size() > options_.max_primary_items) {
        pkg.truncated = true;
        pkg.truncation_reason =
            "Exceeded max_primary_items limit (" + std::to_string(options_.max_primary_items) + ")";
    }

    std::ostringstream header_ss;
    header_ss << "# Context: " << bundle.query << "\n\n";
    std::string current_markdown = header_ss.str();

    for (std::size_t i = 0; i < items_to_process; ++i) {
        const auto& item = bundle.items[i];
        std::ostringstream item_ss;

        const auto& elem = item.primary_element();
        const std::string symbol_name =
            elem.name.empty() ? item.file_path().filename().string() : elem.name;

        if (item.is_expanded_relationship) {
            item_ss << "## Supporting Evidence (Expanded via " << item.expansion_direction
                    << " " << item.expansion_relationship_type << " from `"
                    << item.expansion_seed_name << "`): `" << symbol_name << "`\n";
        } else {
            item_ss << "## Result " << (i + 1) << ": `" << symbol_name << "`\n";
        }

        if (options_.include_metadata) {
            item_ss << "- **Kind**: " << parser::to_string(elem.kind) << "\n";
            item_ss << "- **File**: " << item.file_path().generic_string() << "\n";
            if (elem.location.start.line > 0) {
                item_ss << "- **Range**: L" << elem.location.start.line << ":C"
                        << elem.location.start.column << " - L" << elem.location.end.line << ":C"
                        << elem.location.end.column << "\n";
            }
            if (!elem.parent_context.empty()) {
                item_ss << "- **Parent**: `" << elem.parent_context << "`\n";
            }
            if (!elem.detail.empty()) {
                item_ss << "- **Detail**: `" << elem.detail << "`\n";
            }
        }

        // 1. Source excerpt
        if (options_.include_source) {
            if (item.source_excerpt.has_value()) {
                const auto& exc = *item.source_excerpt;
                const auto slice = extract_anchor_centered_source(
                    exc, elem, options_.max_source_lines_per_item);
                item_ss << "\n### Source Excerpt (Lines " << slice.start_line << "-" << slice.end_line
                        << "):\n";
                const std::string& lang = item.primary_result.unit.language.empty()
                                              ? "cpp"
                                              : item.primary_result.unit.language;
                item_ss << "```" << lang << "\n";
                item_ss << slice.text;
                if (!slice.text.ends_with('\n')) {
                    item_ss << "\n";
                }
                item_ss << "```\n";
            } else {
                item_ss << "\n### Source Excerpt:\n*[Source code unavailable]*\n";
            }
        }

        // 2. Supporting AST elements
        if (options_.include_supporting_evidence && !item.supporting_elements().empty()) {
            item_ss << "\n### Supporting AST Elements:\n";
            const auto& supp = item.supporting_elements();
            const std::size_t count =
                std::min(supp.size(), options_.max_supporting_elements_per_item);
            for (std::size_t s = 0; s < count; ++s) {
                const auto& se = supp[s];
                item_ss << "- " << parser::to_string(se.kind) << " `" << se.name << "` (L"
                        << se.location.start.line << ":C" << se.location.start.column << " - L"
                        << se.location.end.line << ":C" << se.location.end.column << ")\n";
            }
            if (supp.size() > options_.max_supporting_elements_per_item) {
                item_ss << "- *[and " << (supp.size() - options_.max_supporting_elements_per_item)
                        << " more supporting elements]*\n";
            }
        }

        // 3. Direct Relationships
        if (options_.include_relationships && !item.direct_relationships.empty()) {
            item_ss << "\n### Direct Relationships:\n";
            const auto& rels = item.direct_relationships;
            const std::size_t count = std::min(rels.size(), options_.max_relationships_per_item);
            for (std::size_t r = 0; r < count; ++r) {
                item_ss << "- " << format_relationship(rels[r]) << "\n";
            }
            if (rels.size() > options_.max_relationships_per_item) {
                item_ss << "- *[and " << (rels.size() - options_.max_relationships_per_item)
                        << " more direct relationships]*\n";
            }
        }

        item_ss << "\n---\n\n";
        const std::string item_text = item_ss.str();

        // Budget check
        if (options_.max_character_budget > 0 &&
            current_markdown.size() + item_text.size() > options_.max_character_budget) {

            // Candidate Compression Fallback:
            // Attempt to render in compressed form without full source or relationship tree
            const std::string compressed_text = render_compressed_item(item, i);
            if (current_markdown.size() + compressed_text.size() <= options_.max_character_budget) {
                pkg.truncated = true;
                if (pkg.truncation_reason.empty()) {
                    pkg.truncation_reason = "Exceeded character budget (" +
                                            std::to_string(options_.max_character_budget) +
                                            " chars); subsequent items compressed";
                }
                current_markdown += compressed_text;
                pkg.selected_items.push_back(item);
                continue;
            }

            pkg.truncated = true;
            if (pkg.truncation_reason.empty()) {
                pkg.truncation_reason = "Exceeded character budget (" +
                                        std::to_string(options_.max_character_budget) + " chars)";
            }

            // If we have not included any item yet, include a safe UTF-8 prefix of the first item
            if (pkg.selected_items.empty()) {
                const std::string suffix = "\n\n... [Context truncated to fit budget]\n";
                const std::size_t avail =
                    (options_.max_character_budget > current_markdown.size() + suffix.size())
                        ? (options_.max_character_budget - current_markdown.size() - suffix.size())
                        : 0;
                if (avail > 0) {
                    current_markdown += std::string(safe_utf8_prefix(item_text, avail));
                }
                current_markdown += suffix;
                pkg.selected_items.push_back(item);
            }
            break;
        }

        current_markdown += item_text;
        pkg.selected_items.push_back(item);
    }

    pkg.rendered_markdown = std::move(current_markdown);
    pkg.selected_item_count = pkg.selected_items.size();
    pkg.used_characters = pkg.rendered_markdown.size();

    return pkg;
}

}  // namespace amoeba::context
