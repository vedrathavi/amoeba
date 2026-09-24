#pragma once

#include "amoeba/scanner/file_info.hpp"

#include <filesystem>
#include <string_view>
#include <vector>

namespace amoeba::scanner {

using namespace std;
using namespace std::filesystem;

/**
 * @brief Structured result of a repository scanning operation.
 */
struct ScanResult {
    path root_path;
    vector<FileInfo> files;
    size_t total_files_discovered{0};
    size_t total_files_ignored{0};

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
     * @throws invalid_argument if the path does not exist or is not a directory.
     * @return Structured ScanResult containing discovered and filtered source files.
     */
    [[nodiscard]] ScanResult scan(const path& root_path) const;

    /**
     * @brief Checks if a directory name matches the default exclusion list.
     */
    [[nodiscard]] static bool is_excluded_directory(string_view dir_name) noexcept;

    /**
     * @brief Checks if a file extension matches the supported source code list.
     */
    [[nodiscard]] static bool is_supported_extension(string_view extension) noexcept;

    /**
     * @brief Returns the language name associated with a supported extension.
     */
    [[nodiscard]] static string_view get_language_name(string_view extension) noexcept;
};

}  // namespace amoeba::scanner
