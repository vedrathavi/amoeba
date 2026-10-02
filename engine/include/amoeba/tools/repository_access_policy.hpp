#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace amoeba::tools {

/**
 * @brief Authorization decision outcome from RepositoryAccessPolicy.
 */
enum class AccessDecision : uint8_t {
    Allowed,
    PathOutsideRepository,
    SensitiveFile,
    InvalidPath
};

[[nodiscard]] constexpr std::string_view to_string(AccessDecision decision) noexcept {
    switch (decision) {
    case AccessDecision::Allowed:
        return "ALLOWED";
    case AccessDecision::PathOutsideRepository:
        return "PATH_OUTSIDE_REPOSITORY";
    case AccessDecision::SensitiveFile:
        return "SENSITIVE_FILE";
    case AccessDecision::InvalidPath:
        return "INVALID_PATH";
    }
    return "UNKNOWN";
}

/**
 * @brief Centralized, deterministic security policy enforcing repository boundaries
 * and preventing unauthorized access to sensitive files and directory escapes.
 */
class RepositoryAccessPolicy {
public:
    RepositoryAccessPolicy();
    ~RepositoryAccessPolicy() = default;

    /**
     * @brief Checks if a requested path is permitted to be read from the given repository root.
     *
     * @param repository_root The canonical or valid base path of the repository.
     * @param requested_path The path requested by a tool or reasoning caller (must be repo-relative).
     * @return AccessDecision indicating whether access is allowed or reason for denial.
     */
    [[nodiscard]] AccessDecision check_read(const std::filesystem::path& repository_root,
                                            const std::filesystem::path& requested_path) const;

    /**
     * @brief Resolves a requested relative path to a safe, normalized absolute path strictly
     * inside repository_root if allowed, or std::nullopt if denied.
     */
    [[nodiscard]] std::optional<std::filesystem::path>
    resolve_safe_path(const std::filesystem::path& repository_root,
                      const std::filesystem::path& requested_path) const;

    /**
     * @brief Checks if a given filename or path matches known sensitive secrets/key patterns.
     */
    [[nodiscard]] bool is_sensitive_file(const std::filesystem::path& path) const noexcept;

    /**
     * @brief Checks if a given path contains sensitive directory components (e.g. .git, .ssh).
     */
    [[nodiscard]] bool is_sensitive_directory(const std::filesystem::path& path) const noexcept;

    /**
     * @brief Register custom sensitive pattern (extensibility hook).
     */
    void add_sensitive_pattern(std::string pattern);

private:
    std::unordered_set<std::string> sensitive_filenames_;
    std::unordered_set<std::string> sensitive_directories_;
    std::vector<std::string> sensitive_extensions_;
    std::vector<std::string> custom_patterns_;
};

}  // namespace amoeba::tools
