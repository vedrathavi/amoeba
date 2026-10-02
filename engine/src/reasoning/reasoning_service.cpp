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

GroundedAnswer ReasoningService::answer_grounded(
    const std::string& question, const context::ContextPackage& context_package,
    const std::optional<evidence::EvidenceSufficiencyResult>& sufficiency) {

    GroundedAnswer result;
    result.user_question = question;

    // Fast-path refusal if evidence sufficiency check failed (zero LLM token consumption)
    if (sufficiency.has_value() && !sufficiency->is_sufficient) {
        result.status = GroundedAnswerStatus::InsufficientEvidence;
        result.answer_text =
            "I couldn't establish this from the repository evidence available to Amoeba.";
        result.refusal_reason = sufficiency->format_grounded_refusal(question);
        result.model_name = "amoeba::sufficiency_gate";
        return result;
    }

    const LLMRequest req{
        .user_question = question,
        .context_package = context_package,
    };
    const LLMResponse resp = runtime_.generate(req);

    result.model_name = resp.model_name;

    if (!resp.ok()) {
        result.status = GroundedAnswerStatus::Error;
        result.answer_text = resp.error_message.empty() ? "LLM inference failure" : resp.error_message;
        return result;
    }

    result.status = GroundedAnswerStatus::Grounded;
    result.answer_text = resp.content;

    // Populate structured citations from verified context package
    for (const auto& item : context_package.selected_items) {
        const auto& elem = item.primary_element();
        GroundedCitation citation;
        citation.file_path = item.file_path().generic_string();
        citation.symbol_name = elem.name.empty() ? item.file_path().filename().string() : elem.name;
        if (elem.location.start.line > 0) {
            citation.range_description = "L" + std::to_string(elem.location.start.line) + "-L" +
                                         std::to_string(elem.location.end.line);
        }
        citation.element_kind = parser::to_string(elem.kind);
        result.citations.push_back(std::move(citation));
    }

    return result;
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
