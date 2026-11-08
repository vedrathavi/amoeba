#include "amoeba/scanner/repository_scanner.hpp"

#include <git2.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::scanner {

using namespace std;
using namespace std::filesystem;

namespace {

// Tier 1: Non-overridable internal safety directories
constexpr array<string_view, 2> INTERNAL_SAFETY_DIRECTORIES = {
    ".git", ".amoeba"
};

// Tier 5: Fallback default excluded directories
constexpr array<string_view, 20> DEFAULT_EXCLUDED_DIRECTORIES = {
    ".git", "node_modules", "build", "dist", "out", "target", "coverage", "benchmarks",
    ".venv", "venv", "env", "__pycache__", ".pytest_cache", ".mypy_cache",
    ".ruff_cache", "site-packages", "vendor", ".gradle", ".next", ".nuxt"
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

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

string path_to_utf8(const path& p) {
    const wstring& ws = p.native();
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), result.data(), len, nullptr, nullptr);
    replace(result.begin(), result.end(), '\\', '/');
    return result;
}
#else
string path_to_utf8(const path& p) {
    return p.generic_string();
}
#endif

bool iequals(string_view a, string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    return ranges::equal(a, b, [](char c1, char c2) noexcept {
        return tolower(static_cast<unsigned char>(c1)) == tolower(static_cast<unsigned char>(c2));
    });
}

bool is_binary_file(const path& file_path) noexcept {
    ifstream file(file_path, ios::binary);
    if (!file) {
        return false;
    }
    char buf[512];
    file.read(buf, sizeof(buf));
    const auto bytes_read = file.gcount();
    for (streamsize i = 0; i < bytes_read; ++i) {
        if (buf[i] == '\0') {
            return true;
        }
    }
    return false;
}

// Glob pattern matching helper supporting *, **, and ?
bool match_glob(string_view pat, string_view str) noexcept {
    size_t p = 0, s = 0;
    while (p < pat.size() && s < str.size()) {
        if (pat[p] == '*') {
            // Check for '**'
            if (p + 1 < pat.size() && pat[p + 1] == '*') {
                size_t next_p = p + 2;
                if (next_p < pat.size() && pat[next_p] == '/') {
                    next_p++;
                }
                if (next_p == pat.size()) {
                    return true;
                }
                for (size_t next_s = s; next_s <= str.size(); ++next_s) {
                    if (match_glob(pat.substr(next_p), str.substr(next_s))) {
                        return true;
                    }
                }
                return false;
            }
            // Single '*' matches within a path segment (not crossing '/')
            for (size_t next_s = s; next_s <= str.size() && (next_s == s || str[next_s - 1] != '/'); ++next_s) {
                if (match_glob(pat.substr(p + 1), str.substr(next_s))) {
                    return true;
                }
            }
            return false;
        } else if (pat[p] == '?') {
            if (str[s] == '/') return false;
            p++;
            s++;
        } else if (pat[p] == str[s]) {
            p++;
            s++;
        } else {
            return false;
        }
    }
    while (p < pat.size() && pat[p] == '*') {
        p++;
    }
    return p == pat.size() && s == str.size();
}

struct AmoebaIgnoreRule {
    string pattern;
    bool is_negation{false};
    bool only_dir{false};
    bool root_relative{false};
    path rule_root;  ///< Directory where .amoebaignore is located
};

class AmoebaIgnoreMatcher {
public:
    void load_rules_from_file(const path& ignore_file_path, const path& /*repo_root*/) {
        ifstream file(ignore_file_path);
        if (!file) return;

        const path rule_root = ignore_file_path.parent_path();
        string line;
        while (getline(file, line)) {
            // Strip trailing carriage returns and spaces
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
                line.pop_back();
            }
            if (line.empty() || line.front() == '#') {
                continue;
            }

            AmoebaIgnoreRule rule;
            rule.rule_root = rule_root;

            if (line.front() == '!') {
                rule.is_negation = true;
                line = line.substr(1);
            }

            if (line.empty()) continue;

            if (line.back() == '/') {
                rule.only_dir = true;
                line.pop_back();
            }

            if (line.front() == '/') {
                rule.root_relative = true;
                line = line.substr(1);
            } else if (line.find('/') != string::npos) {
                rule.root_relative = true;
            }

            rule.pattern = std::move(line);
            rules_.push_back(std::move(rule));
        }
    }

    enum class MatchResult {
        NoMatch,
        Ignored,
        Unignored
    };

    struct Evaluation {
        MatchResult result{MatchResult::NoMatch};
        string matching_rule{};
    };

    [[nodiscard]] Evaluation evaluate(const path& full_path, const path& repo_root, bool is_dir) const {
        error_code ec;
        const auto rel_to_repo = relative(full_path, repo_root, ec);
        if (ec) return {};

        const string rel_str = path_to_utf8(rel_to_repo);
        const string filename = path_to_utf8(full_path.filename());

        Evaluation eval;

        for (const auto& rule : rules_) {
            bool matched = false;
            if (rule.root_relative) {
                const auto rel_to_rule = relative(full_path, rule.rule_root, ec);
                if (!ec) {
                    const string path_str = path_to_utf8(rel_to_rule);
                    if (is_dir) {
                        matched = match_glob(rule.pattern, path_str);
                    } else {
                        if (rule.only_dir) {
                            matched = path_str.starts_with(rule.pattern + "/") ||
                                      match_glob(rule.pattern + "/**", path_str);
                        } else {
                            matched = match_glob(rule.pattern, path_str);
                        }
                    }
                }
            } else {
                if (is_dir) {
                    matched = match_glob(rule.pattern, filename) || match_glob(rule.pattern, rel_str);
                } else {
                    if (rule.only_dir) {
                        matched = rel_str.starts_with(rule.pattern + "/") ||
                                  rel_str.find("/" + rule.pattern + "/") != string::npos ||
                                  match_glob(rule.pattern + "/**", rel_str);
                    } else {
                        matched = match_glob(rule.pattern, filename) || match_glob(rule.pattern, rel_str);
                    }
                }
            }

            if (matched) {
                eval.result = rule.is_negation ? MatchResult::Unignored : MatchResult::Ignored;
                eval.matching_rule = (rule.is_negation ? "!" : "") + rule.pattern + (rule.only_dir ? "/" : "");
            }
        }

        return eval;
    }

    [[nodiscard]] bool has_negation_under(string_view dir_rel_path) const noexcept {
        const string prefix = string(dir_rel_path) + "/";
        for (const auto& rule : rules_) {
            if (rule.is_negation) {
                if (rule.pattern.starts_with(prefix) || rule.pattern == dir_rel_path) {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] bool empty() const noexcept { return rules_.empty(); }

private:
    vector<AmoebaIgnoreRule> rules_;
};

// RAII guard for libgit2 global lifecycle
struct GitGlobalGuard {
    GitGlobalGuard() { git_libgit2_init(); }
    ~GitGlobalGuard() { git_libgit2_shutdown(); }
};

// RAII handle for git repository and index inspection
struct GitRepoHandle {
    git_repository* repo{nullptr};
    git_index* index{nullptr};

    explicit GitRepoHandle(const path& root_path) {
        if (git_repository_open_ext(&repo, root_path.string().c_str(), 0, nullptr) == 0 && repo != nullptr) {
            git_repository_index(&index, repo);
        }
    }

    ~GitRepoHandle() {
        if (index) {
            git_index_free(index);
            index = nullptr;
        }
        if (repo) {
            git_repository_free(repo);
            repo = nullptr;
        }
    }

    [[nodiscard]] bool is_valid() const noexcept { return repo != nullptr; }

    [[nodiscard]] bool is_tracked(string_view git_rel_path) const noexcept {
        if (!index) return false;
        size_t pos = 0;
        return git_index_find(&pos, index, string(git_rel_path).c_str()) == 0;
    }

    [[nodiscard]] bool is_ignored(string_view git_rel_path) const noexcept {
        if (!repo) return false;
        int ignored = 0;
        if (git_ignore_path_is_ignored(&ignored, repo, string(git_rel_path).c_str()) == 0) {
            return ignored != 0;
        }
        return false;
    }
};

}  // namespace

bool RepositoryScanner::is_excluded_directory(string_view dir_name) noexcept {
    return is_excluded_directory(dir_name, {});
}

bool RepositoryScanner::is_excluded_directory(string_view dir_name,
                                              span<const string> custom_exclusions) noexcept {
    if (ranges::any_of(DEFAULT_EXCLUDED_DIRECTORIES, [dir_name](string_view excluded) noexcept {
            return dir_name == excluded;
        })) {
        return true;
    }
    return ranges::any_of(custom_exclusions, [dir_name](const string& custom) noexcept {
        return iequals(dir_name, custom);
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

string_view RepositoryScanner::skip_reason_to_string(FileSkipReason reason) noexcept {
    switch (reason) {
        case FileSkipReason::UnsupportedExtension: return "UnsupportedExtension";
        case FileSkipReason::BinaryContent: return "BinaryContent";
        case FileSkipReason::AboveNormalThresholdDeferred: return "AboveNormalThresholdDeferred";
        case FileSkipReason::ExceedsHardLimit: return "ExceedsHardLimit";
        case FileSkipReason::GitIgnored: return "GitIgnored";
        case FileSkipReason::AmoebaIgnored: return "AmoebaIgnored";
        case FileSkipReason::CustomExclusion: return "CustomExclusion";
        case FileSkipReason::InternalSafety: return "InternalSafety";
        case FileSkipReason::FilesystemError: return "FilesystemError";
    }
    return "Unknown";
}

ScanResult RepositoryScanner::scan(const path& root_path, const ScanOptions& options) const {
    error_code ec;

    if (!exists(root_path, ec)) {
        throw invalid_argument("Repository path does not exist: " + root_path.string());
    }

    if (!is_directory(root_path, ec)) {
        throw invalid_argument("Repository path is not a directory: " + root_path.string());
    }

    ScanResult result;
    result.root_path = root_path;

    // Effective thresholds respecting backward compatibility
    const size_t effective_normal_threshold = (options.max_file_size_bytes > 0)
        ? options.max_file_size_bytes
        : options.normal_file_size_bytes;
    const size_t hard_ceiling = options.hard_max_file_size_bytes;

    // Initialize libgit2 global runtime and open repository if git is enabled
    GitGlobalGuard git_guard;
    GitRepoHandle git_repo(root_path);

    // Initialize Amoeba-specific ignore matcher (.amoebaignore)
    AmoebaIgnoreMatcher amoeba_matcher;
    if (options.enable_amoeba_ignore) {
        const path root_ignore = root_path / ".amoebaignore";
        if (exists(root_ignore, ec)) {
            amoeba_matcher.load_rules_from_file(root_ignore, root_path);
        }
    }

    const auto dir_options = directory_options::skip_permission_denied;
    auto iterator = recursive_directory_iterator(root_path, dir_options, ec);
    const auto end_iterator = recursive_directory_iterator();

    while (iterator != end_iterator && !ec) {
        try {
            const auto& entry = *iterator;
            const auto entry_path = entry.path();
            const auto rel_path = relative(entry_path, root_path, ec);
            const string rel_str = ec ? path_to_utf8(entry_path.filename()) : path_to_utf8(rel_path);
            const string filename = path_to_utf8(entry_path.filename());

        // ---------------------------------------------------------------------
        // DIRECTORY TRAVERSAL & EXCLUSION PRUNING
        // ---------------------------------------------------------------------
        if (entry.is_directory(ec)) {
            // Tier 1: Non-overridable internal safety check (.git, .amoeba)
            if (ranges::any_of(INTERNAL_SAFETY_DIRECTORIES, [filename](string_view s) noexcept {
                    return filename == s;
                })) {
                iterator.disable_recursion_pending();
                if (options.collect_skipped_details) {
                    result.skipped_details.push_back({
                        .relative_path = rel_path,
                        .file_size = 0,
                        .reason = FileSkipReason::InternalSafety,
                        .matching_rule = filename,
                        .rule_source = "Internal"
                    });
                }
                iterator.increment(ec);
                continue;
            }

            // Tier 2: CLI custom exclusions
            if (ranges::any_of(options.custom_exclusions, [filename, &rel_str](const string& custom) noexcept {
                    return iequals(filename, custom) || iequals(rel_str, custom);
                })) {
                iterator.disable_recursion_pending();
                if (options.collect_skipped_details) {
                    result.skipped_details.push_back({
                        .relative_path = rel_path,
                        .file_size = 0,
                        .reason = FileSkipReason::CustomExclusion,
                        .matching_rule = filename,
                        .rule_source = "CLI"
                    });
                }
                iterator.increment(ec);
                continue;
            }

            // Tier 3: .amoebaignore check on directory
            if (options.enable_amoeba_ignore && !amoeba_matcher.empty()) {
                const auto amoeba_eval = amoeba_matcher.evaluate(entry_path, root_path, /*is_dir=*/true);
                if (amoeba_eval.result == AmoebaIgnoreMatcher::MatchResult::Ignored) {
                    if (!amoeba_matcher.has_negation_under(rel_str)) {
                        iterator.disable_recursion_pending();
                        result.total_files_amoeba_ignored++;
                        if (options.collect_skipped_details) {
                            result.skipped_details.push_back({
                                .relative_path = rel_path,
                                .file_size = 0,
                                .reason = FileSkipReason::AmoebaIgnored,
                                .matching_rule = amoeba_eval.matching_rule,
                                .rule_source = ".amoebaignore"
                            });
                        }
                        iterator.increment(ec);
                        continue;
                    }
                    // Negation exists under this directory: allow traversal to evaluate children
                }
            }

            // Tier 4: Git ignore check on directory
            if (options.enable_git_ignore && git_repo.is_valid()) {
                // In git, directory path for ignore check is checked with trailing slash
                const string dir_git_path = rel_str + "/";
                if (git_repo.is_ignored(dir_git_path) || git_repo.is_ignored(rel_str)) {
                    iterator.disable_recursion_pending();
                    result.total_files_git_ignored++;
                    if (options.collect_skipped_details) {
                        result.skipped_details.push_back({
                            .relative_path = rel_path,
                            .file_size = 0,
                            .reason = FileSkipReason::GitIgnored,
                            .matching_rule = rel_str,
                            .rule_source = ".gitignore"
                        });
                    }
                    iterator.increment(ec);
                    continue;
                }
            }

            // Tier 5: Default excluded directories fallback
            if (is_excluded_directory(filename, {})) {
                iterator.disable_recursion_pending();
                if (options.collect_skipped_details) {
                    result.skipped_details.push_back({
                        .relative_path = rel_path,
                        .file_size = 0,
                        .reason = FileSkipReason::CustomExclusion,
                        .matching_rule = filename,
                        .rule_source = "Default"
                    });
                }
                iterator.increment(ec);
                continue;
            }

            // Check for nested .amoebaignore
            if (options.enable_amoeba_ignore) {
                const path nested_ignore = entry_path / ".amoebaignore";
                if (exists(nested_ignore, ec)) {
                    amoeba_matcher.load_rules_from_file(nested_ignore, root_path);
                }
            }

            iterator.increment(ec);
            continue;
        }

        // ---------------------------------------------------------------------
        // REGULAR FILE EVALUATION
        // ---------------------------------------------------------------------
        if (entry.is_regular_file(ec)) {
            result.total_files_discovered++;
            const auto file_size = entry.file_size(ec);

            // Tier 1: Internal safety check
            if (ranges::any_of(INTERNAL_SAFETY_DIRECTORIES, [&rel_str](string_view s) noexcept {
                    return rel_str.find(s) != string::npos;
                })) {
                result.total_files_ignored++;
                if (options.collect_skipped_details) {
                    result.skipped_details.push_back({
                        .relative_path = rel_path,
                        .file_size = ec ? 0 : file_size,
                        .reason = FileSkipReason::InternalSafety,
                        .matching_rule = rel_str,
                        .rule_source = "Internal"
                    });
                }
                iterator.increment(ec);
                continue;
            }

            // Tier 2: CLI custom exclusions
            if (ranges::any_of(options.custom_exclusions, [filename, &rel_str](const string& custom) noexcept {
                    return iequals(filename, custom) || iequals(rel_str, custom);
                })) {
                result.total_files_ignored++;
                if (options.collect_skipped_details) {
                    result.skipped_details.push_back({
                        .relative_path = rel_path,
                        .file_size = ec ? 0 : file_size,
                        .reason = FileSkipReason::CustomExclusion,
                        .matching_rule = filename,
                        .rule_source = "CLI"
                    });
                }
                iterator.increment(ec);
                continue;
            }

            // Tier 3: .amoebaignore check (applies to tracked and untracked files)
            bool amoeba_unignored = false;
            if (options.enable_amoeba_ignore && !amoeba_matcher.empty()) {
                const auto amoeba_eval = amoeba_matcher.evaluate(entry_path, root_path, /*is_dir=*/false);
                if (amoeba_eval.result == AmoebaIgnoreMatcher::MatchResult::Ignored) {
                    result.total_files_amoeba_ignored++;
                    result.total_files_ignored++;
                    if (options.collect_skipped_details) {
                        result.skipped_details.push_back({
                            .relative_path = rel_path,
                            .file_size = ec ? 0 : file_size,
                            .reason = FileSkipReason::AmoebaIgnored,
                            .matching_rule = amoeba_eval.matching_rule,
                            .rule_source = ".amoebaignore"
                        });
                    }
                    iterator.increment(ec);
                    continue;
                } else if (amoeba_eval.result == AmoebaIgnoreMatcher::MatchResult::Unignored) {
                    amoeba_unignored = true;
                }
            }

            // Tier 4: Git ignore check (Git-compatible mode: tracked files remain eligible)
            if (options.enable_git_ignore && git_repo.is_valid() && !amoeba_unignored) {
                const bool is_tracked = git_repo.is_tracked(rel_str);
                if (!is_tracked && git_repo.is_ignored(rel_str)) {
                    result.total_files_git_ignored++;
                    result.total_files_ignored++;
                    if (options.collect_skipped_details) {
                        result.skipped_details.push_back({
                            .relative_path = rel_path,
                            .file_size = ec ? 0 : file_size,
                            .reason = FileSkipReason::GitIgnored,
                            .matching_rule = rel_str,
                            .rule_source = ".gitignore"
                        });
                    }
                    iterator.increment(ec);
                    continue;
                }
            }

            // Extension check
            const string ext = path_to_utf8(entry_path.extension());
            if (!is_supported_extension(ext)) {
                result.total_files_ignored++;
                if (options.collect_skipped_details) {
                    result.skipped_details.push_back({
                        .relative_path = rel_path,
                        .file_size = ec ? 0 : file_size,
                        .reason = FileSkipReason::UnsupportedExtension,
                        .matching_rule = ext,
                        .rule_source = "LanguageFilter"
                    });
                }
                iterator.increment(ec);
                continue;
            }

            // -----------------------------------------------------------------
            // 3-TIER FILE SIZE POLICY
            // -----------------------------------------------------------------
            if (!ec && hard_ceiling > 0 && file_size > hard_ceiling) {
                // Tier 3: Hard safety ceiling exceeded
                result.total_files_skipped_size++;
                result.total_files_ignored++;
                if (options.collect_skipped_details) {
                    result.skipped_details.push_back({
                        .relative_path = rel_path,
                        .file_size = file_size,
                        .reason = FileSkipReason::ExceedsHardLimit,
                        .matching_rule = "> hard_max_file_size_bytes",
                        .rule_source = "SizePolicy"
                    });
                }
                iterator.increment(ec);
                continue;
            }

            if (!ec && effective_normal_threshold > 0 && file_size > effective_normal_threshold) {
                // Between normal threshold and hard ceiling
                if (!options.include_large_files) {
                    // Deferred / skipped by default
                    result.total_files_skipped_above_normal++;
                    result.total_files_skipped_size++;
                    result.total_files_ignored++;
                    if (options.collect_skipped_details) {
                        result.skipped_details.push_back({
                            .relative_path = rel_path,
                            .file_size = file_size,
                            .reason = FileSkipReason::AboveNormalThresholdDeferred,
                            .matching_rule = "> normal_file_size_bytes",
                            .rule_source = "SizePolicy"
                        });
                    }
                    iterator.increment(ec);
                    continue;
                }
                // When include_large_files is true: file is admitted for controlled indexing!
            }

            // -----------------------------------------------------------------
            // BINARY CONTENT CHECK
            // -----------------------------------------------------------------
            if (options.skip_binary_files && is_binary_file(entry_path)) {
                result.total_files_skipped_binary++;
                result.total_files_ignored++;
                if (options.collect_skipped_details) {
                    result.skipped_details.push_back({
                        .relative_path = rel_path,
                        .file_size = ec ? 0 : file_size,
                        .reason = FileSkipReason::BinaryContent,
                        .matching_rule = "null_byte_detected",
                        .rule_source = "BinaryFilter"
                    });
                }
                iterator.increment(ec);
                continue;
            }

            // File passed all filters: admitted for indexing
            result.files.push_back(FileInfo{
                .path = entry_path,
                .extension = ext,
                .size = ec ? 0 : file_size,
            });
        }
    } catch (const exception& ex) {
        result.total_files_ignored++;
        if (options.collect_skipped_details) {
            result.skipped_details.push_back({
                .relative_path = path_to_utf8(root_path),
                .file_size = 0,
                .reason = FileSkipReason::FilesystemError,
                .matching_rule = ex.what(),
                .rule_source = "Filesystem"
            });
        }
    }

        iterator.increment(ec);
    }

    return result;
}

}  // namespace amoeba::scanner
