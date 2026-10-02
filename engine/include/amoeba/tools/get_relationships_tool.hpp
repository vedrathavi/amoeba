#pragma once

#include "amoeba/tools/repository_tool.hpp"

namespace amoeba::tools {

/**
 * @brief Repository tool for discovering direct 1-hop code relationships.
 *
 * Adapts RelationshipGraph and RelationshipEvidenceResolver to the RepositoryTool interface.
 */
class GetRelationshipsTool final : public RepositoryTool {
public:
    GetRelationshipsTool() = default;
    ~GetRelationshipsTool() override = default;

    [[nodiscard]] std::string name() const override { return "get_relationships"; }

    [[nodiscard]] ToolDescription description() const override;

    [[nodiscard]] ToolResult execute(const ToolRequest& request,
                                     const ToolExecutionContext& context) const override;
};

}  // namespace amoeba::tools
