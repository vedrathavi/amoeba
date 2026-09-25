#pragma once

#include "amoeba/parser/code_element.hpp"
#include "amoeba/parser/parsed_file.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace amoeba::index {

using namespace std;
using namespace std::filesystem;

using FileId = uint32_t;
using ElementId = uint32_t;

/**
 * @brief Metadata for a parsed file stored in the index.
 */
struct IndexedFile {
    path file_path;
    string language;
};

/**
 * @brief Representation of an indexed code element mapped to its source file.
 */
struct IndexedElement {
    parser::CodeElement element;
    FileId file_id{0};
};

/**
 * @brief In-memory inverted index mapping normalized terms to code element IDs.
 */
class InvertedIndex {
public:
    InvertedIndex() = default;
    ~InvertedIndex() = default;

    InvertedIndex(const InvertedIndex&) = default;
    InvertedIndex& operator=(const InvertedIndex&) = default;
    InvertedIndex(InvertedIndex&&) noexcept = default;
    InvertedIndex& operator=(InvertedIndex&&) noexcept = default;

    /**
     * @brief Indexes all code elements and file metadata from a ParsedFile.
     * @return The unique FileId assigned to this file.
     */
    FileId add_parsed_file(const parser::ParsedFile& parsed_file);

    /**
     * @brief Looks up a normalized search term in the index.
     * @param term Normalized term string.
     * @return Pointer to vector of ElementIds if found, nullptr otherwise.
     */
    [[nodiscard]] const vector<ElementId>* lookup(string_view term) const;

    /**
     * @brief Checks if a term exists in the index.
     */
    [[nodiscard]] bool contains(string_view term) const;

    /**
     * @brief Retrieves an indexed element by its unique ElementId.
     * @throws out_of_range if ElementId is invalid.
     */
    [[nodiscard]] const IndexedElement& get_element(ElementId id) const;

    /**
     * @brief Retrieves an indexed file by its unique FileId.
     * @throws out_of_range if FileId is invalid.
     */
    [[nodiscard]] const IndexedFile& get_file(FileId id) const;

    /**
     * @brief Number of files currently indexed.
     */
    [[nodiscard]] size_t file_count() const noexcept { return files_.size(); }

    /**
     * @brief Number of code elements currently indexed.
     */
    [[nodiscard]] size_t element_count() const noexcept { return elements_.size(); }

    /**
     * @brief Number of distinct terms in the index dictionary.
     */
    [[nodiscard]] size_t term_count() const noexcept { return postings_.size(); }

    /**
     * @brief Total number of postings (term-to-element postings) across all terms.
     */
    [[nodiscard]] size_t posting_count() const noexcept { return total_postings_; }

    /**
     * @brief Resets and empties all index data.
     */
    void clear() noexcept;

private:
    vector<IndexedFile> files_;
    vector<IndexedElement> elements_;
    unordered_map<string, vector<ElementId>> postings_;
    size_t total_postings_{0};

    void add_term_posting(string_view term, ElementId element_id);
};

}  // namespace amoeba::index
