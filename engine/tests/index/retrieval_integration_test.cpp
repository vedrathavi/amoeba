#include "amoeba/index/inverted_index.hpp"
#include "amoeba/index/search_engine.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/scanner/repository_scanner.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace {

using namespace std;
using namespace std::filesystem;
using namespace amoeba::index;
using namespace amoeba::parser;
using namespace amoeba::scanner;

class RetrievalIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto timestamp = chrono::steady_clock::now().time_since_epoch().count();
        test_dir = temp_directory_path() / ("amoeba_integration_repo_" + to_string(timestamp));

        create_directories(test_dir / "src/auth");
        create_directories(test_dir / "src/users");
        create_directories(test_dir / "src/components");
        create_directories(test_dir / "app/routes");

        create_file("src/auth/auth_service.cpp", R"(
#include "auth_service.hpp"
namespace amoeba::auth {
class AuthenticationService {
public:
    bool loginUser(const string& username, const string& password) {
        verifyCredentials(username, password);
        return true;
    }
    void logoutUser() {
        clearSession();
    }
};
}
)");

        create_file("src/users/user_model.py", R"(
import json

class UserModel:
    def __init__(self, user_id, username):
        self.user_id = user_id
        self.username = username

    def get_user_by_id(self, user_id):
        return db_query_user(user_id)
)");

        create_file("src/components/UserProfile.tsx", R"(
import React, { useState, useEffect } from 'react';
import { Button } from '@/components/ui/button';

export function UserProfileCard({ user }) {
    const [isEditing, setIsEditing] = useState(false);
    useEffect(() => {
        loadUserProfile();
    }, []);

    return (
        <div className="flex items-center justify-between p-4">
            <span className="font-bold">{user.name}</span>
            <Button onClick={() => setIsEditing(!isEditing)}>Edit</Button>
        </div>
    );
}
)");

        create_file("app/routes/page.tsx", R"(
export default function DashboardPage() {
    return <div>Dashboard Route</div>;
}
)");

        // Execute full pipeline: Scan -> Parse -> Index
        RepositoryScanner scanner;
        const auto scan_result = scanner.scan(test_dir);

        SourceParser parser;
        for (const auto& file_info : scan_result.files) {
            const auto parsed = parser.parse_file(file_info.path);
            if (parsed.success) {
                index.add_parsed_file(parsed);
            }
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
    InvertedIndex index;
};

TEST_F(RetrievalIntegrationTest, EndToEndExactIdentifierSearch) {
    SearchEngine engine(index);

    const auto results = engine.search("AuthenticationService");
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].element.name, "AuthenticationService");
    EXPECT_EQ(results[0].element.kind, ElementKind::Class);
    EXPECT_TRUE(results[0].exact_name_match);
}

TEST_F(RetrievalIntegrationTest, EndToEndSubWordSearch) {
    SearchEngine engine(index);

    // Searching for "login" should find loginUser method
    const auto results = engine.search("login");
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].element.name, "loginUser");
    EXPECT_EQ(results[0].element.parent_context, "AuthenticationService");
}

TEST_F(RetrievalIntegrationTest, EndToEndMultiLanguageSearch) {
    SearchEngine engine(index);

    // Python function
    const auto py_results = engine.search("get_user_by_id");
    ASSERT_FALSE(py_results.empty());
    EXPECT_EQ(py_results[0].element.name, "get_user_by_id");
    EXPECT_EQ(py_results[0].language, "Python");

    // React/TSX Component
    const auto tsx_results = engine.search("UserProfileCard");
    ASSERT_FALSE(tsx_results.empty());
    EXPECT_EQ(tsx_results[0].element.name, "UserProfileCard");
    EXPECT_EQ(tsx_results[0].element.kind, ElementKind::Component);
}

TEST_F(RetrievalIntegrationTest, EndToEndAllTermsConstraint) {
    SearchEngine engine(index);

    // Both "user" and "profile" must match
    const auto results = engine.search("user profile", SearchOptions{
                                                           .match_mode = MatchMode::AllTerms,
                                                       });

    ASSERT_FALSE(results.empty());
    for (const auto& res : results) {
        // Result must match both words either in name, context, or path
        EXPECT_GE(res.match_count, 2U);
    }
}

TEST_F(RetrievalIntegrationTest, EndToEndElementKindFilter) {
    SearchEngine engine(index);

    const auto results = engine.search("user", SearchOptions{
                                                   .kind_filter = ElementKind::Method,
                                               });

    ASSERT_FALSE(results.empty());
    for (const auto& res : results) {
        EXPECT_EQ(res.element.kind, ElementKind::Method);
    }
}

}  // namespace
