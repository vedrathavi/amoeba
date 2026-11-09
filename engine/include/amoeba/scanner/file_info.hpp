#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace amoeba::scanner {

/**
 * @brief Represents metadata for a discovered source code file.
 */
struct FileInfo {
    std::filesystem::path path;
    std::string extension;
    std::uintmax_t size{0};

    [[nodiscard]] bool operator==(const FileInfo& other) const = default;
};

}  // namespace amoeba::scanner
