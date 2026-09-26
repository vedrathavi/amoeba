#pragma once

#include "amoeba/context/context_package.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace amoeba::reasoning {

/**
 * @brief Constructs deterministic, grounded prompts for the LLM runtime.
 *
 * PromptBuilder enforces strict grounding instructions:
 * 1. Answering strictly using the provided repository evidence.
 * 2. Zero fabrication of missing files, symbols, or relationships.
 * 3. Explicit declaration when evidence is insufficient to answer the query.
 * 4. Clear separation between verbatim source facts and inferences.
 */
class PromptBuilder {
public:
    PromptBuilder() = delete;

    /**
     * @brief Builds the standard grounding system instructions.
     */
    [[nodiscard]] static std::string build_system_prompt();

    /**
     * @brief Builds the user prompt containing the bounded context package and question.
     * @param question The user's query.
     * @param context_package The bounded context package produced by Amoeba.
     */
    [[nodiscard]] static std::string
    build_user_prompt(const std::string& question, const context::ContextPackage& context_package);

    /**
     * @brief Combines the system instructions and user context into a single full prompt.
     * @param question The user's query.
     * @param context_package The bounded context package.
     * @param system_override Optional custom system prompt override.
     */
    [[nodiscard]] static std::string
    build_full_prompt(const std::string& question, const context::ContextPackage& context_package,
                      const std::optional<std::string>& system_override = std::nullopt);
};

}  // namespace amoeba::reasoning
