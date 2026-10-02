#pragma once

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace amoeba::tools {

/**
 * @brief Structured execution status for repository tool invocations.
 */
enum class ToolResultStatus : uint8_t {
    Success,           ///< Tool executed successfully and produced valid output
    InvalidRequest,    ///< Request arguments were missing, malformed, or out of range
    PermissionDenied,  ///< Path or operation violated RepositoryAccessPolicy / security boundary
    NotFound,          ///< Requested symbol, file, or entity does not exist in repository
    Error              ///< Internal tool or engine execution failure
};

[[nodiscard]] constexpr std::string_view to_string(ToolResultStatus status) noexcept {
    switch (status) {
    case ToolResultStatus::Success:
        return "SUCCESS";
    case ToolResultStatus::InvalidRequest:
        return "INVALID_REQUEST";
    case ToolResultStatus::PermissionDenied:
        return "PERMISSION_DENIED";
    case ToolResultStatus::NotFound:
        return "NOT_FOUND";
    case ToolResultStatus::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

/**
 * @brief Value type representing an immutable tool execution request.
 */
struct ToolRequest {
    std::string tool_name;
    std::unordered_map<std::string, std::string> arguments;

    [[nodiscard]] std::optional<std::string_view> get_arg(std::string_view key) const noexcept {
        const auto it = arguments.find(std::string(key));
        if (it != arguments.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string get_arg_or(std::string_view key, std::string_view default_val) const {
        const auto val = get_arg(key);
        if (val.has_value()) {
            return std::string(*val);
        }
        return std::string(default_val);
    }

    bool operator==(const ToolRequest&) const = default;
};

/**
 * @brief Formal parameter metadata for a repository tool.
 */
struct ToolParameter {
    std::string name;
    std::string type{"string"};  // "string", "integer", "boolean"
    std::string description;
    bool required{false};
    std::string default_value;

    bool operator==(const ToolParameter&) const = default;
};

/**
 * @brief Self-describing schema metadata exposed by a repository tool.
 */
struct ToolDescription {
    std::string name;
    std::string description;
    std::vector<ToolParameter> parameters;

    bool operator==(const ToolDescription&) const = default;
};

/**
 * @brief Structured, deterministic result returned by repository tool execution.
 */
struct ToolResult {
    ToolResultStatus status{ToolResultStatus::Success};
    std::string tool_name;
    std::string summary;
    std::string content;
    std::string error_code;  // e.g. "PATH_OUTSIDE_REPOSITORY", "SENSITIVE_FILE", "SYMBOL_NOT_FOUND"
    std::vector<std::pair<std::string, std::string>> metadata;

    [[nodiscard]] bool ok() const noexcept { return status == ToolResultStatus::Success; }

    [[nodiscard]] std::string format() const {
        std::ostringstream ss;
        ss << "[" << to_string(status) << "] Tool: " << tool_name << "\n";
        if (!summary.empty()) {
            ss << "Summary: " << summary << "\n";
        }
        if (!error_code.empty()) {
            ss << "Error Code: " << error_code << "\n";
        }
        if (!content.empty()) {
            ss << "\n" << content << "\n";
        }
        return ss.str();
    }

    bool operator==(const ToolResult&) const = default;
};

}  // namespace amoeba::tools
