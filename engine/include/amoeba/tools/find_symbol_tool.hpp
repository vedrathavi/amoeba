#pragma once

#include "amoeba/tools/repository_tool.hpp"

namespace amoeba::tools {

/**
 * @brief Repository tool for locating symbol declarations across indexed files.
 *
 * Adapts InvertedIndex and parsed AST representations to the RepositoryTool interface.
 */
class FindSymbolTool final : public RepositoryTool {
public:
    FindSymbolTool() = default;
    ~FindSymbolTool() override = default;

    [[nodiscard]] std::string name() const override { return "find_symbol"; }

    [[nodiscard]] ToolDescription description() const override;

    [[nodiscard]] ToolResult execute(const ToolRequest& request,
                                     const ToolExecutionContext& context) const override;
};

}  // namespace amoeba::tools
