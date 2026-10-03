#include "amoeba/semantic/semantic_index.hpp"
#include "amoeba/semantic/hnsw_semantic_index.hpp"
#include "amoeba/semantic/similarity.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace amoeba::semantic {

// ─────────────────────────────────────────────────────────────────────────────
// ExactSemanticIndex Implementation
// ─────────────────────────────────────────────────────────────────────────────

bool ExactSemanticIndex::add(const Embedding& embedding) {
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

bool ExactSemanticIndex::add(ElementId element_id, std::vector<float> values) {
    return add(Embedding{.element_id = element_id, .values = std::move(values)});
}

bool ExactSemanticIndex::replace(const Embedding& embedding) {
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

bool ExactSemanticIndex::remove(ElementId element_id) {
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

void ExactSemanticIndex::clear() noexcept {
    entries_.clear();
    dimensions_ = 0;
}

bool ExactSemanticIndex::contains(ElementId element_id) const noexcept {
    return entries_.contains(element_id);
}

const Embedding* ExactSemanticIndex::lookup(ElementId element_id) const noexcept {
    auto it = entries_.find(element_id);
    if (it != entries_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::size_t ExactSemanticIndex::size() const noexcept {
    return entries_.size();
}

bool ExactSemanticIndex::empty() const noexcept {
    return entries_.empty();
}

std::size_t ExactSemanticIndex::dimensions() const noexcept {
    return dimensions_;
}

std::vector<ElementId> ExactSemanticIndex::all_element_ids() const {
    std::vector<ElementId> ids;
    ids.reserve(entries_.size());
    for (const auto& [id, _] : entries_) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::size_t ExactSemanticIndex::estimate_memory_bytes() const noexcept {
    std::size_t bytes = sizeof(*this);
    bytes += entries_.bucket_count() * sizeof(void*);
    for (const auto& [_, emb] : entries_) {
        bytes += sizeof(ElementId) + sizeof(Embedding) + (emb.values.capacity() * sizeof(float));
    }
    return bytes;
}

std::vector<SemanticSearchResult>
ExactSemanticIndex::search(std::span<const float> query_embedding, std::size_t top_k,
                           float min_similarity) const {
    if (query_embedding.empty() || entries_.empty() || top_k == 0) {
        return {};
    }

    std::vector<SemanticSearchResult> scored_candidates;
    scored_candidates.reserve(entries_.size());

    for (const auto& [id, emb] : entries_) {
        float sim = cosine_similarity(query_embedding, std::span<const float>(emb.values));
        if (sim >= min_similarity) {
            scored_candidates.push_back(SemanticSearchResult{
                .element_id = id,
                .similarity_score = sim,
            });
        }
    }

    // Sort by similarity descending, with deterministic tie-breaking on element_id ascending
    std::sort(scored_candidates.begin(), scored_candidates.end(),
              [](const SemanticSearchResult& a, const SemanticSearchResult& b) {
                  if (std::abs(a.similarity_score - b.similarity_score) > 1e-6f) {
                      return a.similarity_score > b.similarity_score;
                  }
                  return a.element_id < b.element_id;
              });

    if (scored_candidates.size() > top_k) {
        scored_candidates.resize(top_k);
    }

    return scored_candidates;
}

bool ExactSemanticIndex::save(const std::filesystem::path& file_path) const {
    if (file_path.empty()) {
        return false;
    }
    std::ofstream os(file_path, std::ios::binary);
    if (!os) {
        return false;
    }

    const uint32_t magic = 0x414D4F45;  // "AMOE"
    const uint32_t version = 1;
    const uint32_t index_type = 1;      // Exact
    const uint32_t dims = static_cast<uint32_t>(dimensions_);
    const uint64_t count = static_cast<uint64_t>(entries_.size());

    os.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    os.write(reinterpret_cast<const char*>(&version), sizeof(version));
    os.write(reinterpret_cast<const char*>(&index_type), sizeof(index_type));
    os.write(reinterpret_cast<const char*>(&dims), sizeof(dims));
    os.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& [id, emb] : entries_) {
        os.write(reinterpret_cast<const char*>(&id), sizeof(id));
        if (dims > 0 && emb.values.size() == dims) {
            os.write(reinterpret_cast<const char*>(emb.values.data()), sizeof(float) * dims);
        }
    }

    return os.good();
}

bool ExactSemanticIndex::load(const std::filesystem::path& file_path) {
    if (!std::filesystem::exists(file_path)) {
        return false;
    }
    std::ifstream is(file_path, std::ios::binary);
    if (!is) {
        return false;
    }

    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t index_type = 0;
    uint32_t dims = 0;
    uint64_t count = 0;

    is.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    is.read(reinterpret_cast<char*>(&version), sizeof(version));
    is.read(reinterpret_cast<char*>(&index_type), sizeof(index_type));
    is.read(reinterpret_cast<char*>(&dims), sizeof(dims));
    is.read(reinterpret_cast<char*>(&count), sizeof(count));

    if (magic != 0x414D4F45 || version != 1 || index_type != 1) {
        return false;
    }

    clear();
    dimensions_ = dims;

    std::vector<float> buf(dims);
    for (uint64_t i = 0; i < count; ++i) {
        ElementId id = 0;
        is.read(reinterpret_cast<char*>(&id), sizeof(id));
        if (dims > 0) {
            is.read(reinterpret_cast<char*>(buf.data()), sizeof(float) * dims);
        }
        if (!is) {
            clear();
            return false;
        }
        entries_.emplace(id, Embedding{.element_id = id, .values = buf});
    }

    return is.good() || is.eof();
}

// ─────────────────────────────────────────────────────────────────────────────
// Factory
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<ISemanticVectorIndex>
create_semantic_index(SemanticIndexType type, const HnswConfig& hnsw_config) {
    switch (type) {
    case SemanticIndexType::Exact:
        return std::make_unique<ExactSemanticIndex>();
    case SemanticIndexType::Hnsw:
        return std::make_unique<HnswSemanticIndex>(hnsw_config);
    }
    return std::make_unique<ExactSemanticIndex>();
}

}  // namespace amoeba::semantic
