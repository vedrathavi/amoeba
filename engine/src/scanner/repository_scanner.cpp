#include "amoeba/scanner/repository_scanner.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>
#include <stdexcept>
#include <string>

namespace amoeba::scanner {

using namespace std;
using namespace std::filesystem;

namespace {

constexpr array<string_view, 7> EXCLUDED_DIRECTORIES = {
    ".git", "node_modules", "build", "dist", "out", "target", "coverage",
};

struct ExtensionMapping {
    string_view extension;
    string_view language;
};

constexpr array<ExtensionMapping, 19> SUPPORTED_EXTENSIONS = {{
    {.extension = ".c", .language = "C"},
    {.extension = ".h", .language = "C/C++ Header"},
    {.extension = ".cc", .language = "C++"},
    {.extension = ".hh", .language = "C++"},
    {.extension = ".cpp", .language = "C++"},
    {.extension = ".hpp", .language = "C++"},
    {.extension = ".cxx", .language = "C++"},
    {.extension = ".hxx", .language = "C++"},
    {.extension = ".py", .language = "Python"},
    {.extension = ".js", .language = "JavaScript"},
    {.extension = ".jsx", .language = "JavaScript (React)"},
    {.extension = ".ts", .language = "TypeScript"},
    {.extension = ".tsx", .language = "TypeScript (React)"},
    {.extension = ".java", .language = "Java"},
    {.extension = ".go", .language = "Go"},
    {.extension = ".rs", .language = "Rust"},
    {.extension = ".html", .language = "HTML"},
    {.extension = ".htm", .language = "HTML"},
    {.extension = ".css", .language = "CSS"},
}};

bool iequals(string_view a, string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    return ranges::equal(a, b, [](char c1, char c2) noexcept {
        return tolower(static_cast<unsigned char>(c1)) == tolower(static_cast<unsigned char>(c2));
    });
}

}  // namespace

bool RepositoryScanner::is_excluded_directory(string_view dir_name) noexcept {
    return ranges::any_of(EXCLUDED_DIRECTORIES, [dir_name](string_view excluded) noexcept {
        return dir_name == excluded;
    });
}

bool RepositoryScanner::is_supported_extension(string_view extension) noexcept {
    return ranges::any_of(SUPPORTED_EXTENSIONS,
                          [extension](const ExtensionMapping& mapping) noexcept {
                              return iequals(extension, mapping.extension);
                          });
}

string_view RepositoryScanner::get_language_name(string_view extension) noexcept {
    for (const auto& mapping : SUPPORTED_EXTENSIONS) {
        if (iequals(extension, mapping.extension)) {
            return mapping.language;
        }
    }
    return "Unknown";
}

ScanResult RepositoryScanner::scan(const path& root_path) const {
    error_code ec;

    if (!exists(root_path, ec)) {
        throw invalid_argument("Repository path does not exist: " + root_path.string());
    }

    if (!is_directory(root_path, ec)) {
        throw invalid_argument("Repository path is not a directory: " + root_path.string());
    }

    ScanResult result;
    result.root_path = root_path;

    const auto options = directory_options::skip_permission_denied;
    auto iterator = recursive_directory_iterator(root_path, options, ec);
    const auto end_iterator = recursive_directory_iterator();

    while (iterator != end_iterator && !ec) {
        const auto& entry = *iterator;

        // Check if current directory entry should be excluded from recursive descent
        if (entry.is_directory(ec)) {
            const string dir_name = entry.path().filename().string();
            if (is_excluded_directory(dir_name)) {
                iterator.disable_recursion_pending();
            }
            iterator.increment(ec);
            continue;
        }

        if (entry.is_regular_file(ec)) {
            result.total_files_discovered++;

            const string ext = entry.path().extension().string();
            if (is_supported_extension(ext)) {
                const auto file_size = entry.file_size(ec);
                result.files.push_back(FileInfo{
                    .path = entry.path(),
                    .extension = ext,
                    .size = ec ? 0 : file_size,
                });
            } else {
                result.total_files_ignored++;
            }
        }

        iterator.increment(ec);
    }

    return result;
}

}  // namespace amoeba::scanner
