#include "amoeba/semantic/semantic_index.hpp"

#include <algorithm>

namespace amoeba::semantic {

bool SemanticIndex::add(const Embedding& embedding) {
    if (embedding.empty() || entries_.contains(embedding.element_id)) {
        return false;
    }

    if (dimensions_ == 0) {
        dimensions_ = embedding.dimensions();
    } else if (embedding.dimensions() != dimensions_) {
        return false;
    }

    entries_.emplace(embedding.element_id, embedding);
    return true;
}

bool SemanticIndex::add(ElementId element_id, std::vector<float> values) {
    return add(Embedding{.element_id = element_id, .values = std::move(values)});
}

bool SemanticIndex::replace(const Embedding& embedding) {
    if (embedding.empty()) {
        return false;
    }

    if (dimensions_ == 0) {
        dimensions_ = embedding.dimensions();
    } else if (embedding.dimensions() != dimensions_) {
        return false;
    }

    bool existed = entries_.contains(embedding.element_id);
    entries_[embedding.element_id] = embedding;
    return existed;
}

bool SemanticIndex::remove(ElementId element_id) {
    auto it = entries_.find(element_id);
    if (it != entries_.end()) {
        entries_.erase(it);
        if (entries_.empty()) {
            dimensions_ = 0;
        }
        return true;
    }
    return false;
}

void SemanticIndex::clear() noexcept {
    entries_.clear();
    dimensions_ = 0;
}

bool SemanticIndex::contains(ElementId element_id) const noexcept {
    return entries_.contains(element_id);
}

const Embedding* SemanticIndex::lookup(ElementId element_id) const noexcept {
    auto it = entries_.find(element_id);
    if (it != entries_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::size_t SemanticIndex::size() const noexcept {
    return entries_.size();
}

bool SemanticIndex::empty() const noexcept {
    return entries_.empty();
}

std::size_t SemanticIndex::dimensions() const noexcept {
    return dimensions_;
}

std::vector<ElementId> SemanticIndex::all_element_ids() const {
    std::vector<ElementId> ids;
    ids.reserve(entries_.size());
    for (const auto& [id, _] : entries_) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

const std::unordered_map<ElementId, Embedding>& SemanticIndex::entries() const noexcept {
    return entries_;
}

std::size_t SemanticIndex::estimate_memory_bytes() const noexcept {
    std::size_t bytes = sizeof(*this);
    bytes += entries_.bucket_count() * sizeof(void*);
    for (const auto& [_, emb] : entries_) {
        bytes += sizeof(ElementId) + sizeof(Embedding) + (emb.values.capacity() * sizeof(float));
    }
    return bytes;
}

}  // namespace amoeba::semantic
