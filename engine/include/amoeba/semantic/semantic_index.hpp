#pragma once

#include "amoeba/semantic/embedding.hpp"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace amoeba::semantic {

/**
 * @brief Minimal in-memory semantic index storing ElementId -> Embedding mappings.
 *
 * Implements clean decoupled vector storage without database/ANN dependencies.
 */
class SemanticIndex {
public:
    SemanticIndex() = default;

    /**
     * @brief Adds an embedding to the index.
     * @return true if added, false if already present (use replace to update).
     */
    bool add(const Embedding& embedding);

    /**
     * @brief Convenience overload to add an embedding.
     */
    bool add(ElementId element_id, std::vector<float> values);

    /**
     * @brief Replaces an existing embedding or inserts if not present.
     * @return true if an existing entry was replaced, false if newly inserted.
     */
    bool replace(const Embedding& embedding);

    /**
     * @brief Removes an embedding from the index by ElementId.
     * @return true if removed, false if not found.
     */
    bool remove(ElementId element_id);

    /**
     * @brief Clears all embeddings from the index.
     */
    void clear() noexcept;

    /**
     * @brief Checks whether an ElementId has an embedding in the index.
     */
    [[nodiscard]] bool contains(ElementId element_id) const noexcept;

    /**
     * @brief Looks up an embedding by ElementId.
     * @return Pointer to Embedding if found, nullptr otherwise.
     */
    [[nodiscard]] const Embedding* lookup(ElementId element_id) const noexcept;

    /**
     * @brief Returns the total number of indexed embeddings.
     */
    [[nodiscard]] std::size_t size() const noexcept;

    /**
     * @brief Checks whether the index is empty.
     */
    [[nodiscard]] bool empty() const noexcept;

    /**
     * @brief Returns the expected embedding vector dimensions (or 0 if index is empty).
     */
    [[nodiscard]] std::size_t dimensions() const noexcept;

    /**
     * @brief Returns a list of all indexed ElementIds.
     */
    [[nodiscard]] std::vector<ElementId> all_element_ids() const;

    /**
     * @brief Returns the raw internal entries map.
     */
    [[nodiscard]] const std::unordered_map<ElementId, Embedding>& entries() const noexcept;

    /**
     * @brief Estimates total in-memory size in bytes.
     */
    [[nodiscard]] std::size_t estimate_memory_bytes() const noexcept;

private:
    std::unordered_map<ElementId, Embedding> entries_;
    std::size_t dimensions_{0};
};

}  // namespace amoeba::semantic
