#include "amoeba/semantic/hnsw_semantic_index.hpp"
#include "amoeba/semantic/similarity.hpp"

#include <hnswlib/hnswlib.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <vector>

namespace amoeba::semantic {

struct HnswSemanticIndex::Impl {
    HnswConfig config;
    std::size_t dimensions{0};

    std::unique_ptr<hnswlib::InnerProductSpace> space;
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> hnsw;
    std::size_t current_capacity{0};

    std::unordered_map<ElementId, Embedding> entries;
    std::vector<ElementId> label_to_element_id;
    std::unordered_map<ElementId, hnswlib::labeltype> element_id_to_label;

    explicit Impl(const HnswConfig& cfg) : config(cfg) {}

    void init_hnsw_if_needed(std::size_t dim) {
        if (!hnsw) {
            dimensions = dim;
            space = std::make_unique<hnswlib::InnerProductSpace>(dim);
            current_capacity = std::max(config.initial_capacity, std::size_t{100});
            hnsw = std::make_unique<hnswlib::HierarchicalNSW<float>>(
                space.get(), current_capacity, config.m, config.ef_construction,
                config.random_seed);
            hnsw->setEf(config.ef_search);
        }
    }

    void ensure_capacity(std::size_t needed) {
        if (!hnsw) {
            return;
        }
        if (needed >= current_capacity) {
            std::size_t new_cap = std::max(needed + 100, current_capacity * 2);
            hnsw->resizeIndex(new_cap);
            current_capacity = new_cap;
        }
    }
};

HnswSemanticIndex::HnswSemanticIndex(const HnswConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

HnswSemanticIndex::~HnswSemanticIndex() = default;

HnswSemanticIndex::HnswSemanticIndex(HnswSemanticIndex&&) noexcept = default;
HnswSemanticIndex& HnswSemanticIndex::operator=(HnswSemanticIndex&&) noexcept = default;

bool HnswSemanticIndex::add(const Embedding& embedding) {
    if (embedding.empty() || impl_->entries.contains(embedding.element_id)) {
        return false;
    }

    if (impl_->dimensions == 0) {
        impl_->init_hnsw_if_needed(embedding.dimensions());
    } else if (embedding.dimensions() != impl_->dimensions) {
        return false;
    }

    const auto label = static_cast<hnswlib::labeltype>(impl_->label_to_element_id.size());
    impl_->ensure_capacity(impl_->label_to_element_id.size() + 1);

    impl_->label_to_element_id.push_back(embedding.element_id);
    impl_->element_id_to_label.emplace(embedding.element_id, label);
    impl_->entries.emplace(embedding.element_id, embedding);

    impl_->hnsw->addPoint(embedding.values.data(), label);
    return true;
}

bool HnswSemanticIndex::add(ElementId element_id, std::vector<float> values) {
    return add(Embedding{.element_id = element_id, .values = std::move(values)});
}

bool HnswSemanticIndex::replace(const Embedding& embedding) {
    if (embedding.empty()) {
        return false;
    }
    remove(embedding.element_id);
    return add(embedding);
}

bool HnswSemanticIndex::remove(ElementId element_id) {
    auto it = impl_->entries.find(element_id);
    if (it == impl_->entries.end()) {
        return false;
    }

    // In-memory entry removal
    impl_->entries.erase(it);
    impl_->element_id_to_label.erase(element_id);

    // Rebuild or clear if empty
    if (impl_->entries.empty()) {
        clear();
        return true;
    }

    // For HNSW graph integrity upon deletion, rebuild the index with remaining entries
    const auto entries_copy = impl_->entries;
    clear();
    for (const auto& [_, emb] : entries_copy) {
        add(emb);
    }
    return true;
}

void HnswSemanticIndex::clear() noexcept {
    impl_->entries.clear();
    impl_->label_to_element_id.clear();
    impl_->element_id_to_label.clear();
    impl_->dimensions = 0;
    impl_->current_capacity = 0;
    impl_->hnsw.reset();
    impl_->space.reset();
}

bool HnswSemanticIndex::contains(ElementId element_id) const noexcept {
    return impl_->entries.contains(element_id);
}

const Embedding* HnswSemanticIndex::lookup(ElementId element_id) const noexcept {
    auto it = impl_->entries.find(element_id);
    if (it != impl_->entries.end()) {
        return &it->second;
    }
    return nullptr;
}

std::size_t HnswSemanticIndex::size() const noexcept {
    return impl_->entries.size();
}

bool HnswSemanticIndex::empty() const noexcept {
    return impl_->entries.empty();
}

std::size_t HnswSemanticIndex::dimensions() const noexcept {
    return impl_->dimensions;
}

std::vector<ElementId> HnswSemanticIndex::all_element_ids() const {
    std::vector<ElementId> ids;
    ids.reserve(impl_->entries.size());
    for (const auto& [id, _] : impl_->entries) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::size_t HnswSemanticIndex::estimate_memory_bytes() const noexcept {
    std::size_t bytes = sizeof(*this) + sizeof(Impl);
    bytes += impl_->entries.bucket_count() * sizeof(void*);
    for (const auto& [_, emb] : impl_->entries) {
        bytes += sizeof(ElementId) + sizeof(Embedding) + (emb.values.capacity() * sizeof(float));
    }
    bytes += impl_->label_to_element_id.capacity() * sizeof(ElementId);
    bytes += impl_->element_id_to_label.bucket_count() * sizeof(void*);
    bytes += impl_->element_id_to_label.size() * (sizeof(ElementId) + sizeof(hnswlib::labeltype));
    if (impl_->hnsw) {
        // Approximate HNSW graph memory: links + data points
        bytes += impl_->current_capacity * (sizeof(float) * impl_->dimensions + impl_->config.m * 2 * sizeof(hnswlib::tableint));
    }
    return bytes;
}

std::vector<SemanticSearchResult>
HnswSemanticIndex::search(std::span<const float> query_embedding, std::size_t top_k,
                          float min_similarity) const {
    if (query_embedding.empty() || impl_->entries.empty() || top_k == 0 || !impl_->hnsw) {
        return {};
    }

    if (query_embedding.size() != impl_->dimensions) {
        return {};
    }

    impl_->hnsw->setEf(impl_->config.ef_search);
    const std::size_t actual_k = std::min(top_k, impl_->entries.size());

    auto pq = impl_->hnsw->searchKnn(query_embedding.data(), actual_k);

    std::vector<SemanticSearchResult> candidates;
    candidates.reserve(pq.size());

    while (!pq.empty()) {
        const auto [dist, label] = pq.top();
        pq.pop();

        if (label < impl_->label_to_element_id.size()) {
            const ElementId eid = impl_->label_to_element_id[label];
            const float sim = 1.0f - dist;
            if (sim >= min_similarity) {
                candidates.push_back(SemanticSearchResult{
                    .element_id = eid,
                    .similarity_score = sim,
                });
            }
        }
    }

    // Sort by similarity descending, with deterministic tie-breaking on element_id ascending
    std::sort(candidates.begin(), candidates.end(),
              [](const SemanticSearchResult& a, const SemanticSearchResult& b) {
                  if (std::abs(a.similarity_score - b.similarity_score) > 1e-6f) {
                      return a.similarity_score > b.similarity_score;
                  }
                  return a.element_id < b.element_id;
              });

    if (candidates.size() > top_k) {
        candidates.resize(top_k);
    }

    return candidates;
}

void HnswSemanticIndex::set_ef_search(std::size_t ef_search) {
    impl_->config.ef_search = ef_search;
    if (impl_->hnsw) {
        impl_->hnsw->setEf(ef_search);
    }
}

const HnswConfig& HnswSemanticIndex::config() const noexcept {
    return impl_->config;
}

bool HnswSemanticIndex::save(const std::filesystem::path& file_path) const {
    if (file_path.empty()) {
        return false;
    }
    std::ofstream os(file_path, std::ios::binary);
    if (!os) {
        return false;
    }

    const uint32_t magic = 0x414D4F48;  // "AMOH"
    const uint32_t version = 1;
    const uint32_t index_type = 2;      // HNSW
    const uint32_t dims = static_cast<uint32_t>(impl_->dimensions);
    const uint64_t count = static_cast<uint64_t>(impl_->label_to_element_id.size());
    const uint64_t m = static_cast<uint64_t>(impl_->config.m);
    const uint64_t ef_c = static_cast<uint64_t>(impl_->config.ef_construction);
    const uint64_t ef_s = static_cast<uint64_t>(impl_->config.ef_search);
    const uint64_t seed = static_cast<uint64_t>(impl_->config.random_seed);

    os.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    os.write(reinterpret_cast<const char*>(&version), sizeof(version));
    os.write(reinterpret_cast<const char*>(&index_type), sizeof(index_type));
    os.write(reinterpret_cast<const char*>(&dims), sizeof(dims));
    os.write(reinterpret_cast<const char*>(&count), sizeof(count));
    os.write(reinterpret_cast<const char*>(&m), sizeof(m));
    os.write(reinterpret_cast<const char*>(&ef_c), sizeof(ef_c));
    os.write(reinterpret_cast<const char*>(&ef_s), sizeof(ef_s));
    os.write(reinterpret_cast<const char*>(&seed), sizeof(seed));

    for (size_t i = 0; i < count; ++i) {
        ElementId id = impl_->label_to_element_id[i];
        os.write(reinterpret_cast<const char*>(&id), sizeof(id));
        const auto it = impl_->entries.find(id);
        if (it != impl_->entries.end() && it->second.values.size() == dims) {
            os.write(reinterpret_cast<const char*>(it->second.values.data()), sizeof(float) * dims);
        }
    }

    os.close();

    // Save HNSW graph binary to companion file
    if (impl_->hnsw && count > 0) {
        const std::string graph_path = file_path.string() + ".graph";
        try {
            impl_->hnsw->saveIndex(graph_path);
        } catch (...) {
            return false;
        }
    }

    return true;
}

bool HnswSemanticIndex::load(const std::filesystem::path& file_path) {
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
    uint64_t m = 0;
    uint64_t ef_c = 0;
    uint64_t ef_s = 0;
    uint64_t seed = 0;

    is.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    is.read(reinterpret_cast<char*>(&version), sizeof(version));
    is.read(reinterpret_cast<char*>(&index_type), sizeof(index_type));
    is.read(reinterpret_cast<char*>(&dims), sizeof(dims));
    is.read(reinterpret_cast<char*>(&count), sizeof(count));
    is.read(reinterpret_cast<char*>(&m), sizeof(m));
    is.read(reinterpret_cast<char*>(&ef_c), sizeof(ef_c));
    is.read(reinterpret_cast<char*>(&ef_s), sizeof(ef_s));
    is.read(reinterpret_cast<char*>(&seed), sizeof(seed));

    if (magic != 0x414D4F48 || version != 1 || index_type != 2) {
        return false;
    }

    clear();
    impl_->dimensions = dims;
    impl_->config.m = m;
    impl_->config.ef_construction = ef_c;
    impl_->config.ef_search = ef_s;
    impl_->config.random_seed = seed;

    impl_->label_to_element_id.reserve(count);
    impl_->element_id_to_label.reserve(count);
    impl_->entries.reserve(count);

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
        impl_->label_to_element_id.push_back(id);
        impl_->element_id_to_label.emplace(id, static_cast<hnswlib::labeltype>(i));
        impl_->entries.emplace(id, Embedding{.element_id = id, .values = buf});
    }

    is.close();

    if (count == 0 || dims == 0) {
        return true;
    }

    impl_->space = std::make_unique<hnswlib::InnerProductSpace>(dims);
    const std::string graph_path = file_path.string() + ".graph";

    if (std::filesystem::exists(graph_path)) {
        try {
            impl_->current_capacity = count;
            impl_->hnsw = std::make_unique<hnswlib::HierarchicalNSW<float>>(
                impl_->space.get(), graph_path, false, count);
            impl_->hnsw->setEf(impl_->config.ef_search);
            return true;
        } catch (...) {
            // Fallback to in-memory graph reconstruction if graph file load fails
        }
    }

    // Reconstruct HNSW graph from deserialized entries
    impl_->current_capacity = std::max(count + 100, impl_->config.initial_capacity);
    impl_->hnsw = std::make_unique<hnswlib::HierarchicalNSW<float>>(
        impl_->space.get(), impl_->current_capacity, impl_->config.m,
        impl_->config.ef_construction, impl_->config.random_seed);
    impl_->hnsw->setEf(impl_->config.ef_search);

    for (size_t i = 0; i < count; ++i) {
        ElementId id = impl_->label_to_element_id[i];
        const auto& emb = impl_->entries[id];
        impl_->hnsw->addPoint(emb.values.data(), static_cast<hnswlib::labeltype>(i));
    }

    return true;
}

}  // namespace amoeba::semantic
