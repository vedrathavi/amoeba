#include "amoeba/scanner/repository_scanner.hpp"

#include <git2.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace {

using namespace std;
using namespace std::filesystem;

class RepositoryScannerTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto timestamp = chrono::steady_clock::now().time_since_epoch().count();
        test_dir = temp_directory_path() / ("amoeba_scanner_test_" + to_string(timestamp));
        create_directories(test_dir);
    }

    void TearDown() override {
        error_code ec;
        remove_all(test_dir, ec);
    }

    void create_file(const path& relative_path, string_view content = "sample content") {
        const auto full_path = test_dir / relative_path;
        create_directories(full_path.parent_path());
        ofstream file(full_path, ios::binary);
        file << content;
    }

    path test_dir;
    amoeba::scanner::RepositoryScanner scanner;
};

TEST_F(RepositoryScannerTest, EmptyDirectoryYieldsZeroFiles) {
    const auto result = scanner.scan(test_dir);

    EXPECT_EQ(result.root_path, test_dir);
    EXPECT_EQ(result.total_files_discovered, 0U);
    EXPECT_EQ(result.total_files_included(), 0U);
    EXPECT_EQ(result.total_files_ignored, 0U);
    EXPECT_TRUE(result.files.empty());
}

TEST_F(RepositoryScannerTest, DiscoversSupportedSourceFilesRecursively) {
    create_file("main.cpp", "int main() { return 0; }");
    create_file("include/header.hpp", "#pragma once");
    create_file("src/utils/helper.cc", "void helper() {}");
    create_file("scripts/deploy.py", "print('deploy')");
    create_file("frontend/src/app.tsx", "export const App = () => null;");
    create_file("backend/server.go", "package main");
    create_file("core/lib.rs", "pub fn init() {}");
    create_file("jvm/Main.java", "public class Main {}");

    const auto result = scanner.scan(test_dir);

    EXPECT_EQ(result.total_files_discovered, 8U);
    EXPECT_EQ(result.total_files_included(), 8U);
    EXPECT_EQ(result.total_files_ignored, 0U);
}

TEST_F(RepositoryScannerTest, IgnoresUnsupportedFileExtensions) {
    create_file("main.cpp", "int main() {}");
    create_file("README.md", "# Project Documentation");
    create_file("package.json", "{}");
    create_file("assets/logo.png", "\x89PNG\r\n\x1a\n");
    create_file("archive.zip", "PK\x03\x04");

    const auto result = scanner.scan(test_dir);

    EXPECT_EQ(result.total_files_discovered, 5U);
    EXPECT_EQ(result.total_files_included(), 1U);
    EXPECT_EQ(result.total_files_ignored, 4U);
    ASSERT_EQ(result.files.size(), 1U);
    EXPECT_EQ(result.files[0].extension, ".cpp");
}

TEST_F(RepositoryScannerTest, SkipsExcludedDirectories) {
    create_file("src/app.cpp", "int main() {}");
    create_file(".git/HEAD", "ref: refs/heads/main");
    create_file(".git/objects/00/file.cpp", "ignored");
    create_file("node_modules/pkg/index.js", "console.log()");
    create_file("build/CMakeFiles/test.cpp", "generated");
    create_file("dist/bundle.js", "bundled");
    create_file("out/bin.exe", "binary");
    create_file("target/debug/app.rs", "compiled");
    create_file("coverage/report.html", "<html></html>");
    create_file("benchmarks/repoprobe/repo/fixture.cpp", "fixture");

    const auto result = scanner.scan(test_dir);

    EXPECT_EQ(result.total_files_discovered, 1U);
    EXPECT_EQ(result.total_files_included(), 1U);
    EXPECT_EQ(result.total_files_ignored, 0U);
    ASSERT_EQ(result.files.size(), 1U);
    EXPECT_EQ(result.files[0].path.filename(), "app.cpp");
}

TEST_F(RepositoryScannerTest, CapturesAccurateFileMetadata) {
    const string content = "constexpr int ANSWER = 42;\n";
    create_file("src/constants.hpp", content);

    const auto result = scanner.scan(test_dir);

    ASSERT_EQ(result.files.size(), 1U);
    const auto& file_info = result.files[0];

    EXPECT_EQ(file_info.path, test_dir / "src" / "constants.hpp");
    EXPECT_EQ(file_info.extension, ".hpp");
    EXPECT_EQ(file_info.size, content.size());
}

TEST_F(RepositoryScannerTest, ThrowsOnNonExistentPath) {
    const auto invalid_path = test_dir / "does_not_exist";

    EXPECT_THROW((void)scanner.scan(invalid_path), invalid_argument);
}

TEST_F(RepositoryScannerTest, ThrowsWhenPathIsRegularFile) {
    create_file("single_file.cpp", "int x = 0;");
    const auto file_path = test_dir / "single_file.cpp";

    EXPECT_THROW((void)scanner.scan(file_path), invalid_argument);
}

TEST_F(RepositoryScannerTest, SkipsExpandedDefaultDirectories) {
    create_file("src/app.py", "print('hello')");
    create_file(".venv/lib/site-packages/torch/init.py", "pass");
    create_file("venv/bin/activate.py", "pass");
    create_file("env/lib/pkg.py", "pass");
    create_file("vendor/github.com/lib/lib.go", "package lib");
    create_file("__pycache__/app.cpython-310.py", "cached");
    create_file(".next/cache/turbopack.js", "export default 1");
    create_file(".nuxt/dist/server.js", "export default 2");
    create_file(".gradle/caches/module.java", "class M {}");
    create_file(".pytest_cache/v/cache.py", "data");

    const auto result = scanner.scan(test_dir);

    EXPECT_EQ(result.total_files_discovered, 1U);
    EXPECT_EQ(result.total_files_included(), 1U);
    ASSERT_EQ(result.files.size(), 1U);
    EXPECT_EQ(result.files[0].path.filename(), "app.py");
}

TEST_F(RepositoryScannerTest, SkipsCustomExcludedDirectories) {
    create_file("src/core.cpp", "int a = 1;");
    create_file("legacy/old.cpp", "int b = 2;");
    create_file("third_party/lib.cpp", "int c = 3;");

    amoeba::scanner::ScanOptions opts;
    opts.custom_exclusions = {"legacy", "third_party"};

    const auto result = scanner.scan(test_dir, opts);

    EXPECT_EQ(result.total_files_discovered, 1U);
    EXPECT_EQ(result.total_files_included(), 1U);
    ASSERT_EQ(result.files.size(), 1U);
    EXPECT_EQ(result.files[0].path.filename(), "core.cpp");
}

TEST_F(RepositoryScannerTest, SkipsBinaryFilesWithSupportedExtension) {
    create_file("valid.py", "print('clean')");
    // Binary file containing a null byte disguised as a Python script
    string binary_data = "print('corrupt')\0hidden_binary_payload";
    binary_data.resize(32);
    create_file("corrupt.py", binary_data);

    const auto result = scanner.scan(test_dir);

    EXPECT_EQ(result.total_files_discovered, 2U);
    EXPECT_EQ(result.total_files_included(), 1U);
    EXPECT_EQ(result.total_files_skipped_binary, 1U);
    ASSERT_EQ(result.files.size(), 1U);
    EXPECT_EQ(result.files[0].path.filename(), "valid.py");
}

TEST_F(RepositoryScannerTest, EnforcesMaxFileSizeBytes) {
    create_file("small.cpp", "int x = 1;");
    string large_data(2000, 'A');
    create_file("large.cpp", large_data);

    amoeba::scanner::ScanOptions opts;
    opts.max_file_size_bytes = 1000;  // 1 KB limit

    const auto result = scanner.scan(test_dir, opts);

    EXPECT_EQ(result.total_files_discovered, 2U);
    EXPECT_EQ(result.total_files_included(), 1U);
    EXPECT_EQ(result.total_files_skipped_size, 1U);
    ASSERT_EQ(result.files.size(), 1U);
    EXPECT_EQ(result.files[0].path.filename(), "small.cpp");
}

TEST_F(RepositoryScannerTest, ThreeTierSizePolicyDeferredAndControlledIndexing) {
    create_file("tier1.cpp", "int a = 1;");                      // ~10 bytes
    create_file("tier2.cpp", string(3000, 'B'));                  // 3000 bytes
    create_file("tier3.cpp", string(10000, 'C'));                 // 10000 bytes

    amoeba::scanner::ScanOptions opts;
    opts.normal_file_size_bytes = 1000;   // Tier 1 threshold: 1KB
    opts.hard_max_file_size_bytes = 5000; // Tier 3 ceiling: 5KB
    opts.include_large_files = false;     // Default: defer tier 2

    const auto result_default = scanner.scan(test_dir, opts);
    EXPECT_EQ(result_default.total_files_discovered, 3U);
    EXPECT_EQ(result_default.total_files_included(), 1U);
    EXPECT_EQ(result_default.total_files_skipped_above_normal, 1U);
    EXPECT_EQ(result_default.total_files_skipped_size, 2U);  // tier2 deferred + tier3 hard skip
    ASSERT_EQ(result_default.files.size(), 1U);
    EXPECT_EQ(result_default.files[0].path.filename(), "tier1.cpp");

    // Enable controlled indexing for large files (tier 2)
    opts.include_large_files = true;
    const auto result_controlled = scanner.scan(test_dir, opts);
    EXPECT_EQ(result_controlled.total_files_discovered, 3U);
    EXPECT_EQ(result_controlled.total_files_included(), 2U);
    EXPECT_EQ(result_controlled.total_files_skipped_above_normal, 0U);
    EXPECT_EQ(result_controlled.total_files_skipped_size, 1U);  // tier 3 still exceeds hard limit!
}

TEST_F(RepositoryScannerTest, EmptyFileIsIncluded) {
    create_file("empty.cpp", "");

    const auto result = scanner.scan(test_dir);
    EXPECT_EQ(result.total_files_discovered, 1U);
    EXPECT_EQ(result.total_files_included(), 1U);
    ASSERT_EQ(result.files.size(), 1U);
    EXPECT_EQ(result.files[0].size, 0U);
    EXPECT_EQ(result.files[0].path.filename(), "empty.cpp");
}

TEST_F(RepositoryScannerTest, ExplainabilityReportingAndSkipReasonDetails) {
    create_file("clean.cpp", "int clean = 1;");
    create_file("huge.cpp", string(6000, 'X'));
    create_file("binary.py", string("def f():\0bad", 12));
    create_file("notes.txt", "unsupported");

    amoeba::scanner::ScanOptions opts;
    opts.normal_file_size_bytes = 2000;
    opts.hard_max_file_size_bytes = 5000;
    opts.collect_skipped_details = true;

    const auto result = scanner.scan(test_dir, opts);
    EXPECT_EQ(result.total_files_discovered, 4U);
    EXPECT_EQ(result.total_files_included(), 1U);
    EXPECT_EQ(result.total_files_ignored, 3U);
    EXPECT_EQ(result.skipped_details.size(), 3U);

    bool found_size = false;
    bool found_binary = false;
    bool found_ext = false;

    for (const auto& item : result.skipped_details) {
        if (item.reason == amoeba::scanner::FileSkipReason::ExceedsHardLimit) {
            found_size = true;
            EXPECT_EQ(item.relative_path.filename(), "huge.cpp");
            EXPECT_EQ(item.rule_source, "SizePolicy");
        } else if (item.reason == amoeba::scanner::FileSkipReason::BinaryContent) {
            found_binary = true;
            EXPECT_EQ(item.relative_path.filename(), "binary.py");
            EXPECT_EQ(item.rule_source, "BinaryFilter");
        } else if (item.reason == amoeba::scanner::FileSkipReason::UnsupportedExtension) {
            found_ext = true;
            EXPECT_EQ(item.relative_path.filename(), "notes.txt");
            EXPECT_EQ(item.rule_source, "LanguageFilter");
        }
    }

    EXPECT_TRUE(found_size);
    EXPECT_TRUE(found_binary);
    EXPECT_TRUE(found_ext);
}

TEST_F(RepositoryScannerTest, AmoebaIgnoreBasicAndWildcardWithNegation) {
    create_file(".amoebaignore",
                "# Project ignore rules\n"
                "generated/\n"
                "*.min.js\n"
                "!generated/public_api.cpp\n");

    create_file("src/main.cpp", "int main() {}");
    create_file("generated/secret.cpp", "int secret = 1;");
    create_file("generated/public_api.cpp", "int api = 2;");
    create_file("web/bundle.min.js", "console.log();");

    amoeba::scanner::ScanOptions opts;
    opts.enable_amoeba_ignore = true;
    opts.collect_skipped_details = true;

    const auto result = scanner.scan(test_dir, opts);

    // main.cpp and generated/public_api.cpp should be included
    EXPECT_EQ(result.total_files_included(), 2U);
    EXPECT_EQ(result.total_files_amoeba_ignored, 2U); // secret.cpp and bundle.min.js

    vector<string> included_names;
    for (const auto& f : result.files) {
        included_names.push_back(f.path.filename().string());
    }
    EXPECT_TRUE(ranges::find(included_names, "main.cpp") != included_names.end());
    EXPECT_TRUE(ranges::find(included_names, "public_api.cpp") != included_names.end());
    EXPECT_TRUE(ranges::find(included_names, "secret.cpp") == included_names.end());
    EXPECT_TRUE(ranges::find(included_names, "bundle.min.js") == included_names.end());
}

TEST_F(RepositoryScannerTest, PrecedenceSafetyInternalDirsCannotBeUnignored) {
    create_file(".amoebaignore",
                "!.git\n"
                "!.git/**\n"
                "!.amoeba\n");

    create_file(".git/config.cpp", "not a real git file");
    create_file(".amoeba/cache.cpp", "cache file");
    create_file("valid.cpp", "int ok = 1;");

    const auto result = scanner.scan(test_dir);
    EXPECT_EQ(result.total_files_included(), 1U);
    ASSERT_EQ(result.files.size(), 1U);
    EXPECT_EQ(result.files[0].path.filename(), "valid.cpp");
}

TEST_F(RepositoryScannerTest, GitIgnoreIntegrationViaLibGit2) {
    // Initialize a real git repo in test_dir using libgit2
    git_libgit2_init();
    git_repository* repo = nullptr;
    ASSERT_EQ(git_repository_init(&repo, test_dir.string().c_str(), 0), 0);
    ASSERT_NE(repo, nullptr);

    create_file(".gitignore",
                "ignored_folder/\n"
                "*.gen.cpp\n"
                "!preserved.gen.cpp\n");

    create_file("normal.cpp", "int n = 1;");
    create_file("ignored_folder/sub.cpp", "int s = 2;");
    create_file("temp.gen.cpp", "int g = 3;");
    create_file("preserved.gen.cpp", "int p = 4;");

    amoeba::scanner::ScanOptions opts;
    opts.enable_git_ignore = true;
    opts.collect_skipped_details = true;

    const auto result = scanner.scan(test_dir, opts);

    git_repository_free(repo);
    git_libgit2_shutdown();

    // normal.cpp and preserved.gen.cpp should be included
    EXPECT_EQ(result.total_files_included(), 2U);
    EXPECT_GE(result.total_files_git_ignored, 1U);

    vector<string> included_names;
    for (const auto& f : result.files) {
        included_names.push_back(f.path.filename().string());
    }
    EXPECT_TRUE(ranges::find(included_names, "normal.cpp") != included_names.end());
    EXPECT_TRUE(ranges::find(included_names, "preserved.gen.cpp") != included_names.end());
    EXPECT_TRUE(ranges::find(included_names, "sub.cpp") == included_names.end());
    EXPECT_TRUE(ranges::find(included_names, "temp.gen.cpp") == included_names.end());
}

TEST_F(RepositoryScannerTest, GitIgnoreTrackedFilePrecedence) {
    // In Git-compatible mode, files tracked in Git index remain eligible even if matching .gitignore
    git_libgit2_init();
    git_repository* repo = nullptr;
    ASSERT_EQ(git_repository_init(&repo, test_dir.string().c_str(), 0), 0);
    ASSERT_NE(repo, nullptr);

    create_file(".gitignore", "*.tracked.cpp\n");
    create_file("app.tracked.cpp", "int tracked = 1;");
    create_file("other.tracked.cpp", "int untracked = 2;");

    // Add app.tracked.cpp to the git index
    git_index* index = nullptr;
    ASSERT_EQ(git_repository_index(&index, repo), 0);
    ASSERT_EQ(git_index_add_bypath(index, "app.tracked.cpp"), 0);
    ASSERT_EQ(git_index_write(index), 0);
    git_index_free(index);

    amoeba::scanner::ScanOptions opts;
    opts.enable_git_ignore = true;

    const auto result = scanner.scan(test_dir, opts);

    git_repository_free(repo);
    git_libgit2_shutdown();

    // app.tracked.cpp is tracked in Git, so it is NOT ignored!
    // other.tracked.cpp is untracked and matches .gitignore, so it IS ignored!
    EXPECT_EQ(result.total_files_included(), 1U);
    ASSERT_EQ(result.files.size(), 1U);
    EXPECT_EQ(result.files[0].path.filename(), "app.tracked.cpp");
}

TEST(RepositoryScannerStaticTest, HelperFilters) {
    EXPECT_TRUE(amoeba::scanner::RepositoryScanner::is_excluded_directory(".git"));
    EXPECT_TRUE(amoeba::scanner::RepositoryScanner::is_excluded_directory("node_modules"));
    EXPECT_TRUE(amoeba::scanner::RepositoryScanner::is_excluded_directory("build"));
    EXPECT_TRUE(amoeba::scanner::RepositoryScanner::is_excluded_directory("benchmarks"));
    EXPECT_FALSE(amoeba::scanner::RepositoryScanner::is_excluded_directory("src"));
    EXPECT_FALSE(amoeba::scanner::RepositoryScanner::is_excluded_directory("include"));

    EXPECT_TRUE(amoeba::scanner::RepositoryScanner::is_supported_extension(".cpp"));
    EXPECT_TRUE(amoeba::scanner::RepositoryScanner::is_supported_extension(".CPP"));
    EXPECT_TRUE(amoeba::scanner::RepositoryScanner::is_supported_extension(".ts"));
    EXPECT_TRUE(amoeba::scanner::RepositoryScanner::is_supported_extension(".py"));
    EXPECT_FALSE(amoeba::scanner::RepositoryScanner::is_supported_extension(".md"));
    EXPECT_FALSE(amoeba::scanner::RepositoryScanner::is_supported_extension(".png"));

    EXPECT_EQ(amoeba::scanner::RepositoryScanner::get_language_name(".cpp"), "C++");
    EXPECT_EQ(amoeba::scanner::RepositoryScanner::get_language_name(".py"), "Python");
    EXPECT_EQ(amoeba::scanner::RepositoryScanner::get_language_name(".ts"), "TypeScript");
    EXPECT_EQ(amoeba::scanner::RepositoryScanner::get_language_name(".unknown"), "Unknown");
}

}  // namespace
