#include "amoeba/reasoning/fake_llm_runtime.hpp"

#include <algorithm>

namespace amoeba::reasoning {

FakeLLMRuntime::FakeLLMRuntime(std::string default_response)
    : default_response_(std::move(default_response)) {}

LLMResponse FakeLLMRuntime::generate(const LLMRequest& request) {
    recorded_requests_.push_back(request);

    if (simulated_error_.has_value()) {
        return LLMResponse{
            .content = "",
            .model_name = "fake-llm-runtime",
            .success = false,
            .error_message = *simulated_error_,
            .finish_reason = "error",
        };
    }

    return LLMResponse{
        .content = default_response_,
        .model_name = "fake-llm-runtime",
        .success = true,
        .error_message = "",
        .finish_reason = "stop",
    };
}

void FakeLLMRuntime::generate_stream(const LLMRequest& request, ResponseSink& sink) {
    recorded_requests_.push_back(request);

    if (simulated_error_.has_value()) {
        sink.on_event(ReasoningEvent::error(*simulated_error_));
        return;
    }

    if (default_response_.empty()) {
        sink.on_event(ReasoningEvent::completed(""));
        return;
    }

    const std::size_t chunk_len = std::max<std::size_t>(1, chunk_size_);
    for (std::size_t i = 0; i < default_response_.size(); i += chunk_len) {
        const std::string chunk = default_response_.substr(i, chunk_len);
        sink.on_event(ReasoningEvent::text(chunk));
    }

    sink.on_event(ReasoningEvent::completed(default_response_));
}

std::string_view FakeLLMRuntime::runtime_name() const noexcept {
    return "fake-llm-runtime";
}

void FakeLLMRuntime::set_canned_response(std::string response) {
    default_response_ = std::move(response);
}

void FakeLLMRuntime::set_chunk_size(std::size_t chunk_size) {
    chunk_size_ = chunk_size;
}

void FakeLLMRuntime::set_error(std::string error_message) {
    simulated_error_ = std::move(error_message);
}

void FakeLLMRuntime::clear_error() {
    simulated_error_ = std::nullopt;
}

const std::vector<LLMRequest>& FakeLLMRuntime::recorded_requests() const noexcept {
    return recorded_requests_;
}

std::size_t FakeLLMRuntime::invocation_count() const noexcept {
    return recorded_requests_.size();
}

void FakeLLMRuntime::reset() {
    recorded_requests_.clear();
    simulated_error_ = std::nullopt;
}

}  // namespace amoeba::reasoning
