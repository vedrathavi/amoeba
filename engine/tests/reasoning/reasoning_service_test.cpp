#include "amoeba/reasoning/fake_llm_runtime.hpp"
#include "amoeba/reasoning/llm_runtime.hpp"
#include "amoeba/reasoning/llm_types.hpp"
#include "amoeba/reasoning/prompt_builder.hpp"
#include "amoeba/reasoning/reasoning_service.hpp"
#include "amoeba/reasoning/response_sink.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

using namespace amoeba;
using namespace amoeba::context;
using namespace amoeba::reasoning;

class ReasoningServiceTest : public ::testing::Test {
protected:
    ContextPackage create_dummy_context_package(const std::string& query = "CalendarGrid") {
        ContextPackage pkg;
        pkg.query = query;
        pkg.total_available_items = 1;
        pkg.selected_item_count = 1;
        pkg.rendered_markdown =
            "## Result 1: `CalendarGrid`\n- **Kind**: Component\n- **File**: "
            "src/CalendarGrid.tsx\n\n```tsx\nexport function CalendarGrid() {}\n```\n";
        pkg.used_characters = pkg.rendered_markdown.size();
        return pkg;
    }
};

// 1. LLMRequest and LLMResponse value types
TEST_F(ReasoningServiceTest, LLMRequestAndResponseValueTypes) {
    LLMRequest req;
    req.user_question = "Where is CalendarGrid defined?";
    req.context_package = create_dummy_context_package();
    req.temperature = 0.0;

    EXPECT_EQ(req.user_question, "Where is CalendarGrid defined?");
    EXPECT_EQ(req.context_package.query, "CalendarGrid");
    EXPECT_DOUBLE_EQ(req.temperature, 0.0);

    LLMResponse resp;
    resp.content = "CalendarGrid is defined in src/CalendarGrid.tsx.";
    resp.model_name = "test-model";
    resp.success = true;

    EXPECT_TRUE(resp.ok());
    EXPECT_EQ(resp.content, "CalendarGrid is defined in src/CalendarGrid.tsx.");
    EXPECT_EQ(resp.finish_reason, "stop");

    LLMResponse err_resp;
    err_resp.success = false;
    err_resp.error_message = "Connection timeout";
    err_resp.finish_reason = "error";

    EXPECT_FALSE(err_resp.ok());
    EXPECT_EQ(err_resp.error_message, "Connection timeout");
}

// 2. PromptBuilder grounding and insufficient evidence instructions
TEST_F(ReasoningServiceTest, PromptBuilderGroundingAndInstructions) {
    const std::string system_prompt = PromptBuilder::build_system_prompt();

    // Verify critical grounding rules
    EXPECT_NE(system_prompt.find("GROUNDING RULES"), std::string::npos);
    EXPECT_NE(system_prompt.find("Answer ONLY using facts"), std::string::npos);
    EXPECT_NE(system_prompt.find("Do NOT invent"), std::string::npos);
    EXPECT_NE(system_prompt.find(
                  "I couldn't establish this from the repository evidence available to Amoeba"),
              std::string::npos);

    // Verify user prompt contains markdown context and query
    const auto pkg = create_dummy_context_package();
    const std::string user_prompt =
        PromptBuilder::build_user_prompt("Where is CalendarGrid defined?", pkg);

    EXPECT_NE(user_prompt.find("=== REPOSITORY EVIDENCE CONTEXT ==="), std::string::npos);
    EXPECT_NE(user_prompt.find("Result 1: `CalendarGrid`"), std::string::npos);
    EXPECT_NE(user_prompt.find("=== USER QUESTION ==="), std::string::npos);
    EXPECT_NE(user_prompt.find("Where is CalendarGrid defined?"), std::string::npos);

    // Verify full prompt combinations
    const std::string full_prompt =
        PromptBuilder::build_full_prompt("Where is CalendarGrid defined?", pkg);
    EXPECT_NE(full_prompt.find("=== SYSTEM INSTRUCTIONS ==="), std::string::npos);
    EXPECT_NE(full_prompt.find("=== REPOSITORY EVIDENCE CONTEXT ==="), std::string::npos);

    // Custom system prompt override
    const std::string custom_full = PromptBuilder::build_full_prompt(
        "Where is CalendarGrid defined?", pkg, "Custom system rules.");
    EXPECT_NE(custom_full.find("Custom system rules."), std::string::npos);
}

// 3. PromptBuilder determinism
TEST_F(ReasoningServiceTest, PromptBuilderDeterminism) {
    const auto pkg = create_dummy_context_package();
    const std::string p1 = PromptBuilder::build_full_prompt("test question", pkg);
    const std::string p2 = PromptBuilder::build_full_prompt("test question", pkg);

    EXPECT_EQ(p1, p2);
}

// 4. ReasoningService synchronous execution with FakeLLMRuntime
TEST_F(ReasoningServiceTest, ReasoningServiceSynchronousExecution) {
    FakeLLMRuntime runtime("CalendarGrid is a 7-column monthly grid component.");
    ReasoningService service(runtime);

    const auto pkg = create_dummy_context_package();
    const auto response = service.answer("What is CalendarGrid?", pkg);

    EXPECT_TRUE(response.ok());
    EXPECT_EQ(response.content, "CalendarGrid is a 7-column monthly grid component.");
    EXPECT_EQ(response.model_name, "fake-llm-runtime");
    EXPECT_EQ(runtime.invocation_count(), 1u);
    EXPECT_EQ(runtime.recorded_requests()[0].user_question, "What is CalendarGrid?");
}

// 5. ReasoningService streaming execution with BufferingResponseSink
TEST_F(ReasoningServiceTest, ReasoningServiceStreamingExecution) {
    FakeLLMRuntime runtime("Line 1: Calendar state is initialized via useCalendar.");
    runtime.set_chunk_size(10);  // Chunk into 10-byte pieces
    ReasoningService service(runtime);

    const auto pkg = create_dummy_context_package();
    BufferingResponseSink sink;

    service.answer_stream("How is calendar state initialized?", pkg, sink);

    EXPECT_TRUE(sink.is_completed());
    EXPECT_FALSE(sink.has_error());
    EXPECT_EQ(sink.text(), "Line 1: Calendar state is initialized via useCalendar.");
    EXPECT_GT(sink.events().size(), 1u);

    // Verify last event is Completed
    EXPECT_EQ(sink.events().back().type, ReasoningEventType::Completed);
}

// 6. CallbackResponseSink verification
TEST_F(ReasoningServiceTest, CallbackResponseSinkEventFlow) {
    FakeLLMRuntime runtime("Streamed output test.");
    runtime.set_chunk_size(5);
    ReasoningService service(runtime);

    std::vector<ReasoningEvent> received_events;
    CallbackResponseSink sink([&](const ReasoningEvent& ev) { received_events.push_back(ev); });

    const auto pkg = create_dummy_context_package();
    service.answer_stream("test stream", pkg, sink);

    ASSERT_FALSE(received_events.empty());
    EXPECT_EQ(received_events.back().type, ReasoningEventType::Completed);

    std::string assembled;
    for (const auto& ev : received_events) {
        if (ev.type == ReasoningEventType::TextChunk) {
            assembled += ev.text_chunk;
        }
    }
    EXPECT_EQ(assembled, "Streamed output test.");
}

// 7. Error propagation in synchronous mode
TEST_F(ReasoningServiceTest, SynchronousErrorPropagation) {
    FakeLLMRuntime runtime;
    runtime.set_error("API rate limit exceeded");
    ReasoningService service(runtime);

    const auto pkg = create_dummy_context_package();
    const auto response = service.answer("test query", pkg);

    EXPECT_FALSE(response.ok());
    EXPECT_FALSE(response.success);
    EXPECT_EQ(response.error_message, "API rate limit exceeded");
    EXPECT_EQ(response.finish_reason, "error");
}

// 8. Error propagation in streaming mode
TEST_F(ReasoningServiceTest, StreamingErrorPropagation) {
    FakeLLMRuntime runtime;
    runtime.set_error("Backend model unavailable");
    ReasoningService service(runtime);

    const auto pkg = create_dummy_context_package();
    BufferingResponseSink sink;

    service.answer_stream("test query", pkg, sink);

    EXPECT_TRUE(sink.has_error());
    EXPECT_FALSE(sink.is_completed());
    EXPECT_EQ(sink.error_message(), "Backend model unavailable");
    ASSERT_EQ(sink.events().size(), 1u);
    EXPECT_EQ(sink.events().front().type, ReasoningEventType::Error);
}

// 9. Runtime substitution (DIP / LSP)
TEST_F(ReasoningServiceTest, RuntimeSubstitutionSupport) {
    class CustomMockRuntime : public LLMRuntime {
    public:
        LLMResponse generate(const LLMRequest& req) override {
            return LLMResponse{
                .content = "Custom answer to: " + req.user_question,
                .model_name = "custom-mock",
                .success = true,
                .error_message = "",
                .finish_reason = "stop",
            };
        }

        void generate_stream(const LLMRequest& req, ResponseSink& sink) override {
            sink.on_event(ReasoningEvent::text("Custom streamed: " + req.user_question));
            sink.on_event(ReasoningEvent::completed());
        }

        std::string_view runtime_name() const noexcept override { return "custom-mock"; }
    };

    CustomMockRuntime custom_runtime;
    ReasoningService service(custom_runtime);

    EXPECT_EQ(service.runtime().runtime_name(), "custom-mock");

    const auto pkg = create_dummy_context_package();
    const auto resp = service.answer("Hello Amoeba", pkg);
    EXPECT_EQ(resp.content, "Custom answer to: Hello Amoeba");
}

// 10. Empty ContextPackage Handling
TEST_F(ReasoningServiceTest, EmptyContextPackageHandling) {
    FakeLLMRuntime runtime(
        "I couldn't find sufficient evidence in the repository to determine this.");
    ReasoningService service(runtime);

    ContextPackage empty_pkg;
    empty_pkg.query = "Where is DatabaseConnection defined?";

    const auto resp = service.answer("Where is DatabaseConnection defined?", empty_pkg);
    EXPECT_TRUE(resp.ok());
    EXPECT_EQ(resp.content,
              "I couldn't find sufficient evidence in the repository to determine this.");
}

}  // namespace
