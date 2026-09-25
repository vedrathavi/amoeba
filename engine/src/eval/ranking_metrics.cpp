#include "amoeba/eval/ranking_metrics.hpp"

#include <algorithm>
#include <cmath>

namespace amoeba::eval {

RankingMetrics RankingMetrics::average(std::span<const RankingMetrics> metrics_list) {
    RankingMetrics avg;
    if (metrics_list.empty()) {
        return avg;
    }

    for (const auto& m : metrics_list) {
        avg.p_at_1 += m.p_at_1;
        avg.p_at_3 += m.p_at_3;
        avg.p_at_5 += m.p_at_5;
        avg.p_at_10 += m.p_at_10;
        avg.r_at_5 += m.r_at_5;
        avg.r_at_10 += m.r_at_10;
        avg.mrr += m.mrr;
        avg.ndcg_at_5 += m.ndcg_at_5;
        avg.ndcg_at_10 += m.ndcg_at_10;
    }

    const double n = static_cast<double>(metrics_list.size());
    avg.p_at_1 /= n;
    avg.p_at_3 /= n;
    avg.p_at_5 /= n;
    avg.p_at_10 /= n;
    avg.r_at_5 /= n;
    avg.r_at_10 /= n;
    avg.mrr /= n;
    avg.ndcg_at_5 /= n;
    avg.ndcg_at_10 /= n;

    return avg;
}

RankingMetrics
compute_ranking_metrics(std::span<const std::string> retrieved_names,
                        const std::unordered_map<std::string, uint32_t>& relevance_grades) {
    RankingMetrics m;

    std::size_t total_relevant = 0;
    std::vector<uint32_t> all_grades;
    for (const auto& [_, grade] : relevance_grades) {
        if (grade > 0) {
            total_relevant++;
            all_grades.push_back(grade);
        }
    }

    if (total_relevant == 0) {
        return m;
    }

    std::sort(all_grades.begin(), all_grades.end(), std::greater<uint32_t>());

    auto compute_idcg = [&](std::size_t k) -> double {
        double idcg = 0.0;
        std::size_t limit = std::min(k, all_grades.size());
        for (std::size_t i = 0; i < limit; ++i) {
            idcg += (std::pow(2.0, all_grades[i]) - 1.0) / std::log2(static_cast<double>(i + 2));
        }
        return idcg;
    };

    auto compute_dcg = [&](std::size_t k) -> double {
        double dcg = 0.0;
        std::size_t limit = std::min(k, retrieved_names.size());
        for (std::size_t i = 0; i < limit; ++i) {
            uint32_t rel = 0;
            if (auto it = relevance_grades.find(retrieved_names[i]); it != relevance_grades.end()) {
                rel = it->second;
            }
            if (rel > 0) {
                dcg += (std::pow(2.0, rel) - 1.0) / std::log2(static_cast<double>(i + 2));
            }
        }
        return dcg;
    };

    auto compute_precision = [&](std::size_t k) -> double {
        if (k == 0)
            return 0.0;
        std::size_t relevant_count = 0;
        std::size_t limit = std::min(k, retrieved_names.size());
        for (std::size_t i = 0; i < limit; ++i) {
            if (auto it = relevance_grades.find(retrieved_names[i]);
                it != relevance_grades.end() && it->second > 0) {
                relevant_count++;
            }
        }
        return static_cast<double>(relevant_count) / static_cast<double>(k);
    };

    auto compute_recall = [&](std::size_t k) -> double {
        if (total_relevant == 0)
            return 0.0;
        std::size_t relevant_count = 0;
        std::size_t limit = std::min(k, retrieved_names.size());
        for (std::size_t i = 0; i < limit; ++i) {
            if (auto it = relevance_grades.find(retrieved_names[i]);
                it != relevance_grades.end() && it->second > 0) {
                relevant_count++;
            }
        }
        return static_cast<double>(relevant_count) / static_cast<double>(total_relevant);
    };

    m.p_at_1 = compute_precision(1);
    m.p_at_3 = compute_precision(3);
    m.p_at_5 = compute_precision(5);
    m.p_at_10 = compute_precision(10);

    m.r_at_5 = compute_recall(5);
    m.r_at_10 = compute_recall(10);

    // MRR
    for (std::size_t i = 0; i < retrieved_names.size(); ++i) {
        if (auto it = relevance_grades.find(retrieved_names[i]);
            it != relevance_grades.end() && it->second > 0) {
            m.mrr = 1.0 / static_cast<double>(i + 1);
            break;
        }
    }

    // NDCG
    double idcg_5 = compute_idcg(5);
    double dcg_5 = compute_dcg(5);
    m.ndcg_at_5 = (idcg_5 > 0.0) ? (dcg_5 / idcg_5) : 0.0;

    double idcg_10 = compute_idcg(10);
    double dcg_10 = compute_dcg(10);
    m.ndcg_at_10 = (idcg_10 > 0.0) ? (dcg_10 / idcg_10) : 0.0;

    return m;
}

}  // namespace amoeba::eval
