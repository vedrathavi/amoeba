#pragma once

#include "amoeba/evidence/evidence_bundle.hpp"
#include "amoeba/tools/tool_types.hpp"

#include <optional>
#include <vector>

namespace amoeba::tools {

/**
 * @brief Converts ToolResult objects into first-class Amoeba repository evidence.
 *
 * This adapter bridges the tool subsystem and the evidence evaluation pipeline.
 * Tool results from read_file, find_symbol, get_relationships, or search_code
 * are converted into EvidenceItem instances, enabling the existing EvidenceSufficiencyChecker
 * to evaluate whether additional tool discoveries satisfy the query's evidence requirements.
 */
class ToolEvidenceAdapter {
public:
    /**
     * @brief Converts a successful ToolResult into an optional EvidenceItem.
     *
     * Returns std::nullopt if the tool result is not successful, empty, or cannot be adapted.
     */
    [[nodiscard]] static std::optional<evidence::EvidenceItem>
    to_evidence_item(const ToolResult& result);

    /**
     * @brief Converts a successful ToolResult into zero or more EvidenceItems.
     */
    [[nodiscard]] static std::vector<evidence::EvidenceItem>
    to_evidence_items(const ToolResult& result);

    /**
     * @brief Integrates a ToolResult into an existing EvidenceBundle.
     *
     * Appends any generated EvidenceItems to the bundle's items list.
     * Returns true if at least one evidence item was successfully added.
     */
    static bool integrate_into_bundle(evidence::EvidenceBundle& bundle,
                                      const ToolResult& result);
};

} // namespace amoeba::tools
