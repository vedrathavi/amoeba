#pragma once

#include <string_view>

namespace amoeba {

using namespace std;

/**
 * @brief Returns the official name of the engine.
 */
[[nodiscard]] constexpr string_view get_name() noexcept {
    return "Amoeba";
}

/**
 * @brief Returns the version string of the engine.
 */
[[nodiscard]] string_view get_version() noexcept;

/**
 * @brief Returns the one-line description of the engine.
 */
[[nodiscard]] constexpr string_view get_description() noexcept {
    return "Source Code Search & Indexing Engine";
}

}  // namespace amoeba
