#pragma once

#include "amoeba/reasoning/llm_runtime.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace amoeba::reasoning {

/**
 * @brief Deterministic fake LLM runtime for offline testing and verification.
 */
class FakeLLMRuntime final : public LLMRuntime {
public:
    FakeLLMRuntime() = default;
    explicit FakeLLMRuntime(std::string default_response);

    [[nodiscard]] LLMResponse generate(const LLMRequest& request) override;
    void generate_stream(const LLMRequest& request, ResponseSink& sink) override;
    [[nodiscard]] std::string_view runtime_name() const noexcept override;

    void set_canned_response(std::string response);
    void set_chunk_size(std::size_t chunk_size);
    void set_error(std::string error_message);
    void clear_error();

    [[nodiscard]] const std::vector<LLMRequest>& recorded_requests() const noexcept;
    [[nodiscard]] std::size_t invocation_count() const noexcept;
    void reset();

private:
    std::string default_response_{"Based on the provided repository evidence, the requested "
                                  "component is implemented as shown in the source context."};
    std::size_t chunk_size_{20};
    std::optional<std::string> simulated_error_{std::nullopt};
    std::vector<LLMRequest> recorded_requests_;
};

}  // namespace amoeba::reasoning
