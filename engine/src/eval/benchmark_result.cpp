#include "amoeba/eval/benchmark_result.hpp"

#include <iomanip>
#include <sstream>

namespace amoeba::eval {

std::string BenchmarkResult::to_json() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    oss << "{\n";
    oss << "  \"experiment\": \"" << experiment_name << "\",\n";
    oss << "  \"dataset\": \"" << dataset_name << "\",\n";
    oss << "  \"method\": \"" << method_name << "\",\n";
    oss << "  \"query_count\": " << query_count << ",\n";
    oss << "  \"corpus_files\": " << corpus_files << ",\n";
    oss << "  \"corpus_elements\": " << corpus_elements << ",\n";
    oss << "  \"corpus_terms\": " << corpus_terms << ",\n";
    oss << "  \"corpus_relationships\": " << corpus_relationships << ",\n";
    oss << "  \"embedding_dimensions\": " << embedding_dimensions << ",\n";
    oss << "  \"indexing_time_ms\": " << indexing_time_ms << ",\n";
    oss << "  \"query_latency_ms\": " << query_latency_ms << ",\n";
    oss << "  \"memory_bytes\": " << memory_bytes << ",\n";
    oss << "  \"metrics\": {\n";
    oss << "    \"p_at_1\": " << metrics.p_at_1 << ",\n";
    oss << "    \"p_at_3\": " << metrics.p_at_3 << ",\n";
    oss << "    \"p_at_5\": " << metrics.p_at_5 << ",\n";
    oss << "    \"p_at_10\": " << metrics.p_at_10 << ",\n";
    oss << "    \"r_at_5\": " << metrics.r_at_5 << ",\n";
    oss << "    \"r_at_10\": " << metrics.r_at_10 << ",\n";
    oss << "    \"mrr\": " << metrics.mrr << ",\n";
    oss << "    \"ndcg_at_5\": " << metrics.ndcg_at_5 << ",\n";
    oss << "    \"ndcg_at_10\": " << metrics.ndcg_at_10 << "\n";
    oss << "  },\n";
    oss << "  \"timestamp\": \"" << timestamp << "\",\n";
    oss << "  \"build_configuration\": \"" << build_configuration << "\",\n";
    oss << "  \"environment_info\": \"" << environment_info << "\"\n";
    oss << "}\n";
    return oss.str();
}

std::string BenchmarkResult::to_csv_header() const {
    return "experiment,dataset,method,query_count,corpus_files,corpus_elements,indexing_time_ms,"
           "query_latency_ms,memory_bytes,p_at_1,p_at_3,p_at_5,r_at_5,mrr,ndcg_at_5,ndcg_at_10\n";
}

std::string BenchmarkResult::to_csv_row() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    oss << experiment_name << "," << dataset_name << "," << method_name << "," << query_count << ","
        << corpus_files << "," << corpus_elements << "," << indexing_time_ms << ","
        << query_latency_ms << "," << memory_bytes << "," << metrics.p_at_1 << "," << metrics.p_at_3
        << "," << metrics.p_at_5 << "," << metrics.r_at_5 << "," << metrics.mrr << ","
        << metrics.ndcg_at_5 << "," << metrics.ndcg_at_10 << "\n";
    return oss.str();
}

}  // namespace amoeba::eval
