#include "amoeba/tools/tool_registry.hpp"
#include "amoeba/tools/find_symbol_tool.hpp"
#include "amoeba/tools/get_relationships_tool.hpp"
#include "amoeba/tools/read_file_tool.hpp"
#include "amoeba/tools/search_code_tool.hpp"

#include <algorithm>

namespace amoeba::tools {

bool ToolRegistry::register_tool(std::unique_ptr<RepositoryTool> tool) {
    if (!tool) {
        return false;
    }
    const std::string name = tool->name();
    if (name.empty()) {
        return false;
    }
    if (tools_.find(name) != tools_.end()) {
        return false;
    }
    tools_[name] = std::move(tool);
    return true;
}

bool ToolRegistry::has_tool(std::string_view name) const noexcept {
    return tools_.find(std::string(name)) != tools_.end();
}

const RepositoryTool* ToolRegistry::find_tool(std::string_view name) const noexcept {
    auto it = tools_.find(std::string(name));
    if (it == tools_.end()) {
        return nullptr;
    }
    return it->second.get();
}

std::vector<std::string> ToolRegistry::list_tools() const {
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& [name, _] : tools_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<ToolDescription> ToolRegistry::list_descriptions() const {
    std::vector<ToolDescription> descriptions;
    descriptions.reserve(tools_.size());
    for (const auto& name : list_tools()) {
        const auto* tool = find_tool(name);
        if (tool) {
            descriptions.push_back(tool->description());
        }
    }
    return descriptions;
}

ToolResult ToolRegistry::execute(const ToolRequest& request,
                                const ToolExecutionContext& context) const {
    const auto* tool = find_tool(request.tool_name);
    if (!tool) {
        ToolResult res;
        res.status = ToolResultStatus::NotFound;
        res.tool_name = request.tool_name;
        res.error_code = "UNKNOWN_TOOL";
        res.summary = "Tool '" + request.tool_name + "' is not registered";
        return res;
    }
    return tool->execute(request, context);
}

ToolRegistry ToolRegistry::create_default_registry() {
    ToolRegistry registry;
    registry.register_tool(std::make_unique<SearchCodeTool>());
    registry.register_tool(std::make_unique<FindSymbolTool>());
    registry.register_tool(std::make_unique<ReadFileTool>());
    registry.register_tool(std::make_unique<GetRelationshipsTool>());
    return registry;
}

} // namespace amoeba::tools
