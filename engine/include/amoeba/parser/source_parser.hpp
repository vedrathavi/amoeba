#pragma once

#include "amoeba/parser/parsed_file.hpp"

#include <filesystem>
#include <memory>
#include <string_view>

namespace amoeba::parser {

using namespace std;
using namespace std::filesystem;

/**
 * @brief Parses source code files using Tree-sitter and extracts structural elements.
 */
class SourceParser {
public:
    SourceParser();
    ~SourceParser();

    SourceParser(const SourceParser&) = delete;
    SourceParser& operator=(const SourceParser&) = delete;
    SourceParser(SourceParser&&) noexcept;
    SourceParser& operator=(SourceParser&&) noexcept;

    /**
     * @brief Parses source code text for the specified language ("C", "C++").
     * @param source Raw source code text.
     * @param language Target language ("C" or "C++").
     * @param file_path Optional path for metadata.
     * @return ParsedFile containing extracted structural elements.
     */
    [[nodiscard]] ParsedFile parse_source(string_view source, string_view language = "C++",
                                          const path& file_path = {}) const;

    /**
     * @brief Reads a source file from disk and parses its structural syntax.
     * @param file_path Path to the source file.
     * @throws invalid_argument if the file does not exist or is not a regular file.
     * @return ParsedFile containing extracted structural elements.
     */
    [[nodiscard]] ParsedFile parse_file(const path& file_path) const;

    /**
     * @brief Determines the language name ("C", "C++") from a file path or extension.
     */
    [[nodiscard]] static string_view detect_language(const path& file_path);

private:
    struct Impl;
    unique_ptr<Impl> impl_;
};

}  // namespace amoeba::parser
