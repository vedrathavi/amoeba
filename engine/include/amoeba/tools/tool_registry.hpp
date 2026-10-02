#pragma once

#include "amoeba/tools/repository_tool.hpp"
#include "amoeba/tools/tool_execution_context.hpp"
#include "amoeba/tools/tool_types.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace amoeba::tools {

/**
 * @brief Registry and dispatcher for repository exploration tools.
 *
 * Responsibilities:
 * - Owns and manages registered RepositoryTool instances.
 * - Dispatches ToolRequests to specific tools without giant conditional/switch chains in reasoning.
 * - Provides deterministic tool discovery and metadata listings.
 */
class ToolRegistry {
public:
    ToolRegistry() = default;
    ~ToolRegistry() = default;

    ToolRegistry(const ToolRegistry&) = delete;
    ToolRegistry& operator=(const ToolRegistry&) = delete;
    ToolRegistry(ToolRegistry&&) noexcept = default;
    ToolRegistry& operator=(ToolRegistry&&) noexcept = default;

    /**
     * @brief Registers a repository tool instance.
     * @param tool Owned tool instance.
     * @return true if successfully registered, false if a tool with the same name already exists.
     */
    bool register_tool(std::unique_ptr<RepositoryTool> tool);

    /**
     * @brief Finds a registered tool by name.
     * @param name Tool identifier.
     * @return Pointer to tool, or nullptr if not registered.
     */
    [[nodiscard]] const RepositoryTool* find_tool(std::string_view name) const noexcept;

    /**
     * @brief Checks if a tool with the given name is registered.
     */
    [[nodiscard]] bool has_tool(std::string_view name) const noexcept;

    /**
     * @brief Returns a deterministically sorted list of all registered tool names.
     */
    [[nodiscard]] std::vector<std::string> list_tools() const;

    /**
     * @brief Returns a deterministically sorted list of metadata descriptions for all tools.
     */
    [[nodiscard]] std::vector<ToolDescription> list_descriptions() const;

    /**
     * @brief Executes a tool request through the registry dispatcher.
     *
     * If the tool is not registered, returns ToolResult with ToolResultStatus::NotFound.
     */
    [[nodiscard]] ToolResult execute(const ToolRequest& request,
                                     const ToolExecutionContext& context) const;

    [[nodiscard]] std::size_t size() const noexcept { return tools_.size(); }
    [[nodiscard]] bool empty() const noexcept { return tools_.empty(); }

    /**
     * @brief Helper to create a ToolRegistry pre-populated with Amoeba's standard built-in tools:
     * (search_code, find_symbol, read_file, get_relationships).
     */
    [[nodiscard]] static ToolRegistry create_default_registry();

private:
    std::unordered_map<std::string, std::unique_ptr<RepositoryTool>> tools_;
};

}  // namespace amoeba::tools
