#include "amoeba/engine.hpp"

namespace amoeba {

namespace {
constexpr std::string_view ENGINE_VERSION = "0.1.0";
}  // namespace

std::string_view get_version() noexcept {
    return ENGINE_VERSION;
}

}  // namespace amoeba
