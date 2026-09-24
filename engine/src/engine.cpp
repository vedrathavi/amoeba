#include "amoeba/engine.hpp"

namespace amoeba {

using namespace std;

namespace {
constexpr string_view ENGINE_VERSION = "0.1.0";
}  // namespace

string_view get_version() noexcept {
    return ENGINE_VERSION;
}

}  // namespace amoeba
