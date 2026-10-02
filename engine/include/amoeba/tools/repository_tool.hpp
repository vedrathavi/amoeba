#pragma once

#include "amoeba/tools/tool_execution_context.hpp"
#include "amoeba/tools/tool_types.hpp"

#include <string>

namespace amoeba::tools {

/**
 * @brief Minimal, cohesive interface for repository exploration tools.
 *
 * Design constraints:
 * - Decoupled from LLM runtimes, prompts, providers, and agent loops.
 * - Read-only, deterministic, and bounded.
 * - Operates as an adapter over existing Amoeba engine capabilities.
 */
class RepositoryTool {
public:
    virtual ~RepositoryTool() = default;

    /**
     * @brief Unique identifier/name of the tool (e.g. "search_code", "read_file").
     */
    [[nodiscard]] virtual std::string name() const = 0;

    /**
     * @brief Self-describing schema metadata for parameter validation and discovery.
     */
    [[nodiscard]] virtual ToolDescription description() const = 0;

    /**
     * @brief Executes the requested tool operation within the bounded execution context.
     *
     * @param request Immutable tool invocation request and arguments.
     * @param context Execution context containing authorized read-only engine capabilities.
     * @return Structured ToolResult.
     */
    [[nodiscard]] virtual ToolResult execute(const ToolRequest& request,
                                            const ToolExecutionContext& context) const = 0;
};

}  // namespace amoeba::tools
