#pragma once

#include "amoeba/tools/repository_tool.hpp"

namespace amoeba::tools {

/**
 * @brief Security-hardened repository tool for reading bounded source file ranges.
 *
 * Enforces strict repository-root boundaries, directory traversal rejection,
 * sensitive file filtering, and line/character bounds via RepositoryAccessPolicy.
 */
class ReadFileTool final : public RepositoryTool {
public:
    ReadFileTool() = default;
    ~ReadFileTool() override = default;

    [[nodiscard]] std::string name() const override { return "read_file"; }

    [[nodiscard]] ToolDescription description() const override;

    [[nodiscard]] ToolResult execute(const ToolRequest& request,
                                     const ToolExecutionContext& context) const override;
};

}  // namespace amoeba::tools
