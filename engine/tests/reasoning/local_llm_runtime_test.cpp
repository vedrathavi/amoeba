#include "amoeba/reasoning/local_llm_runtime.hpp"
#include "amoeba/reasoning/reasoning_service.hpp"
#include "amoeba/reasoning/response_sink.hpp"

#include <gtest/gtest.h>

namespace {

using namespace amoeba;
using namespace amoeba::context;
using namespace amoeba::reasoning;

class LocalLLMRuntimeTest : public ::testing::Test {
protected:
    ContextPackage create_dummy_context_package() {
        ContextPackage pkg;
        pkg.query = "useCalendar state";
        pkg.total_available_items = 1;
        pkg.selected_item_count = 1;
        pkg.rendered_markdown = "## Result 1: `useCalendar`\n- **Kind**: Hook\n- **File**: "
                                "src/hooks/useCalendar.ts\n\n```ts\nexport function useCalendar() "
                                "{ return {}; }\n```\n";
        pkg.used_characters = pkg.rendered_markdown.size();
        return pkg;
    }
};

// 1. LocalLLMConfig default initialization and mutation
TEST_F(LocalLLMRuntimeTest, ConfigurationDefaultsAndCustomization) {
    LocalLLMConfig config;
    EXPECT_EQ(config.endpoint, "http://127.0.0.1:11434");
    EXPECT_EQ(config.model_name, "qwen2.5-coder:1.5b");
    EXPECT_DOUBLE_EQ(config.temperature, 0.0);
    EXPECT_EQ(config.max_tokens, 1024u);
    EXPECT_EQ(config.timeout_ms, 60000u);

    config.endpoint = "http://127.0.0.1:8080";
    config.model_name = "llama3.2:1b";
    config.temperature = 0.2;
    config.max_tokens = 512;

    LocalLLMRuntime runtime(config);
    EXPECT_EQ(runtime.config().endpoint, "http://127.0.0.1:8080");
    EXPECT_EQ(runtime.config().model_name, "llama3.2:1b");
    EXPECT_DOUBLE_EQ(runtime.config().temperature, 0.2);
    EXPECT_EQ(runtime.config().max_tokens, 512u);
    EXPECT_EQ(runtime.runtime_name(), "local-llm-runtime");
}

// 2. LocalLLMRuntime move semantics
TEST_F(LocalLLMRuntimeTest, MoveSemanticsAndLifecycle) {
    LocalLLMConfig cfg;
    cfg.model_name = "custom-local-model";

    LocalLLMRuntime r1(cfg);
    LocalLLMRuntime r2(std::move(r1));

    EXPECT_EQ(r2.config().model_name, "custom-local-model");
    EXPECT_EQ(r2.runtime_name(), "local-llm-runtime");
}

// 3. Error handling when local server endpoint is unreachable
TEST_F(LocalLLMRuntimeTest, UnreachableEndpointErrorHandling) {
    // Point to an inactive loopback port (e.g. 59999)
    LocalLLMConfig cfg;
    cfg.endpoint = "http://127.0.0.1:59999";
    cfg.timeout_ms = 1000;  // 1s timeout

    LocalLLMRuntime runtime(cfg);
    ReasoningService service(runtime);

    const auto pkg = create_dummy_context_package();

    // Synchronous execution test
    const auto resp = service.answer("Where is useCalendar?", pkg);
    EXPECT_FALSE(resp.ok());
    EXPECT_FALSE(resp.success);
    EXPECT_EQ(resp.finish_reason, "error");
    EXPECT_NE(resp.error_message.find("Failed to connect"), std::string::npos);

    // Streaming execution test
    BufferingResponseSink sink;
    service.answer_stream("Where is useCalendar?", pkg, sink);
    EXPECT_TRUE(sink.has_error());
    EXPECT_FALSE(sink.is_completed());
    EXPECT_NE(sink.error_message().find("Failed to connect"), std::string::npos);
}

// 4. Endpoint reachable probe
TEST_F(LocalLLMRuntimeTest, EndpointReachableProbe) {
    LocalLLMConfig cfg;
    cfg.endpoint = "http://127.0.0.1:59998";  // Inactive port
    LocalLLMRuntime runtime(cfg);

    EXPECT_FALSE(runtime.is_endpoint_reachable());
}

}  // namespace
