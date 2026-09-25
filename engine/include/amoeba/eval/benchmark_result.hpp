#pragma once

#include "amoeba/eval/ranking_metrics.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace amoeba::eval {

/**
 * @brief Machine-readable container for benchmark measurements and experimental context.
 */
struct BenchmarkResult {
    std::string experiment_name;
    std::string dataset_name;
    std::string method_name;

    std::size_t query_count{0};
    std::size_t corpus_files{0};
    std::size_t corpus_elements{0};
    std::size_t corpus_terms{0};
    std::size_t corpus_relationships{0};
    std::size_t embedding_dimensions{0};

    double indexing_time_ms{0.0};
    double query_latency_ms{0.0};
    std::size_t memory_bytes{0};

    RankingMetrics metrics;

    std::string timestamp;
    std::string build_configuration;
    std::string environment_info;

    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] std::string to_csv_header() const;
    [[nodiscard]] std::string to_csv_row() const;
};

}  // namespace amoeba::eval
