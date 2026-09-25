#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/source_parser.hpp"

#include <gtest/gtest.h>

namespace {

using namespace std;
using namespace amoeba::index;
using namespace amoeba::parser;

TEST(InvertedIndexTest, EmptyIndexState) {
    InvertedIndex index;
    EXPECT_EQ(index.file_count(), 0U);
    EXPECT_EQ(index.element_count(), 0U);
    EXPECT_EQ(index.term_count(), 0U);
    EXPECT_EQ(index.posting_count(), 0U);
    EXPECT_FALSE(index.contains("user"));
    EXPECT_EQ(index.lookup("user"), nullptr);
}

TEST(InvertedIndexTest, IndexParsedFileAndLookup) {
    SourceParser parser;
    const string source = R"(
class UserService {
public:
    void authenticateUser(const string& user);
};
)";

    const auto parsed = parser.parse_source(source, "C++", "src/user_service.cpp");

    InvertedIndex index;
    const FileId fid = index.add_parsed_file(parsed);

    EXPECT_EQ(fid, 0U);
    EXPECT_EQ(index.file_count(), 1U);
    EXPECT_GE(index.element_count(), 2U);
    EXPECT_GE(index.term_count(), 5U);
    EXPECT_GE(index.posting_count(), 5U);

    // Exact identifier lookup
    const auto* class_postings = index.lookup("userservice");
    ASSERT_NE(class_postings, nullptr);
    ASSERT_FALSE(class_postings->empty());

    // Sub-token lookup
    const auto* user_postings = index.lookup("user");
    ASSERT_NE(user_postings, nullptr);
    EXPECT_GE(user_postings->size(), 2U);  // matches both UserService and authenticateUser

    const auto* auth_postings = index.lookup("authenticate");
    ASSERT_NE(auth_postings, nullptr);
    ASSERT_EQ(auth_postings->size(), 1U);

    // Verify element retrieval
    const auto& elem = index.get_element((*auth_postings)[0]);
    EXPECT_EQ(elem.element.name, "authenticateUser");
    EXPECT_EQ(elem.element.parent_context, "UserService");
    EXPECT_EQ(elem.file_id, 0U);

    // Verify file retrieval
    const auto& file = index.get_file(elem.file_id);
    EXPECT_EQ(file.file_path, "src/user_service.cpp");
    EXPECT_EQ(file.language, "C++");
}

TEST(InvertedIndexTest, MultipleFilesAndClear) {
    SourceParser parser;
    const auto parsed1 = parser.parse_source("void compute();", "C++", "src/math.cpp");
    const auto parsed2 = parser.parse_source("def compute(): pass", "Python", "src/calc.py");

    InvertedIndex index;
    index.add_parsed_file(parsed1);
    index.add_parsed_file(parsed2);

    EXPECT_EQ(index.file_count(), 2U);
    const auto* compute_postings = index.lookup("compute");
    ASSERT_NE(compute_postings, nullptr);
    EXPECT_EQ(compute_postings->size(), 2U);

    index.clear();
    EXPECT_EQ(index.file_count(), 0U);
    EXPECT_EQ(index.element_count(), 0U);
    EXPECT_EQ(index.term_count(), 0U);
    EXPECT_EQ(index.posting_count(), 0U);
    EXPECT_EQ(index.lookup("compute"), nullptr);
}

TEST(InvertedIndexTest, ThrowsOnInvalidIDs) {
    InvertedIndex index;
    EXPECT_THROW((void)index.get_element(999), out_of_range);
    EXPECT_THROW((void)index.get_file(999), out_of_range);
}

}  // namespace
