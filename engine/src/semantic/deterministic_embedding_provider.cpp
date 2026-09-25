#include "amoeba/semantic/deterministic_embedding_provider.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace amoeba::semantic {

namespace {

// FNV-1a 64-bit hash
uint64_t fnv1a_hash(std::string_view str) noexcept {
    uint64_t hash = 14695981039346656037ULL;
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t fnv1a_hash_seed(std::string_view str, uint64_t seed) noexcept {
    uint64_t hash = seed;
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

}  // namespace

DeterministicEmbeddingProvider::DeterministicEmbeddingProvider(std::size_t dimensions)
    : dimensions_(dimensions == 0 ? 64 : dimensions) {}

std::vector<float> DeterministicEmbeddingProvider::embed(std::string_view text) const {
    std::vector<float> vec(dimensions_, 0.0f);
    if (text.empty()) {
        return vec;
    }

    // 1. Tokenize text into words / identifiers
    std::string current_token;
    std::vector<std::string> tokens;

    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            current_token.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else {
            if (!current_token.empty()) {
                tokens.push_back(std::move(current_token));
                current_token.clear();
            }
        }
    }
    if (!current_token.empty()) {
        tokens.push_back(std::move(current_token));
    }

    if (tokens.empty()) {
        return vec;
    }

    // 2. Hash each token and character 3-gram into vector dimensions
    for (const auto& token : tokens) {
        uint64_t h = fnv1a_hash(token);
        std::size_t idx = static_cast<std::size_t>(h % dimensions_);
        float sign = (h & 0x1000000000000000ULL) ? 1.0f : -1.0f;
        vec[idx] += sign * (1.0f + static_cast<float>(token.length()) * 0.1f);

        // Character n-grams for subword similarity
        if (token.length() >= 3) {
            for (std::size_t i = 0; i <= token.length() - 3; ++i) {
                std::string_view ngram(token.data() + i, 3);
                uint64_t nh = fnv1a_hash_seed(ngram, h);
                std::size_t nidx = static_cast<std::size_t>(nh % dimensions_);
                float nsign = (nh & 0x2000000000000000ULL) ? 0.5f : -0.5f;
                vec[nidx] += nsign;
            }
        }
    }

    // 3. Normalize to unit L2 length
    double sum_sq = 0.0;
    for (float val : vec) {
        sum_sq += static_cast<double>(val) * static_cast<double>(val);
    }

    if (sum_sq > 1e-12) {
        double norm = std::sqrt(sum_sq);
        for (float& val : vec) {
            val = static_cast<float>(val / norm);
        }
    }

    return vec;
}

}  // namespace amoeba::semantic
