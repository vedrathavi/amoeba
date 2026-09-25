#include "amoeba/index/search_engine.hpp"
#include "amoeba/parser/source_parser.hpp"

#include <gtest/gtest.h>

namespace {

using namespace std;
using namespace amoeba::index;
using namespace amoeba::parser;

class SearchEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        SourceParser parser;

        const string cpp_source = R"(
namespace amoeba::auth {

class UserAuthenticationService {
public:
    bool authenticateUser(const string& user);
    void logoutUser(const string& user);
};

}
)";

        const string tsx_source = R"(
import { useState } from 'react';

export function UserProfileCard({ userId }) {
    const [user, setUser] = useState(null);
    return <div className="user-card font-bold">Profile</div>;
}
)";

        const string route_source = "export default function Page() { return <div>Home</div>; }";

        index.add_parsed_file(parser.parse_source(cpp_source, "C++", "src/auth/service.cpp"));
        index.add_parsed_file(
            parser.parse_source(tsx_source, "TSX", "src/components/UserProfileCard.tsx"));
        index.add_parsed_file(parser.parse_source(route_source, "TSX", "app/dashboard/page.tsx"));
    }

    InvertedIndex index;
};

TEST_F(SearchEngineTest, ExactIdentifierSearch) {
    SearchEngine engine(index);

    const auto results = engine.search("authenticateUser");
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].element.name, "authenticateUser");
    EXPECT_EQ(results[0].file_path, "src/auth/service.cpp");
    EXPECT_TRUE(results[0].exact_name_match);
}

TEST_F(SearchEngineTest, SplitSubWordSearch) {
    SearchEngine engine(index);

    const auto results = engine.search("authenticate");
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].element.name, "authenticateUser");
}

TEST_F(SearchEngineTest, MultiTermSearchAnyMode) {
    SearchEngine engine(index);

    // AnyTerm: matches both UserProfileCard and UserAuthenticationService
    const auto results = engine.search("UserProfileCard authenticateUser",
                                       SearchOptions{.match_mode = MatchMode::AnyTerm});
    ASSERT_GE(results.size(), 2U);
}

TEST_F(SearchEngineTest, MultiTermSearchAllMode) {
    SearchEngine engine(index);

    // AllTerms: element must match both "user" and "authenticate"
    const auto results =
        engine.search("user authenticate", SearchOptions{.match_mode = MatchMode::AllTerms});
    ASSERT_GE(results.size(), 1U);
    EXPECT_EQ(results[0].element.name, "authenticateUser");
}

TEST_F(SearchEngineTest, ElementKindFilter) {
    SearchEngine engine(index);

    // Search for "user" filtering only Component elements
    const auto results = engine.search("user", SearchOptions{
                                                   .kind_filter = ElementKind::Component,
                                               });

    ASSERT_FALSE(results.empty());
    for (const auto& res : results) {
        EXPECT_EQ(res.element.kind, ElementKind::Component);
    }
}

TEST_F(SearchEngineTest, NextJSRouteSearch) {
    SearchEngine engine(index);

    const auto results = engine.search("dashboard");
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].file_path, "app/dashboard/page.tsx");
}

TEST_F(SearchEngineTest, NonExistentQuery) {
    SearchEngine engine(index);

    const auto results = engine.search("nonExistentSymbol12345");
    EXPECT_TRUE(results.empty());
}

TEST_F(SearchEngineTest, EmptyQuery) {
    SearchEngine engine(index);

    const auto results = engine.search("");
    EXPECT_TRUE(results.empty());

    const auto whitespace_results = engine.search("   ");
    EXPECT_TRUE(whitespace_results.empty());
}

}  // namespace
