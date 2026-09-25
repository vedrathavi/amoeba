#include "amoeba/index/inverted_index.hpp"
#include "amoeba/index/search_engine.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/scanner/repository_scanner.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>

namespace {

using namespace std;
using namespace std::filesystem;
using namespace amoeba::index;
using namespace amoeba::parser;
using namespace amoeba::scanner;

class IndexBenchmarkTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto timestamp = chrono::steady_clock::now().time_since_epoch().count();
        test_dir = temp_directory_path() / ("amoeba_bench_repo_" + to_string(timestamp));
        create_directories(test_dir / "src/auth");
        create_directories(test_dir / "src/components");
        create_directories(test_dir / "src/models");
        create_directories(test_dir / "app/dashboard");

        // Populate synthetic multi-language files
        for (int i = 0; i < 5; ++i) {
            create_file("src/auth/service_" + to_string(i) + ".cpp",
                        "namespace auth {\nclass AuthService" + to_string(i) +
                            " {\npublic:\n    void authenticateUser" + to_string(i) +
                            "(const string& u) { verify(); }\n};\n}");
            create_file("src/models/user_" + to_string(i) + ".py",
                        "class UserModel" + to_string(i) +
                            ":\n    def get_user_by_id(self, user_id):\n        return "
                            "fetch_record(user_id)\n");
            create_file("src/components/Card" + to_string(i) + ".tsx",
                        "import { useState } from 'react';\nexport function UserCard" +
                            to_string(i) +
                            "() {\n    const [val, setVal] = useState(0);\n    return <div "
                            "className=\"flex items-center px-4\">Card " +
                            to_string(i) + "</div>;\n}");
            create_file("app/dashboard/page_" + to_string(i) + ".tsx",
                        "export default function Page" + to_string(i) +
                            "() { return <div>Dashboard " + to_string(i) + "</div>; }");
        }
    }

    void TearDown() override {
        error_code ec;
        remove_all(test_dir, ec);
    }

    void create_file(const string& rel_path, string_view content) {
        const path file_path = test_dir / rel_path;
        ofstream file(file_path, ios::binary);
        file << content;
    }

    path test_dir;
};

TEST_F(IndexBenchmarkTest, MeasureIndexingAndRetrievalPerformance) {
    RepositoryScanner scanner;
    const auto scan_result = scanner.scan(test_dir);

    ASSERT_EQ(scan_result.files.size(), 20U);

    SourceParser parser;
    InvertedIndex index;

    const auto start_index = chrono::high_resolution_clock::now();

    for (const auto& file_info : scan_result.files) {
        try {
            const auto parsed = parser.parse_file(file_info.path);
            if (parsed.success) {
                index.add_parsed_file(parsed);
            }
        } catch (...) {
        }
    }

    const auto end_index = chrono::high_resolution_clock::now();
    const auto index_time_us =
        chrono::duration_cast<chrono::microseconds>(end_index - start_index).count();

    EXPECT_EQ(index.file_count(), 20U);
    EXPECT_GT(index.element_count(), 50U);
    EXPECT_GT(index.term_count(), 30U);
    EXPECT_GT(index.posting_count(), 100U);

    SearchEngine engine(index);

    // Warm-up and lookup benchmark
    const auto start_lookup = chrono::high_resolution_clock::now();
    const auto results = engine.search("authenticateUser");
    const auto end_lookup = chrono::high_resolution_clock::now();
    const auto lookup_time_us =
        chrono::duration_cast<chrono::microseconds>(end_lookup - start_lookup).count();

    EXPECT_FALSE(results.empty());

    cout << "\n[=== In-Memory Index Benchmark Metrics ===]\n"
         << "  Files Indexed:      " << index.file_count() << "\n"
         << "  Elements Indexed:   " << index.element_count() << "\n"
         << "  Distinct Terms:     " << index.term_count() << "\n"
         << "  Total Postings:     " << index.posting_count() << "\n"
         << "  Index Build Time:   " << index_time_us << " us (" << (index_time_us / 1000.0)
         << " ms)\n"
         << "  Lookup Time (Query: 'authenticateUser'): " << lookup_time_us << " us\n"
         << "  Matches Found:      " << results.size() << "\n"
         << "[========================================]\n\n";
}

}  // namespace
