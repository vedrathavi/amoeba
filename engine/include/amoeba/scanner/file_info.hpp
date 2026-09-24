#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace amoeba::scanner {

using namespace std;
using namespace std::filesystem;

/**
 * @brief Represents metadata for a discovered source code file.
 */
struct FileInfo {
    path path;
    string extension;
    uintmax_t size{0};

    [[nodiscard]] bool operator==(const FileInfo& other) const = default;
};

}  // namespace amoeba::scanner
