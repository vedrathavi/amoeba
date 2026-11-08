#pragma once

#include "amoeba/scanner/file_info.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::scanner {

using namespace std;
using namespace std::filesystem;

/**
 * @brief Categorized reasons why a discovered file or directory was skipped during scanning.
 */
enum class FileSkipReason : uint8_t {
    UnsupportedExtension,
    BinaryContent,
    AboveNormalThresholdDeferred,
    ExceedsHardLimit,
    GitIgnored,
    AmoebaIgnored,
    CustomExclusion,
    InternalSafety,
    FilesystemError
};

/**
 * @brief Detailed explainability record for a skipped repository item.
 */
struct SkippedFileInfo {
    path relative_path;
    uintmax_t file_size{0};
    FileSkipReason reason{FileSkipReason::UnsupportedExtension};
    string matching_rule{};
    string rule_source{};  ///< "Internal", "CLI", ".amoebaignore", ".gitignore", "Default"
};

/**
 * @brief User-configurable options controlling repository scanning behavior.
 */
struct ScanOptions {
    vector<string> custom_exclusions{};
    size_t normal_file_size_bytes{2 * 1024 * 1024};     ///< Tier 1: Normal indexing threshold (default 2MB)
    size_t hard_max_file_size_bytes{20 * 1024 * 1024};  ///< Tier 3: Hard safety ceiling (default 20MB)
    bool include_large_files{false};                    ///< Tier 2: Enable controlled indexing for (normal, hard]
    bool skip_binary_files{true};                       ///< Check for null bytes to reject binaries
    bool enable_git_ignore{true};                       ///< Enable libgit2-based .gitignore evaluation
    bool enable_amoeba_ignore{true};                    ///< Enable .amoebaignore evaluation
    bool collect_skipped_details{false};                ///< Record detailed SkippedFileInfo entries for explainability

    // Backward-compatibility shim for earlier phases
    size_t max_file_size_bytes{0};                      ///< If set > 0, overrides normal_file_size_bytes
};

/**
 * @brief Structured result of a repository scanning operation.
 */
struct ScanResult {
    path root_path;
    vector<FileInfo> files;
    size_t total_files_discovered{0};
    size_t total_files_ignored{0};
    size_t total_files_skipped_size{0};                 ///< Files exceeding hard safety limit
    size_t total_files_skipped_above_normal{0};         ///< Files deferred above normal threshold
    size_t total_files_skipped_binary{0};               ///< Files rejected due to binary content
    size_t total_files_git_ignored{0};                  ///< Files excluded by Git ignore
    size_t total_files_amoeba_ignored{0};               ///< Files excluded by .amoebaignore
    vector<SkippedFileInfo> skipped_details{};          ///< Populated when collect_skipped_details = true

    [[nodiscard]] size_t total_files_included() const noexcept { return files.size(); }
};

/**
 * @brief Traverses a repository, applying directory and file filters to discover source files.
 */
class RepositoryScanner {
public:
    RepositoryScanner() = default;

    /**
     * @brief Recursively scans the specified repository directory.
     * @param root_path Path to the root directory to scan.
     * @param options Configuration options for exclusions, size limits, and binary checks.
     * @throws invalid_argument if the path does not exist or is not a directory.
     * @return Structured ScanResult containing discovered and filtered source files.
     */
    [[nodiscard]] ScanResult scan(const path& root_path, const ScanOptions& options = {}) const;

    /**
     * @brief Checks if a directory name matches the default exclusion list.
     */
    [[nodiscard]] static bool is_excluded_directory(string_view dir_name) noexcept;

    /**
     * @brief Checks if a directory name matches the default exclusion list or custom exclusions.
     */
    [[nodiscard]] static bool is_excluded_directory(string_view dir_name,
                                                    span<const string> custom_exclusions) noexcept;

    /**
     * @brief Checks if a file extension matches the supported source code list.
     */
    [[nodiscard]] static bool is_supported_extension(string_view extension) noexcept;

    /**
     * @brief Returns the language name associated with a supported extension.
     */
    [[nodiscard]] static string_view get_language_name(string_view extension) noexcept;

    /**
     * @brief Converts a FileSkipReason to a readable string representation.
     */
    [[nodiscard]] static string_view skip_reason_to_string(FileSkipReason reason) noexcept;
};

}  // namespace amoeba::scanner
