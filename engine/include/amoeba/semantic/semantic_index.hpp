#pragma once

#include "amoeba/semantic/embedding.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace amoeba::semantic {

/**
 * @brief Search result record from semantic retrieval.
 */
struct SemanticSearchResult {
    ElementId element_id{0};
    float similarity_score{0.0f};

    [[nodiscard]] bool operator==(const SemanticSearchResult&) const = default;
};

/**
 * @brief Configuration parameters for semantic retrieval.
 */
struct SemanticRetrievalOptions {
    std::size_t top_k{10};
    float min_similarity{-1.0f};  ///< Minimum similarity score threshold
};

/**
 * @brief Supported semantic vector index implementations.
 */
enum class SemanticIndexType {
    Exact,  ///< Reference exhaustive linear scan with deterministic tie-breaking
    Hnsw    ///< Approximate nearest neighbor search via Hierarchical Navigable Small World graphs
};

/**
 * @brief Configuration options for HNSW vector index construction and search.
 */
struct HnswConfig {
    std::size_t m{16};                  ///< Number of bidirectional links per vector (default: 16)
    std::size_t ef_construction{200};   ///< Exploration depth during graph construction (default: 200)
    std::size_t ef_search{64};          ///< Exploration depth during nearest-neighbor query (default: 64)
    std::size_t initial_capacity{1000}; ///< Initial pre-allocated vector capacity
    std::size_t random_seed{42};        ///< Seed for deterministic graph generation
};

/**
 * @brief Abstract interface for semantic vector indexing and nearest-neighbor search.
 *
 * Provides a clean replaceable seam for vector search without leaking vector database,
 * LLM, query understanding, evidence, sufficiency, or fusion concerns.
 */
class ISemanticVectorIndex {
public:
    virtual ~ISemanticVectorIndex() = default;

    /**
     * @brief Adds an embedding to the index.
     * @return true if added, false if already present or dimension mismatch.
     */
    virtual bool add(const Embedding& embedding) = 0;

    /**
     * @brief Convenience overload to add an embedding.
     */
    virtual bool add(ElementId element_id, std::vector<float> values) = 0;

    /**
     * @brief Replaces an existing embedding or inserts if not present.
     */
    virtual bool replace(const Embedding& embedding) = 0;

    /**
     * @brief Removes an embedding from the index by ElementId.
     */
    virtual bool remove(ElementId element_id) = 0;

    /**
     * @brief Clears all embeddings from the index.
     */
    virtual void clear() noexcept = 0;

    /**
     * @brief Checks whether an ElementId has an embedding in the index.
     */
    [[nodiscard]] virtual bool contains(ElementId element_id) const noexcept = 0;

    /**
     * @brief Looks up an embedding by ElementId.
     * @return Pointer to Embedding if found, nullptr otherwise.
     */
    [[nodiscard]] virtual const Embedding* lookup(ElementId element_id) const noexcept = 0;

    /**
     * @brief Returns the total number of indexed embeddings.
     */
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;

    /**
     * @brief Checks whether the index is empty.
     */
    [[nodiscard]] virtual bool empty() const noexcept = 0;

    /**
     * @brief Returns the expected embedding vector dimensions (or 0 if index is empty).
     */
    [[nodiscard]] virtual std::size_t dimensions() const noexcept = 0;

    /**
     * @brief Returns a list of all indexed ElementIds in ascending order.
     */
    [[nodiscard]] virtual std::vector<ElementId> all_element_ids() const = 0;

    /**
     * @brief Estimates total in-memory size in bytes.
     */
    [[nodiscard]] virtual std::size_t estimate_memory_bytes() const noexcept = 0;

    /**
     * @brief Performs nearest-neighbor search for the query embedding vector.
     * @param query_embedding Query vector span.
     * @param top_k Maximum number of matches to return.
     * @param min_similarity Minimum similarity score threshold.
     * @return Ranked list of SemanticSearchResult items with deterministic tie-breaking.
     */
    [[nodiscard]] virtual std::vector<SemanticSearchResult>
    search(std::span<const float> query_embedding, std::size_t top_k,
           float min_similarity = -1.0f) const = 0;

    /**
     * @brief Serializes the index state to a binary file on disk.
     * @param file_path Destination file path.
     * @return true if successfully saved, false on I/O error.
     */
    virtual bool save(const std::filesystem::path& file_path) const = 0;

    /**
     * @brief Deserializes and restores the index state from a binary file on disk.
     * @param file_path Source file path.
     * @return true if successfully loaded, false on error/corruption/mismatch.
     */
    virtual bool load(const std::filesystem::path& file_path) = 0;
};

/**
 * @brief Reference exhaustive linear cosine semantic index.
 *
 * Scans all stored embeddings and computes cosine similarity against the query vector.
 * Serves as the ground truth reference for correctness and accuracy benchmarks.
 */
class ExactSemanticIndex : public ISemanticVectorIndex {
public:
    ExactSemanticIndex() = default;

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
     * @brief Returns the raw internal entries map.
     */
    [[nodiscard]] const std::unordered_map<ElementId, Embedding>& entries() const noexcept {
        return entries_;
    }

private:
    std::unordered_map<ElementId, Embedding> entries_;
    std::size_t dimensions_{0};
};

/**
 * @brief Backward-compatible alias for ExactSemanticIndex.
 */
using SemanticIndex = ExactSemanticIndex;

/**
 * @brief Factory function to instantiate semantic vector indices.
 */
[[nodiscard]] std::unique_ptr<ISemanticVectorIndex>
create_semantic_index(SemanticIndexType type = SemanticIndexType::Exact,
                      const HnswConfig& hnsw_config = {});

}  // namespace amoeba::semantic
