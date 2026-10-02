#include "amoeba/scanner/repository_scanner.hpp"
#include "amoeba/parser/source_parser.hpp"

#include <gtest/gtest.h>
#include <filesystem>
#include <vector>
#include <string>
#include <iostream>

namespace {

using namespace amoeba;
namespace fs = std::filesystem;

class BenchmarkCompatibilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        repos_root = fs::current_path() / "benchmarks" / "repoprobe_v1" / "repos";
        has_repos = fs::exists(repos_root);
    }

    fs::path repos_root;
    bool has_repos{false};
    scanner::RepositoryScanner scanner;
    parser::SourceParser parser;
};

TEST_F(BenchmarkCompatibilityTest, ScanAndParseAllTenRepositories) {
    if (!has_repos) {
        GTEST_SKIP() << "Benchmark repos directory not found at " << repos_root;
    }

    const std::vector<std::string> repo_slugs = {
        "ducklake",
        "adk-python",
        "better-auth",
        "dokploy",
        "pkl",
        "beszel",
        "opencloud",
        "jiff",
        "walker",
        "docling"
    };

    std::size_t total_scanned_files = 0;
    std::size_t total_parsed_files = 0;
    std::size_t total_extracted_elements = 0;

    for (const auto& slug : repo_slugs) {
        const auto repo_path = repos_root / slug;
        ASSERT_TRUE(fs::exists(repo_path)) << "Repo does not exist: " << repo_path;

        const auto scan_result = scanner.scan(repo_path);
        EXPECT_GT(scan_result.total_files_included(), 0U) << "No files included for repo: " << slug;
        total_scanned_files += scan_result.total_files_included();

        std::size_t repo_parsed = 0;
        std::size_t repo_elements = 0;

        // Parse up to first 50 files per repository to verify parser compatibility across all 10 repos
        const std::size_t sample_limit = std::min(scan_result.files.size(), std::size_t{50});
        for (std::size_t i = 0; i < sample_limit; ++i) {
            const auto& file_meta = scan_result.files[i];
            const auto pf = parser.parse_file(file_meta.path);
            if (pf.success) {
                repo_parsed++;
                repo_elements += pf.elements.size();
            }
        }

        EXPECT_GT(repo_parsed, 0U) << "Failed to parse files for repo: " << slug;
        EXPECT_GT(repo_elements, 0U) << "No AST elements extracted for repo: " << slug;

        total_parsed_files += repo_parsed;
        total_extracted_elements += repo_elements;
    }

    std::cout << "[Benchmark Compatibility Report]\n"
              << "  Repositories Verified:     " << repo_slugs.size() << "\n"
              << "  Total Included Files:      " << total_scanned_files << "\n"
              << "  Sample Parsed Files:       " << total_parsed_files << "\n"
              << "  Sample Extracted Elements: " << total_extracted_elements << "\n";

    EXPECT_GT(total_scanned_files, 100U);
    EXPECT_GT(total_parsed_files, 50U);
    EXPECT_GT(total_extracted_elements, 200U);
}

TEST_F(BenchmarkCompatibilityTest, ParseOpenCloudDiagnostic) {
    if (!has_repos) GTEST_SKIP();
    const auto repo_path = repos_root / "opencloud";
    const auto scan_result = scanner.scan(repo_path);
    std::cout << "opencloud discovered: " << scan_result.files.size() << " files.\n";

    std::size_t parsed_count = 0;
    for (std::size_t i = 0; i < scan_result.files.size(); ++i) {
        const auto& file_meta = scan_result.files[i];
        if (i % 1000 == 0) {
            std::cout << "  parsing file " << i << " / " << scan_result.files.size() << " : " << file_meta.path.string() << "\n";
        }
        auto pf = parser.parse_file(file_meta.path);
        if (pf.success) {
            parsed_count++;
        }
    }
    std::cout << "opencloud parsed: " << parsed_count << " / " << scan_result.files.size() << " files.\n";
    EXPECT_GT(parsed_count, 1000U);
}

}  // namespace

