#include "amoeba/retrieval/supporting_evidence_resolver.hpp"

#include <algorithm>
#include <limits>

namespace amoeba::retrieval {

// ─────────────────────────────────────────────────────────────────────────────
// is_enclosed_by
// ─────────────────────────────────────────────────────────────────────────────

bool SupportingEvidenceResolver::is_enclosed_by(const parser::CodeElement& child,
                                                const parser::CodeElement& parent) noexcept {

    // Rule 1: parent_context match — authoritative structural attribution.
    // Tree-sitter parsers populate parent_context with the enclosing symbol
    // name; this is the most reliable containment signal.
    if (!child.parent_context.empty() && child.parent_context == parent.name) {
        return true;
    }

    // Rule 2: Source range containment.
    // Only applied if the parent has valid, non-degenerate line numbers.
    const auto& ps = parent.location.start;
    const auto& pe = parent.location.end;
    if (ps.line > 0 && pe.line >= ps.line) {
        const auto& cs = child.location.start;
        const auto& ce = child.location.end;
        if (cs.line >= ps.line && ce.line <= pe.line) {
            return true;
        }
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helper: find best enclosing primary unit
// Returns the index into `units`, or -1 if no confident match.
// ─────────────────────────────────────────────────────────────────────────────

static int find_best_enclosing_unit(const parser::CodeElement& el,
                                    const std::vector<RetrievalUnit>& units) noexcept {
    int best_idx = -1;
    uint32_t smallest_span = std::numeric_limits<uint32_t>::max();

    for (std::size_t u = 0; u < units.size(); ++u) {
        const auto& primary = units[u].primary_element;
        if (!SupportingEvidenceResolver::is_enclosed_by(el, primary)) {
            continue;
        }
        // Among all enclosing primaries, prefer the innermost (smallest span).
        const auto& ps = primary.location.start;
        const auto& pe = primary.location.end;
        uint32_t span = (pe.line >= ps.line && ps.line > 0) ? (pe.line - ps.line)
                                                            : std::numeric_limits<uint32_t>::max();
        if (span < smallest_span) {
            smallest_span = span;
            best_idx = static_cast<int>(u);
        }
    }

    return best_idx;
}

// ─────────────────────────────────────────────────────────────────────────────
// resolve_units (single file)
// ─────────────────────────────────────────────────────────────────────────────

std::vector<RetrievalUnit>
SupportingEvidenceResolver::resolve_units(const parser::ParsedFile& parsed_file) {
    std::vector<RetrievalUnit> units;
    std::vector<EvidenceResolution> ignored;
    resolve_with_diagnostics(parsed_file, units, ignored);
    return units;
}

// ─────────────────────────────────────────────────────────────────────────────
// resolve_units (multiple files)
// ─────────────────────────────────────────────────────────────────────────────

std::vector<RetrievalUnit>
SupportingEvidenceResolver::resolve_units(std::span<const parser::ParsedFile> parsed_files) {
    std::vector<RetrievalUnit> all_units;
    for (const auto& file : parsed_files) {
        auto file_units = resolve_units(file);
        all_units.insert(all_units.end(), std::make_move_iterator(file_units.begin()),
                         std::make_move_iterator(file_units.end()));
    }
    return all_units;
}

// ─────────────────────────────────────────────────────────────────────────────
// resolve_with_diagnostics — core implementation
// ─────────────────────────────────────────────────────────────────────────────

void SupportingEvidenceResolver::resolve_with_diagnostics(
    const parser::ParsedFile& parsed_file, std::vector<RetrievalUnit>& units,
    std::vector<EvidenceResolution>& resolutions) {

    units.clear();
    resolutions.clear();

    const auto& elements = parsed_file.elements;
    if (elements.empty()) {
        return;
    }

    // Pass 1: Collect primary units in file order.
    for (std::size_t i = 0; i < elements.size(); ++i) {
        const auto& el = elements[i];
        if (RetrievalUnitClassifier::is_primary(el.kind)) {
            RetrievalUnit unit;
            unit.primary_element_id = static_cast<uint32_t>(i);
            unit.primary_element = el;
            unit.file_path = parsed_file.file_path;
            unit.language = parsed_file.language;
            unit.role = RetrievalUnitRole::Primary;
            units.push_back(std::move(unit));
        }
    }

    // Synthetic module-level unit for files with no primary symbols.
    //
    // Rationale: Pure CSS files, JSON-like configs, or files that contain only
    // supporting elements (selectors, properties) are still semantically
    // searchable. A single file-level unit prevents them from disappearing from
    // the retrieval candidate set entirely. Since the file has only one
    // primary (the synthetic one), all elements in the file safely belong to it.
    bool is_module_level_unit = false;
    if (units.empty()) {
        parser::CodeElement module_primary;
        module_primary.kind = parser::ElementKind::Route;
        module_primary.name = parsed_file.file_path.filename().string();
        module_primary.location.start = {.line = 1, .column = 1, .byte_offset = 0};
        module_primary.location.end = {
            .line = std::numeric_limits<uint32_t>::max(), .column = 1, .byte_offset = 0};
        module_primary.parent_context = "";
        module_primary.detail = "File Module";

        RetrievalUnit file_unit;
        file_unit.primary_element_id = 0;
        file_unit.primary_element = std::move(module_primary);
        file_unit.file_path = parsed_file.file_path;
        file_unit.language = parsed_file.language;
        file_unit.role = RetrievalUnitRole::Primary;
        units.push_back(std::move(file_unit));
        is_module_level_unit = true;
    }

    // Pass 2: Attribute each supporting element to its innermost enclosing
    // primary unit.
    for (std::size_t i = 0; i < elements.size(); ++i) {
        const auto& el = elements[i];
        if (!RetrievalUnitClassifier::is_supporting(el.kind)) {
            continue;
        }

        EvidenceResolution res;
        res.supporting_element_idx = static_cast<uint32_t>(i);

        if (is_module_level_unit) {
            // The only primary is the synthetic file-level unit; all elements
            // unambiguously belong to it.
            res.owner_unit_index = 0;
        } else {
            int best = find_best_enclosing_unit(el, units);
            if (best >= 0) {
                res.owner_unit_index = static_cast<uint32_t>(best);
            }
            // else: owner_unit_index remains nullopt (unresolved)
        }

        resolutions.push_back(res);

        if (res.owner_unit_index.has_value()) {
            uint32_t uid = *res.owner_unit_index;
            units[uid].supporting_element_ids.push_back(static_cast<uint32_t>(i));
            units[uid].supporting_elements.push_back(el);
        }
    }
}

}  // namespace amoeba::retrieval
