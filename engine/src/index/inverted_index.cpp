#include "amoeba/index/inverted_index.hpp"

#include "amoeba/index/code_tokenizer.hpp"

#include <stdexcept>
#include <unordered_set>

namespace amoeba::index {

using namespace std;

FileId InvertedIndex::add_parsed_file(const parser::ParsedFile& parsed_file) {
    const auto file_id = static_cast<FileId>(files_.size());
    files_.push_back(IndexedFile{
        .file_path = parsed_file.file_path,
        .language = parsed_file.language,
    });

    const string path_str = parsed_file.file_path.generic_string();
    const auto path_tokens = CodeTokenizer::tokenize_path(path_str);

    for (const auto& elem : parsed_file.elements) {
        const auto element_id = static_cast<ElementId>(elements_.size());
        elements_.push_back(IndexedElement{
            .element = elem,
            .file_id = file_id,
        });

        unordered_set<string> indexed_for_elem;

        // 1. Index identifier name
        const auto name_tokens = CodeTokenizer::tokenize_identifier(elem.name);
        for (const auto& t : name_tokens) {
            if (!indexed_for_elem.contains(t)) {
                add_term_posting(t, element_id);
                indexed_for_elem.insert(t);
            }
        }

        // 2. Index parent context (e.g. class, namespace, component)
        if (!elem.parent_context.empty()) {
            const auto context_tokens = CodeTokenizer::tokenize_identifier(elem.parent_context);
            for (const auto& t : context_tokens) {
                if (!indexed_for_elem.contains(t)) {
                    add_term_posting(t, element_id);
                    indexed_for_elem.insert(t);
                }
            }
        }

        // 3. Index detail for specific metadata-rich kinds (routes, utility classes, properties)
        if (!elem.detail.empty() && (elem.kind == parser::ElementKind::Route ||
                                     elem.kind == parser::ElementKind::UtilityClass ||
                                     elem.kind == parser::ElementKind::Property ||
                                     elem.kind == parser::ElementKind::Component)) {
            const auto detail_tokens = CodeTokenizer::tokenize_identifier(elem.detail);
            for (const auto& t : detail_tokens) {
                if (!indexed_for_elem.contains(t)) {
                    add_term_posting(t, element_id);
                    indexed_for_elem.insert(t);
                }
            }
        }

        // 4. Index path tokens for the element so path queries find symbols in that path
        for (const auto& pt : path_tokens) {
            if (!indexed_for_elem.contains(pt)) {
                add_term_posting(pt, element_id);
                indexed_for_elem.insert(pt);
            }
        }
    }

    return file_id;
}

void InvertedIndex::add_term_posting(string_view term, ElementId element_id) {
    const string norm = CodeTokenizer::normalize_term(term);
    if (norm.empty()) {
        return;
    }

    auto& list = postings_[norm];
    if (list.empty() || list.back() != element_id) {
        list.push_back(element_id);
        total_postings_++;
    }
}

const vector<ElementId>* InvertedIndex::lookup(string_view term) const {
    const string norm = CodeTokenizer::normalize_term(term);
    if (norm.empty()) {
        return nullptr;
    }

    const auto it = postings_.find(norm);
    if (it != postings_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool InvertedIndex::contains(string_view term) const {
    const string norm = CodeTokenizer::normalize_term(term);
    if (norm.empty()) {
        return false;
    }
    return postings_.contains(norm);
}

const IndexedElement& InvertedIndex::get_element(ElementId id) const {
    if (id >= elements_.size()) {
        throw out_of_range("Invalid ElementId: " + to_string(id));
    }
    return elements_[id];
}

const IndexedFile& InvertedIndex::get_file(FileId id) const {
    if (id >= files_.size()) {
        throw out_of_range("Invalid FileId: " + to_string(id));
    }
    return files_[id];
}

void InvertedIndex::clear() noexcept {
    files_.clear();
    elements_.clear();
    postings_.clear();
    total_postings_ = 0;
}

}  // namespace amoeba::index
