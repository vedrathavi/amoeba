#pragma once

#include "amoeba/reasoning/llm_types.hpp"
#include "amoeba/reasoning/response_sink.hpp"

#include <string_view>

namespace amoeba::reasoning {

/**
 * @brief Abstract interface for LLM execution backends (Local models, API models, Mock models).
 *
 * Adheres to Dependency Inversion (DIP) and Open-Closed Principle (OCP):
 * ReasoningService depends solely on this interface, enabling transparent substitution
 * between local runtimes (llama.cpp, ONNX GenAI) and cloud/API providers without modifying
 * upstream retrieval, evidence, or context components.
 */
class LLMRuntime {
public:
    virtual ~LLMRuntime() = default;

    /**
     * @brief Execute generation synchronously and return the complete response.
     * @param request The generation request payload.
     * @return The complete LLM response.
     */
    [[nodiscard]] virtual LLMResponse generate(const LLMRequest& request) = 0;

    /**
     * @brief Execute generation asynchronously/streamingly, delivering events to a sink.
     * @param request The generation request payload.
     * @param sink The response sink receiving text chunks, completion, and error events.
     */
    virtual void generate_stream(const LLMRequest& request, ResponseSink& sink) = 0;

    /**
     * @brief Return a human-readable identifier for this runtime implementation.
     */
    [[nodiscard]] virtual std::string_view runtime_name() const noexcept = 0;
};

}  // namespace amoeba::reasoning
