#include "amoeba/tools/tool_registry.hpp"
#include "amoeba/tools/repository_tool.hpp"

#include <gtest/gtest.h>

using namespace amoeba::tools;

namespace {

class DummyEchoTool : public RepositoryTool {
public:
    [[nodiscard]] std::string name() const override {
        return "echo_test";
    }

    [[nodiscard]] ToolDescription description() const override {
        return ToolDescription{
            .name = "echo_test",
            .description = "Echoes back the input message for testing",
            .parameters = {
                ToolParameter{
                    .name = "message",
                    .type = "string",
                    .description = "The message to echo",
                    .required = true
                }
            }
        };
    }

    [[nodiscard]] ToolResult execute(const ToolRequest& request,
                                     const ToolExecutionContext&) const override {
        auto it = request.arguments.find("message");
        if (it == request.arguments.end()) {
            ToolResult res;
            res.status = ToolResultStatus::InvalidRequest;
            res.tool_name = name();
            res.error_code = "MISSING_ARGUMENT";
            res.summary = "Missing required argument 'message'";
            return res;
        }
        ToolResult result;
        result.status = ToolResultStatus::Success;
        result.tool_name = name();
        result.summary = "Echoed message";
        result.content = it->second;
        return result;
    }
};

} // namespace

TEST(ToolRegistryTest, RegisterAndLookup) {
    ToolRegistry registry;
    EXPECT_FALSE(registry.has_tool("echo_test"));
    EXPECT_EQ(registry.find_tool("echo_test"), nullptr);

    EXPECT_TRUE(registry.register_tool(std::make_unique<DummyEchoTool>()));
    EXPECT_TRUE(registry.has_tool("echo_test"));
    EXPECT_NE(registry.find_tool("echo_test"), nullptr);
}

TEST(ToolRegistryTest, RejectDuplicateRegistration) {
    ToolRegistry registry;
    EXPECT_TRUE(registry.register_tool(std::make_unique<DummyEchoTool>()));
    EXPECT_FALSE(registry.register_tool(std::make_unique<DummyEchoTool>()));
}

TEST(ToolRegistryTest, RejectNullTool) {
    ToolRegistry registry;
    EXPECT_FALSE(registry.register_tool(nullptr));
}

TEST(ToolRegistryTest, UnknownToolExecution) {
    ToolRegistry registry;
    ToolExecutionContext ctx;
    ToolRequest req{.tool_name = "non_existent_tool"};

    auto res = registry.execute(req, ctx);
    EXPECT_EQ(res.status, ToolResultStatus::NotFound);
    EXPECT_EQ(res.error_code, "UNKNOWN_TOOL");
    EXPECT_EQ(res.tool_name, "non_existent_tool");
}

TEST(ToolRegistryTest, ExecuteRegisteredTool) {
    ToolRegistry registry;
    registry.register_tool(std::make_unique<DummyEchoTool>());

    ToolExecutionContext ctx;
    ToolRequest req{
        .tool_name = "echo_test",
        .arguments = {{"message", "Hello Amoeba"}}
    };

    auto res = registry.execute(req, ctx);
    EXPECT_EQ(res.status, ToolResultStatus::Success);
    EXPECT_EQ(res.tool_name, "echo_test");
    EXPECT_EQ(res.content, "Hello Amoeba");
}

TEST(ToolRegistryTest, DeterministicDescriptionList) {
    auto registry = ToolRegistry::create_default_registry();
    auto descriptions = registry.list_descriptions();

    EXPECT_EQ(descriptions.size(), 4u);
    // Names sorted alphabetically: find_symbol, get_relationships, read_file, search_code
    EXPECT_EQ(descriptions[0].name, "find_symbol");
    EXPECT_EQ(descriptions[1].name, "get_relationships");
    EXPECT_EQ(descriptions[2].name, "read_file");
    EXPECT_EQ(descriptions[3].name, "search_code");
}

TEST(ToolRegistryTest, ExtensibilityWithoutCentralModification) {
    // Prove that adding a new tool requires only registering the class
    ToolRegistry registry;
    EXPECT_TRUE(registry.register_tool(std::make_unique<DummyEchoTool>()));

    ToolExecutionContext ctx;
    ToolRequest req{.tool_name = "echo_test", .arguments = {{"message", "plug-and-play"}}};
    auto res = registry.execute(req, ctx);

    EXPECT_EQ(res.status, ToolResultStatus::Success);
    EXPECT_EQ(res.content, "plug-and-play");
}
