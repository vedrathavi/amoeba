#include "amoeba/reasoning/prompt_builder.hpp"

#include <sstream>

namespace amoeba::reasoning {

std::string PromptBuilder::build_system_prompt() {
    return "You are an expert code intelligence assistant for the Amoeba engine.\n"
           "Your task is to answer user questions about a codebase strictly and accurately using "
           "the provided Repository Evidence Context.\n\n"
           "GROUNDING RULES:\n"
           "1. Base all explanations, symbols, file paths, line numbers, and relationships ONLY on "
           "the supplied repository evidence.\n"
           "2. Retrieved candidates in the context are not automatically relevant evidence. "
           "Inspect "
           "them critically.\n"
           "3. Do NOT invent, assume, or extrapolate functions, classes, files, imports, "
           "technologies, "
           "or behaviors not explicitly shown in the evidence.\n"
           "4. Do NOT infer authentication, persistence, networking, or other mechanisms unless "
           "the supplied code explicitly implements them.\n"
           "5. If the provided evidence is insufficient to fully answer the question, or if the "
           "requested concept is absent from the code, state clearly: "
           "\"I couldn't find sufficient evidence in the repository to determine this.\"\n"
           "6. Never fill in missing repository information using general programming assumptions "
           "or "
           "unrelated code elements.\n"
           "7. Always cite specific file paths and symbol names from the context when explaining "
           "code structure.";
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
