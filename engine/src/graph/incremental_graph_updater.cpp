#include "amoeba/graph/incremental_graph_updater.hpp"

#include <algorithm>

namespace amoeba::graph {

IncrementalGraphUpdater::IncrementalGraphUpdater(index::InvertedIndex& index,
                                                 RelationshipGraph& graph,
                                                 parser::SourceParser& parser)
    : index_(index), graph_(graph), parser_(parser) {}

bool IncrementalGraphUpdater::has_file(const std::filesystem::path& file_path) const noexcept {
    auto norm = file_path.lexically_normal().string();
    return file_contents_.contains(norm);
}

void IncrementalGraphUpdater::rebuild_internal() {
    index_.clear();
    graph_.clear();

    for (const auto& file : files_) {
        index_.add_parsed_file(file);
    }
    RepositoryGraphBuilder::build_repository_graph(index_, graph_);
}

IncrementalUpdateResult IncrementalGraphUpdater::add_file(const std::filesystem::path& file_path,
                                                          std::string_view source) {
    IncrementalUpdateResult result;

    auto norm_path = file_path.lexically_normal().string();
    if (file_contents_.contains(norm_path)) {
        return replace_file(file_path, source);
    }

    auto lang = parser::SourceParser::detect_language(file_path);
    if (lang.empty() || lang == "Unknown") {
        result.success = false;
        result.message = "unsupported_or_unknown_language";
        return result;
    }

    try {
        auto parsed = parser_.parse_source(source, lang, file_path);
        if (!parsed.success) {
            result.success = false;
            result.message = "parse_unsuccessful";
            return result;
        }

        std::size_t old_elems = index_.element_count();
        std::size_t old_rels = graph_.relationship_count();

        files_.push_back(parsed);
        file_contents_[norm_path] = std::string(source);

        rebuild_internal();

        result.success = true;
        result.elements_added =
            (index_.element_count() >= old_elems) ? (index_.element_count() - old_elems) : 0;
        result.relationships_added = (graph_.relationship_count() >= old_rels)
                                         ? (graph_.relationship_count() - old_rels)
                                         : 0;
        result.message = "file_added_successfully";
    } catch (const std::exception& e) {
        result.success = false;
        result.message = std::string("parse_exception: ") + e.what();
    }

    return result;
}

IncrementalUpdateResult
IncrementalGraphUpdater::replace_file(const std::filesystem::path& file_path,
                                      std::string_view new_source) {
    IncrementalUpdateResult result;

    auto norm_path = file_path.lexically_normal().string();
    auto it = std::find_if(files_.begin(), files_.end(), [&](const parser::ParsedFile& f) {
        return f.file_path.lexically_normal().string() == norm_path;
    });

    if (it == files_.end()) {
        return add_file(file_path, new_source);
    }

    if (auto cit = file_contents_.find(norm_path); cit != file_contents_.end()) {
        if (cit->second == new_source) {
            result.success = true;
            result.message = "file_unchanged";
            return result;
        }
    }

    auto lang = parser::SourceParser::detect_language(file_path);
    if (lang.empty() || lang == "Unknown") {
        result.success = false;
        result.message = "unsupported_language_replacement_rejected";
        return result;
    }

    try {
        auto parsed = parser_.parse_source(new_source, lang, file_path);
        if (!parsed.success) {
            result.success = false;
            result.message = "malformed_parse_rejected_previous_state_preserved";
            return result;
        }

        std::size_t old_file_elems = it->elements.size();
        std::size_t new_file_elems = parsed.elements.size();

        *it = parsed;
        file_contents_[norm_path] = std::string(new_source);

        rebuild_internal();

        result.success = true;
        result.elements_removed = old_file_elems;
        result.elements_added = new_file_elems;
        result.message = "file_replaced_successfully";
    } catch (const std::exception& e) {
        result.success = false;
        result.message = std::string("replacement_failed_previous_state_preserved: ") + e.what();
    }

    return result;
}

IncrementalUpdateResult
IncrementalGraphUpdater::remove_file(const std::filesystem::path& file_path) {
    IncrementalUpdateResult result;

    auto norm_path = file_path.lexically_normal().string();
    auto it = std::find_if(files_.begin(), files_.end(), [&](const parser::ParsedFile& f) {
        return f.file_path.lexically_normal().string() == norm_path;
    });

    if (it == files_.end()) {
        result.success = false;
        result.message = "file_not_found";
        return result;
    }

    std::size_t old_elems = it->elements.size();
    std::size_t old_rels = graph_.relationship_count();

    files_.erase(it);
    file_contents_.erase(norm_path);

    rebuild_internal();

    result.success = true;
    result.elements_removed = old_elems;
    result.relationships_removed =
        (old_rels >= graph_.relationship_count()) ? (old_rels - graph_.relationship_count()) : 0;
    result.message = "file_removed_successfully";

    return result;
}

}  // namespace amoeba::graph
