#include "amoeba/index/inverted_index.hpp"

#include "amoeba/index/code_tokenizer.hpp"

#include <stdexcept>
#include <unordered_set>

namespace amoeba::index {

using namespace std;

FileId InvertedIndex::add_parsed_file(const parser::ParsedFile& parsed_file) {
    const FileId file_id = static_cast<FileId>(files_.size());
    files_.push_back(IndexedFile{
        .file_path = parsed_file.file_path,
        .language = parsed_file.language,
    });

    const auto path_tokens =
        CodeTokenizer::tokenize_path(parsed_file.file_path.generic_string());

    for (const auto& elem : parsed_file.elements) {
        const ElementId elem_id = static_cast<ElementId>(elements_.size());
        elements_.push_back(IndexedElement{
            .element = elem,
            .file_id = file_id,
        });

        unordered_set<string> unique_elem_terms;
        size_t raw_token_count = 0;

        // 1. Name tokens
        const auto name_tokens = CodeTokenizer::tokenize_identifier(elem.name);
        raw_token_count += name_tokens.size();
        for (const auto& token : name_tokens) {
            unique_elem_terms.insert(token);
        }

        // 2. Parent context tokens
        if (!elem.parent_context.empty()) {
            const auto context_tokens = CodeTokenizer::tokenize_identifier(elem.parent_context);
            raw_token_count += context_tokens.size();
            for (const auto& token : context_tokens) {
                unique_elem_terms.insert(token);
            }
        }

        // 3. Detail tokens
        if (!elem.detail.empty()) {
            const auto detail_tokens = CodeTokenizer::tokenize_identifier(elem.detail);
            raw_token_count += detail_tokens.size();
            for (const auto& token : detail_tokens) {
                unique_elem_terms.insert(token);
            }
        }

        // 4. File path tokens
        raw_token_count += path_tokens.size();
        for (const auto& token : path_tokens) {
            unique_elem_terms.insert(token);
        }

        element_lengths_.push_back(raw_token_count);
        total_element_tokens_ += raw_token_count;

        for (const auto& term : unique_elem_terms) {
            add_term_posting(term, elem_id);
        }
    }

    return file_id;
}

void InvertedIndex::add_term_posting(string_view term, ElementId element_id) {
    string norm_term = CodeTokenizer::normalize_term(term);
    if (norm_term.empty()) {
        return;
    }

    auto& posting_list = postings_[norm_term];
    if (posting_list.empty() || posting_list.back() != element_id) {
        posting_list.push_back(element_id);
        total_postings_++;
    }
}

const vector<ElementId>* InvertedIndex::lookup(string_view term) const {
    string norm_term = CodeTokenizer::normalize_term(term);
    if (norm_term.empty()) {
        return nullptr;
    }

    const auto it = postings_.find(norm_term);
    if (it != postings_.end()) {
        return &(it->second);
    }
    return nullptr;
}

bool InvertedIndex::contains(string_view term) const {
    string norm_term = CodeTokenizer::normalize_term(term);
    if (norm_term.empty()) {
        return false;
    }
    return postings_.contains(norm_term);
}

const IndexedElement& InvertedIndex::get_element(ElementId id) const {
    if (id >= elements_.size()) {
        throw out_of_range("ElementId out of range: " + to_string(id));
    }
    return elements_[id];
}

const IndexedFile& InvertedIndex::get_file(FileId id) const {
    if (id >= files_.size()) {
        throw out_of_range("FileId out of range: " + to_string(id));
    }
    return files_[id];
}

size_t InvertedIndex::document_frequency(string_view term) const noexcept {
    string norm_term = CodeTokenizer::normalize_term(term);
    if (norm_term.empty()) {
        return 0;
    }
    const auto it = postings_.find(norm_term);
    if (it != postings_.end()) {
        return it->second.size();
    }
    return 0;
}

double InvertedIndex::avg_element_length() const noexcept {
    if (elements_.empty()) {
        return 0.0;
    }
    return static_cast<double>(total_element_tokens_) / static_cast<double>(elements_.size());
}

size_t InvertedIndex::get_element_length(ElementId id) const {
    if (id >= element_lengths_.size()) {
        throw out_of_range("ElementId out of range: " + to_string(id));
    }
    return element_lengths_[id];
}

void InvertedIndex::clear() noexcept {
    files_.clear();
    elements_.clear();
    element_lengths_.clear();
    postings_.clear();
    total_postings_ = 0;
    total_element_tokens_ = 0;
}

vector<string_view> InvertedIndex::all_terms() const {
    vector<string_view> terms;
    terms.reserve(postings_.size());
    for (const auto& [term, _] : postings_) {
        terms.push_back(term);
    }
    return terms;
}

}  // namespace amoeba::index
