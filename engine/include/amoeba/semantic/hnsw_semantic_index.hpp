#pragma once

#include "amoeba/semantic/semantic_index.hpp"

#include <memory>
#include <span>
#include <vector>

namespace amoeba::semantic {

/**
 * @brief Approximate Nearest Neighbor semantic vector index using HNSW graphs.
 *
 * Encapsulates the underlying HNSW implementation (via hnswlib) behind the
 * ISemanticVectorIndex interface, completely hiding third-party types from Amoeba's public API.
 */
class HnswSemanticIndex : public ISemanticVectorIndex {
public:
    explicit HnswSemanticIndex(const HnswConfig& config = {});
    ~HnswSemanticIndex() override;

    HnswSemanticIndex(HnswSemanticIndex&&) noexcept;
    HnswSemanticIndex& operator=(HnswSemanticIndex&&) noexcept;

    HnswSemanticIndex(const HnswSemanticIndex&) = delete;
    HnswSemanticIndex& operator=(const HnswSemanticIndex&) = delete;

    bool add(const Embedding& embedding) override;
    bool add(ElementId element_id, std::vector<float> values) override;
    bool replace(const Embedding& embedding) override;
    bool remove(ElementId element_id) override;
    void clear() noexcept override;

    [[nodiscard]] bool contains(ElementId element_id) const noexcept override;
    [[nodiscard]] const Embedding* lookup(ElementId element_id) const noexcept override;
    [[nodiscard]] std::size_t size() const noexcept override;
    [[nodiscard]] bool empty() const noexcept override;
    [[nodiscard]] std::size_t dimensions() const noexcept override;
    [[nodiscard]] std::vector<ElementId> all_element_ids() const override;
    [[nodiscard]] std::size_t estimate_memory_bytes() const noexcept override;

    [[nodiscard]] std::vector<SemanticSearchResult>
    search(std::span<const float> query_embedding, std::size_t top_k,
           float min_similarity = -1.0f) const override;

    bool save(const std::filesystem::path& file_path) const override;
    bool load(const std::filesystem::path& file_path) override;

    /**
     * @brief Dynamic query-time exploration depth configuration.
     */
    void set_ef_search(std::size_t ef_search);

    [[nodiscard]] const HnswConfig& config() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace amoeba::semantic
