#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace amoeba::index {

using namespace std;

/**
 * @brief Utility for tokenizing and normalizing source code identifiers and search queries.
 */
class CodeTokenizer {
public:
    /**
     * @brief Normalizes a string by trimming whitespace and converting to lowercase.
     */
    [[nodiscard]] static string normalize_term(string_view term);

    /**
     * @brief Splits a code identifier into sub-words (camelCase, PascalCase, snake_case,
     * kebab-case) and returns normalized terms including the full identifier.
     *
     * Example:
     *   "getUserById" -> {"getuserbyid", "get", "user", "by", "id"}
     *   "SCREAMING_SNAKE" -> {"screaming_snake", "screaming", "snake"}
     */
    [[nodiscard]] static vector<string> tokenize_identifier(string_view identifier);

    /**
     * @brief Tokenizes a user query string into normalized search terms.
     *
     * Example:
     *   "User Auth" -> {"user", "auth"}
     *   "getUserById" -> {"getuserbyid", "get", "user", "by", "id"}
     */
    [[nodiscard]] static vector<string> tokenize_query(string_view query);

    /**
     * @brief Tokenizes a file path into searchable directory, filename, and stem tokens.
     */
    [[nodiscard]] static vector<string> tokenize_path(string_view file_path);
};

}  // namespace amoeba::index
