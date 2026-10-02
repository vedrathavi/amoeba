#pragma once

#include "amoeba/tools/repository_tool.hpp"

namespace amoeba::tools {

/**
 * @brief Repository tool providing hybrid semantic and lexical code retrieval.
 *
 * Adapts PrimaryRetrievalPipeline to the RepositoryTool interface.
 */
class SearchCodeTool final : public RepositoryTool {
public:
    SearchCodeTool() = default;
    ~SearchCodeTool() override = default;

    [[nodiscard]] std::string name() const override { return "search_code"; }

    [[nodiscard]] ToolDescription description() const override;

    [[nodiscard]] ToolResult execute(const ToolRequest& request,
                                     const ToolExecutionContext& context) const override;
};

}  // namespace amoeba::tools
