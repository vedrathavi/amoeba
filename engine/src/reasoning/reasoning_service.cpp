#include "amoeba/reasoning/reasoning_service.hpp"

namespace amoeba::reasoning {

ReasoningService::ReasoningService(LLMRuntime& runtime) : runtime_(runtime) {}

LLMResponse ReasoningService::answer(const std::string& question,
                                     const context::ContextPackage& context_package) {
    const LLMRequest req{
        .user_question = question,
        .context_package = context_package,
    };
    return runtime_.generate(req);
}

LLMResponse ReasoningService::answer(const LLMRequest& request) {
    return runtime_.generate(request);
}

void ReasoningService::answer_stream(const std::string& question,
                                     const context::ContextPackage& context_package,
                                     ResponseSink& sink) {
    const LLMRequest req{
        .user_question = question,
        .context_package = context_package,
    };
    runtime_.generate_stream(req, sink);
}

void ReasoningService::answer_stream(const LLMRequest& request, ResponseSink& sink) {
    runtime_.generate_stream(request, sink);
}

}  // namespace amoeba::reasoning
