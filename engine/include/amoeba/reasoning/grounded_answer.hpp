#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::reasoning {

/**
 * @brief Formal verification status of an Amoeba reasoning answer.
 */
enum class GroundedAnswerStatus : uint8_t {
    Grounded,              ///< Evidence sufficiently and authoritatively supports the answer
    InsufficientEvidence,  ///< Evidence cannot establish requested facts from repository code
    Error                  ///< Reasoning or inference runtime error occurred
};

[[nodiscard]] constexpr std::string_view to_string(GroundedAnswerStatus status) noexcept {
    switch (status) {
    case GroundedAnswerStatus::Grounded:
        return "GROUNDED";
    case GroundedAnswerStatus::InsufficientEvidence:
        return "INSUFFICIENT_EVIDENCE";
    case GroundedAnswerStatus::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

/**
 * @brief Bounded code citation referencing verified repository evidence.
 */
struct GroundedCitation {
    std::string file_path;
    std::string symbol_name;
    std::string range_description;
    std::string element_kind;

    bool operator==(const GroundedCitation&) const = default;
};

/**
 * @brief Structured contract for answers produced by Amoeba's reasoning layer.
 */
struct GroundedAnswer {
    std::string user_question;
    std::string answer_text;
    GroundedAnswerStatus status{GroundedAnswerStatus::Grounded};
    std::vector<GroundedCitation> citations;
    std::string refusal_reason;
    std::string model_name;

    [[nodiscard]] bool is_grounded() const noexcept {
        return status == GroundedAnswerStatus::Grounded;
    }

    [[nodiscard]] bool is_insufficient() const noexcept {
        return status == GroundedAnswerStatus::InsufficientEvidence;
    }

    [[nodiscard]] std::string format() const {
        std::ostringstream ss;
        if (status == GroundedAnswerStatus::InsufficientEvidence) {
            ss << "Answer:\n"
               << (answer_text.empty()
                       ? "I couldn't establish this from the repository evidence available to Amoeba."
                       : answer_text)
               << "\n\n";
            if (!refusal_reason.empty()) {
                ss << "Evidence:\n" << refusal_reason << "\n";
            }
        } else if (status == GroundedAnswerStatus::Error) {
            ss << "[Error]\n" << answer_text << "\n";
        } else {
            ss << "Answer:\n" << answer_text << "\n";
            if (!citations.empty()) {
                ss << "\nEvidence:\n";
                for (const auto& c : citations) {
                    ss << "- " << c.file_path;
                    if (!c.symbol_name.empty()) {
                        ss << " (" << c.symbol_name << ")";
                    }
                    if (!c.range_description.empty()) {
                        ss << " [" << c.range_description << "]";
                    }
                    ss << "\n";
                }
            }
        }
        return ss.str();
    }

    bool operator==(const GroundedAnswer&) const = default;
};

}  // namespace amoeba::reasoning
