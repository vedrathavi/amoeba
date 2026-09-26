#pragma once

#include "amoeba/reasoning/llm_runtime.hpp"
#include "amoeba/reasoning/llm_types.hpp"
#include "amoeba/reasoning/response_sink.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace amoeba::reasoning {

/**
 * @brief Configuration parameters for the local LLM inference runtime.
 */
struct LocalLLMConfig {
    /// Local inference endpoint (e.g. "http://127.0.0.1:11434" for Ollama or
    /// "http://127.0.0.1:8080" for llama-server/LM Studio).
    std::string endpoint{"http://127.0.0.1:11434"};

    /// Target local model name (e.g. "qwen2.5-coder:1.5b", "llama3.2:1b", "deepseek-coder:1.3b").
    std::string model_name{"qwen2.5-coder:1.5b"};

    /// Optional path to local GGUF/model weights file.
    std::string model_path{""};

    /// Sampling temperature (0.0 for deterministic factual code reasoning).
    double temperature{0.0};

    /// Maximum tokens to generate (default 1024).
    std::size_t max_tokens{1024};

    /// Request timeout in milliseconds (default 60000 = 60s).
    uint32_t timeout_ms{60000};

    bool operator==(const LocalLLMConfig& other) const = default;
};

/**
 * @brief Concrete local LLM runtime connecting Amoeba to local models.
 *
 * Implements the LLMRuntime abstraction. Communicates with local runners
 * (such as Ollama, llama-server, LocalAI, or LM Studio) over local loopback HTTP
 * with real-time SSE / chunked token streaming.
 */
class LocalLLMRuntime final : public LLMRuntime {
public:
    LocalLLMRuntime();
    explicit LocalLLMRuntime(LocalLLMConfig config);
    ~LocalLLMRuntime() override;

    LocalLLMRuntime(const LocalLLMRuntime&) = delete;
    LocalLLMRuntime& operator=(const LocalLLMRuntime&) = delete;
    LocalLLMRuntime(LocalLLMRuntime&&) noexcept;
    LocalLLMRuntime& operator=(LocalLLMRuntime&&) noexcept;

    [[nodiscard]] LLMResponse generate(const LLMRequest& request) override;
    void generate_stream(const LLMRequest& request, ResponseSink& sink) override;
    [[nodiscard]] std::string_view runtime_name() const noexcept override;

    [[nodiscard]] const LocalLLMConfig& config() const noexcept;
    void set_config(LocalLLMConfig config);

    /**
     * @brief Checks whether the local inference endpoint is currently reachable.
     */
    [[nodiscard]] bool is_endpoint_reachable() const noexcept;

private:
    LocalLLMConfig config_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace amoeba::reasoning
