#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace amoeba::semantic {

using ElementId = uint32_t;

/**
 * @brief Representation of a dense vector embedding associated with a CodeElement.
 */
struct Embedding {
    ElementId element_id{0};
    std::vector<float> values;

    [[nodiscard]] std::size_t dimensions() const noexcept { return values.size(); }
    [[nodiscard]] bool empty() const noexcept { return values.empty(); }
    [[nodiscard]] bool operator==(const Embedding&) const = default;
};

}  // namespace amoeba::semantic
