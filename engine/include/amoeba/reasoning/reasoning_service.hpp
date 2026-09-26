#pragma once

#include "amoeba/context/context_package.hpp"
#include "amoeba/reasoning/llm_runtime.hpp"
#include "amoeba/reasoning/llm_types.hpp"
#include "amoeba/reasoning/response_sink.hpp"

namespace amoeba::reasoning {

/**
 * @brief Orchestrates reasoning queries over Amoeba's ContextPackage using an LLMRuntime.
 *
 * Responsibilities:
 * - Accepts user question and ContextPackage.
 * - Constructs the appropriate grounded prompt payload.
 * - Dispatches execution to the configured LLMRuntime (synchronously or streamingly).
 * - Remains strictly decoupled from retrieval, graph, and AST parser internals.
 */
class ReasoningService {
public:
    /**
     * @brief Construct ReasoningService with a concrete LLMRuntime.
     * @param runtime The runtime implementation to use for generation.
     */
    explicit ReasoningService(LLMRuntime& runtime);

    ~ReasoningService() = default;

    /**
     * @brief Generate a synchronous answer for a question given a ContextPackage.
     * @param question The user's query.
     * @param context_package The bounded repository context.
     * @return Complete LLM response.
     */
    [[nodiscard]] LLMResponse answer(const std::string& question,
                                     const context::ContextPackage& context_package);

    /**
     * @brief Generate a synchronous answer for a pre-built LLMRequest.
     * @param request The full reasoning request.
     * @return Complete LLM response.
     */
    [[nodiscard]] LLMResponse answer(const LLMRequest& request);

    /**
     * @brief Stream response events for a question given a ContextPackage.
     * @param question The user's query.
     * @param context_package The bounded repository context.
     * @param sink The response sink receiving text chunks and completion.
     */
    void answer_stream(const std::string& question, const context::ContextPackage& context_package,
                       ResponseSink& sink);

    /**
     * @brief Stream response events for an LLMRequest.
     * @param request The full reasoning request.
     * @param sink The response sink receiving text chunks and completion.
     */
    void answer_stream(const LLMRequest& request, ResponseSink& sink);

    /**
     * @brief Access the underlying runtime reference.
     */
    [[nodiscard]] LLMRuntime& runtime() noexcept { return runtime_; }

    [[nodiscard]] const LLMRuntime& runtime() const noexcept { return runtime_; }

private:
    LLMRuntime& runtime_;
};

}  // namespace amoeba::reasoning
