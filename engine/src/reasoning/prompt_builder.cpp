#include "amoeba/reasoning/prompt_builder.hpp"

#include <sstream>

namespace amoeba::reasoning {

std::string PromptBuilder::build_system_prompt() {
    return "You are an expert code intelligence assistant for the Amoeba engine.\n"
           "Your task is to answer user questions about a codebase strictly and accurately using "
           "the provided Repository Evidence Context.\n\n"
           "GROUNDING RULES:\n"
           "1. Answer ONLY using facts directly supported by the supplied repository evidence.\n"
           "2. Do NOT invent, assume, or extrapolate files, symbols, functions, classes, variables, "
           "or relationships.\n"
           "3. Do NOT assume common framework behavior, authentication, persistence, or networking "
           "unless explicitly present in the evidence.\n"
           "4. Never fabricate a source location (file path or line range) or invent a relationship "
           "between symbols.\n"
           "5. Do NOT use general programming knowledge to fill in missing repository facts.\n"
           "6. Distinguish explicit evidence from reasonable explanation.\n"
           "7. If the supplied evidence does not support the requested claim, state clearly:\n"
           "   \"I couldn't establish this from the repository evidence available to Amoeba.\"\n\n"
           "RESPONSE FORMAT:\n"
           "Answer:\n"
           "<concise grounded explanation>\n\n"
           "Evidence:\n"
           "- <file path / symbol / location>";
}

std::string PromptBuilder::build_user_prompt(const std::string& question,
                                             const context::ContextPackage& context_package) {
    std::ostringstream ss;
    ss << "=== REPOSITORY EVIDENCE CONTEXT ===\n\n";
    if (context_package.rendered_markdown.empty()) {
        ss << "*No relevant code elements were retrieved.*\n\n";
    } else {
        ss << context_package.rendered_markdown << "\n\n";
    }

    ss << "=== USER QUESTION ===\n\n";
    ss << question << "\n";
    return ss.str();
}

std::string PromptBuilder::build_full_prompt(const std::string& question,
                                             const context::ContextPackage& context_package,
                                             const std::optional<std::string>& system_override) {
    std::ostringstream ss;
    ss << "=== SYSTEM INSTRUCTIONS ===\n\n";
    if (system_override.has_value() && !system_override->empty()) {
        ss << *system_override << "\n\n";
    } else {
        ss << build_system_prompt() << "\n\n";
    }

    ss << build_user_prompt(question, context_package);
    return ss.str();
}

}  // namespace amoeba::reasoning
