#include "amoeba/scanner/repository_scanner.hpp"

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

TEST(RepositoryScannerStaticTest, HelperFilters) {
    EXPECT_TRUE(amoeba::scanner::RepositoryScanner::is_excluded_directory(".git"));
    EXPECT_TRUE(amoeba::scanner::RepositoryScanner::is_excluded_directory("node_modules"));
    EXPECT_TRUE(amoeba::scanner::RepositoryScanner::is_excluded_directory("build"));
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
