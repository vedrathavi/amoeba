#include "amoeba/tools/repository_access_policy.hpp"

#include <algorithm>
#include <cctype>

namespace amoeba::tools {

namespace {

[[nodiscard]] std::string to_lower_string(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

[[nodiscard]] bool starts_with_case_insensitive(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ends_with_case_insensitive(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) {
        return false;
    }
    const std::size_t offset = s.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[offset + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

RepositoryAccessPolicy::RepositoryAccessPolicy() {
    sensitive_filenames_ = {
        ".env",            ".env.local",      ".env.development", ".env.test",
        ".env.production", ".env.staging",    ".env.ci",          "id_rsa",
        "id_rsa.pub",      "id_ed25519",      "id_ed25519.pub",   "id_dsa",
        "id_ecdsa",        "credentials",     "credentials.json", "secrets",
        "secrets.json",    "secrets.yaml",    "secrets.yml",      ".npmrc",
        ".pypirc",         ".htpasswd",       ".netrc",           "authorized_keys",
        "known_hosts",     "master.key",      "secret_key_base",  "jwt_secret"};

    sensitive_directories_ = {".git",       ".ssh",       ".gnupg",      "secrets",
                              "secret",     "credentials",".aws",        ".kube",
                              ".docker",    ".vault",     "certificates"};

    sensitive_extensions_ = {".pem", ".key", ".p12", ".pfx", ".kdbx", ".keystore", ".jks", ".ovpn"};
}

bool RepositoryAccessPolicy::is_sensitive_file(const std::filesystem::path& path) const noexcept {
    const std::string filename = to_lower_string(path.filename().string());
    if (filename.empty()) {
        return false;
    }

    if (sensitive_filenames_.contains(filename)) {
        return true;
    }

    if (starts_with_case_insensitive(filename, ".env.") ||
        starts_with_case_insensitive(filename, "id_rsa") ||
        starts_with_case_insensitive(filename, "id_ed25519")) {
        return true;
    }

    for (const auto& ext : sensitive_extensions_) {
        if (ends_with_case_insensitive(filename, ext)) {
            return true;
        }
    }

    for (const auto& custom : custom_patterns_) {
        if (filename.find(custom) != std::string::npos) {
            return true;
        }
    }

    return false;
}

bool RepositoryAccessPolicy::is_sensitive_directory(
    const std::filesystem::path& path) const noexcept {
    for (const auto& part : path) {
        const std::string part_str = to_lower_string(part.string());
        if (sensitive_directories_.contains(part_str)) {
            return true;
        }
    }
    return false;
}

void RepositoryAccessPolicy::add_sensitive_pattern(std::string pattern) {
    if (!pattern.empty()) {
        custom_patterns_.push_back(to_lower_string(pattern));
    }
}

AccessDecision
RepositoryAccessPolicy::check_read(const std::filesystem::path& repository_root,
                                   const std::filesystem::path& requested_path) const {
    if (requested_path.empty() || repository_root.empty()) {
        return AccessDecision::InvalidPath;
    }

    // 1. Absolute paths are strictly rejected to prevent arbitrary filesystem access
    if (requested_path.is_absolute() || requested_path.has_root_name() ||
        requested_path.has_root_directory()) {
        return AccessDecision::PathOutsideRepository;
    }

    // 2. Traversal component check in raw path string before and after normalization
    const std::string raw_str = requested_path.generic_string();
    if (raw_str.find("..") != std::string::npos) {
        // Detailed check for parent directory escape
        auto rel_normal = requested_path.lexically_normal();
        if (rel_normal.empty() || rel_normal.string().starts_with("..")) {
            return AccessDecision::PathOutsideRepository;
        }
    }

    // 3. Resolve combined path and verify containment
    const auto norm_root = repository_root.lexically_normal();
    const auto combined = (norm_root / requested_path).lexically_normal();

    const auto root_str = norm_root.generic_string();
    const auto combined_str = combined.generic_string();

    // Combined path MUST begin with repository root path
    if (combined_str.size() < root_str.size() ||
        !combined_str.starts_with(root_str)) {
        return AccessDecision::PathOutsideRepository;
    }

    if (combined_str.size() > root_str.size() && combined_str[root_str.size()] != '/') {
        return AccessDecision::PathOutsideRepository;
    }

    // 4. Sensitive file and directory checks
    if (is_sensitive_file(requested_path) || is_sensitive_file(combined)) {
        return AccessDecision::SensitiveFile;
    }

    if (is_sensitive_directory(requested_path) || is_sensitive_directory(combined)) {
        return AccessDecision::SensitiveFile;
    }

    return AccessDecision::Allowed;
}

std::optional<std::filesystem::path>
RepositoryAccessPolicy::resolve_safe_path(const std::filesystem::path& repository_root,
                                          const std::filesystem::path& requested_path) const {
    if (check_read(repository_root, requested_path) != AccessDecision::Allowed) {
        return std::nullopt;
    }
    const auto norm_root = repository_root.lexically_normal();
    const auto combined = (norm_root / requested_path).lexically_normal();

    // Reparse point / Symlink / Junction safety:
    // If the path exists on disk, ensure its canonical target does not escape canonical repository root.
    std::error_code ec_target;
    if (std::filesystem::exists(combined, ec_target) && !ec_target) {
        std::error_code ec_can_target, ec_can_root;
        const auto can_target = std::filesystem::canonical(combined, ec_can_target).generic_string();
        const auto can_root = std::filesystem::canonical(norm_root, ec_can_root).generic_string();
        if (!ec_can_target && !ec_can_root) {
            if (can_target.size() < can_root.size() || !can_target.starts_with(can_root)) {
                return std::nullopt;
            }
            if (can_target.size() > can_root.size() && can_target[can_root.size()] != '/') {
                return std::nullopt;
            }
        }
    }

    return combined;
}

}  // namespace amoeba::tools
