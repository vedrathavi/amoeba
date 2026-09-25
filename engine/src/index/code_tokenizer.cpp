#include "amoeba/index/code_tokenizer.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace amoeba::index {

using namespace std;

string CodeTokenizer::normalize_term(string_view term) {
    // Trim leading whitespace
    size_t start = 0;
    while (start < term.size() && isspace(static_cast<unsigned char>(term[start]))) {
        start++;
    }
    if (start >= term.size()) {
        return "";
    }

    // Trim trailing whitespace
    size_t end = term.size();
    while (end > start && isspace(static_cast<unsigned char>(term[end - 1]))) {
        end--;
    }

    string normalized;
    normalized.reserve(end - start);
    for (size_t i = start; i < end; ++i) {
        normalized.push_back(static_cast<char>(tolower(static_cast<unsigned char>(term[i]))));
    }
    return normalized;
}

vector<string> CodeTokenizer::tokenize_identifier(string_view identifier) {
    if (identifier.empty()) {
        return {};
    }

    // Strip leading and trailing common symbol noise (quotes, punctuation, namespace delimiters)
    size_t start = 0;
    while (start < identifier.size() &&
           (identifier[start] == '"' || identifier[start] == '\'' || identifier[start] == '<' ||
            identifier[start] == '>' || identifier[start] == '~' || identifier[start] == '#' ||
            identifier[start] == '.' || identifier[start] == '*' || identifier[start] == '&')) {
        start++;
    }

    size_t end = identifier.size();
    while (end > start && (identifier[end - 1] == '"' || identifier[end - 1] == '\'' ||
                           identifier[end - 1] == '<' || identifier[end - 1] == '>' ||
                           identifier[end - 1] == ';' || identifier[end - 1] == '.' ||
                           identifier[end - 1] == '*' || identifier[end - 1] == '&')) {
        end--;
    }

    if (start >= end) {
        return {};
    }

    string_view clean_id = identifier.substr(start, end - start);
    string full_normalized = normalize_term(clean_id);

    vector<string> tokens;
    unordered_set<string> seen;

    if (!full_normalized.empty()) {
        tokens.push_back(full_normalized);
        seen.insert(full_normalized);
    }

    // Sub-word split
    string current_chunk;
    for (size_t i = 0; i < clean_id.size(); ++i) {
        const char c = clean_id[i];

        if (c == '_' || c == '-' || c == ':' || c == '.' || c == '/' || c == '\\' ||
            isspace(static_cast<unsigned char>(c))) {
            if (!current_chunk.empty()) {
                string norm_chunk = normalize_term(current_chunk);
                if (!norm_chunk.empty() && !seen.contains(norm_chunk)) {
                    tokens.push_back(norm_chunk);
                    seen.insert(norm_chunk);
                }
                current_chunk.clear();
            }
            continue;
        }

        const bool is_upper = isupper(static_cast<unsigned char>(c)) != 0;
        const bool is_digit = isdigit(static_cast<unsigned char>(c)) != 0;

        if (!current_chunk.empty()) {
            const char prev = current_chunk.back();
            const bool prev_is_upper = isupper(static_cast<unsigned char>(prev)) != 0;
            const bool prev_is_lower = islower(static_cast<unsigned char>(prev)) != 0;
            const bool prev_is_digit = isdigit(static_cast<unsigned char>(prev)) != 0;

            // Transition: lowercase/digit to uppercase (e.g. getUser)
            if (is_upper && (prev_is_lower || prev_is_digit)) {
                string norm_chunk = normalize_term(current_chunk);
                if (!norm_chunk.empty() && !seen.contains(norm_chunk)) {
                    tokens.push_back(norm_chunk);
                    seen.insert(norm_chunk);
                }
                current_chunk.clear();
            }
            // Transition: sequence of uppercase followed by lowercase (e.g. XMLReader -> XML +
            // Reader)
            else if (prev_is_upper && is_upper && (i + 1 < clean_id.size()) &&
                     islower(static_cast<unsigned char>(clean_id[i + 1]))) {
                string norm_chunk = normalize_term(current_chunk);
                if (!norm_chunk.empty() && !seen.contains(norm_chunk)) {
                    tokens.push_back(norm_chunk);
                    seen.insert(norm_chunk);
                }
                current_chunk.clear();
            }
            // Transition: letter to digit or digit to letter (e.g. user123 -> user + 123)
            else if ((is_digit && !prev_is_digit) || (!is_digit && prev_is_digit)) {
                string norm_chunk = normalize_term(current_chunk);
                if (!norm_chunk.empty() && !seen.contains(norm_chunk)) {
                    tokens.push_back(norm_chunk);
                    seen.insert(norm_chunk);
                }
                current_chunk.clear();
            }
        }

        current_chunk.push_back(c);
    }

    if (!current_chunk.empty()) {
        string norm_chunk = normalize_term(current_chunk);
        if (!norm_chunk.empty() && !seen.contains(norm_chunk)) {
            tokens.push_back(norm_chunk);
            seen.insert(norm_chunk);
        }
    }

    return tokens;
}

vector<string> CodeTokenizer::tokenize_query(string_view query) {
    if (query.empty()) {
        return {};
    }

    vector<string> tokens;
    unordered_set<string> seen;

    size_t start = 0;
    while (start < query.size()) {
        while (start < query.size() && isspace(static_cast<unsigned char>(query[start]))) {
            start++;
        }
        if (start >= query.size()) {
            break;
        }

        size_t end = start;
        while (end < query.size() && !isspace(static_cast<unsigned char>(query[end]))) {
            end++;
        }

        string_view word = query.substr(start, end - start);
        const auto word_tokens = tokenize_identifier(word);
        for (const auto& token : word_tokens) {
            if (!seen.contains(token)) {
                tokens.push_back(token);
                seen.insert(token);
            }
        }
        start = end;
    }

    return tokens;
}

vector<string> CodeTokenizer::tokenize_path(string_view file_path) {
    if (file_path.empty()) {
        return {};
    }

    vector<string> tokens;
    unordered_set<string> seen;

    string norm_path = normalize_term(file_path);
    if (!norm_path.empty()) {
        tokens.push_back(norm_path);
        seen.insert(norm_path);
    }

    size_t start = 0;
    while (start < file_path.size()) {
        while (start < file_path.size() &&
               (file_path[start] == '/' || file_path[start] == '\\' || file_path[start] == '.' ||
                isspace(static_cast<unsigned char>(file_path[start])))) {
            start++;
        }
        if (start >= file_path.size()) {
            break;
        }

        size_t end = start;
        while (end < file_path.size() && file_path[end] != '/' && file_path[end] != '\\' &&
               file_path[end] != '.' && !isspace(static_cast<unsigned char>(file_path[end]))) {
            end++;
        }

        string_view segment = file_path.substr(start, end - start);
        const auto segment_tokens = tokenize_identifier(segment);
        for (const auto& token : segment_tokens) {
            if (!seen.contains(token)) {
                tokens.push_back(token);
                seen.insert(token);
            }
        }
        start = end;
    }

    return tokens;
}

}  // namespace amoeba::index
