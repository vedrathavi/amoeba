#include "amoeba/hybrid/hybrid_retriever.hpp"

#include <algorithm>
#include <tuple>
#include <unordered_map>

namespace amoeba::hybrid {

namespace {

// Key type for matching CodeElement across index and search results
struct ElementMatchKey {
    std::string file_path;
    std::string name;
    uint32_t start_line;
    uint32_t start_col;

    bool operator==(const ElementMatchKey& other) const {
        return start_line == other.start_line && start_col == other.start_col &&
               name == other.name && file_path == other.file_path;
    }
};

struct ElementMatchKeyHash {
    std::size_t operator()(const ElementMatchKey& k) const {
        std::size_t h1 = std::hash<std::string>{}(k.file_path);
        std::size_t h2 = std::hash<std::string>{}(k.name);
        std::size_t h3 = std::hash<uint32_t>{}(k.start_line);
        std::size_t h4 = std::hash<uint32_t>{}(k.start_col);
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
    }
};

}  // namespace

HybridRetriever::HybridRetriever(const index::InvertedIndex& index,
                                 const semantic::SemanticIndex& semantic_index,
                                 const semantic::EmbeddingProvider& provider)
    : index_(index), search_engine_(index), semantic_retriever_(semantic_index),
      provider_(provider) {}

std::vector<HybridSearchResult> HybridRetriever::search(std::string_view query,
                                                        const HybridSearchOptions& options) const {
    if (query.empty()) {
        return {};
    }

    // 1. Lexical candidate retrieval & ranking
    const index::SearchOptions lex_opts{
        .ranker_type = options.lexical_ranker,
        .max_results = options.lexical_top_k,
        .kind_filter = options.kind_filter,
    };
    const auto lex_results = search_engine_.search(query, lex_opts);

    // 2. Semantic candidate retrieval
    const semantic::SemanticRetrievalOptions sem_opts{
        .top_k = options.semantic_top_k,
    };
    const auto sem_results = semantic_retriever_.retrieve_text(query, provider_, sem_opts);

    if (lex_results.empty() && sem_results.empty()) {
        return {};
    }

    // 3. Build index element lookup map for exact ID mapping
    std::unordered_map<ElementMatchKey, index::ElementId, ElementMatchKeyHash> element_to_id;
    element_to_id.reserve(index_.element_count());
    for (index::ElementId id = 0; id < index_.element_count(); ++id) {
        const auto& indexed = index_.get_element(id);
        const auto& file = index_.get_file(indexed.file_id);
        element_to_id[ElementMatchKey{
            .file_path = file.file_path.generic_string(),
            .name = indexed.element.name,
            .start_line = indexed.element.location.start.line,
            .start_col = indexed.element.location.start.column,
        }] = id;
    }

    // 4. Normalize scores per modality
    std::vector<double> raw_lex_scores;
    raw_lex_scores.reserve(lex_results.size());
    for (const auto& r : lex_results) {
        raw_lex_scores.push_back(r.score);
    }
    const auto norm_lex_scores = ScoreNormalizer::min_max_normalize(raw_lex_scores);

    std::vector<double> raw_sem_scores;
    raw_sem_scores.reserve(sem_results.size());
    for (const auto& r : sem_results) {
        raw_sem_scores.push_back(static_cast<double>(r.similarity_score));
    }
    const auto norm_sem_scores = ScoreNormalizer::min_max_normalize(raw_sem_scores);

    // 5. Candidate Union
    std::unordered_map<index::ElementId, ScoredCandidate> candidate_map;

    // Ingest lexical candidates
    for (std::size_t i = 0; i < lex_results.size(); ++i) {
        const auto& lr = lex_results[i];
        index::ElementId elem_id = static_cast<index::ElementId>(i);
        const ElementMatchKey key{
            .file_path = lr.file_path.generic_string(),
            .name = lr.element.name,
            .start_line = lr.element.location.start.line,
            .start_col = lr.element.location.start.column,
        };
        auto it = element_to_id.find(key);
        if (it != element_to_id.end()) {
            elem_id = it->second;
        }

        candidate_map[elem_id] = ScoredCandidate{
            .element_id = elem_id,
            .element = lr.element,
            .file_path = lr.file_path,
            .language = lr.language,
            .raw_lexical_score = lr.score,
            .normalized_lexical_score = norm_lex_scores[i],
            .raw_semantic_score = 0.0,
            .normalized_semantic_score = 0.0,
            .fused_score = 0.0,
            .lexical_rank = static_cast<uint32_t>(i + 1),
            .semantic_rank = 0,
        };
    }

    // Ingest semantic candidates
    for (std::size_t i = 0; i < sem_results.size(); ++i) {
        const auto& sr = sem_results[i];
        auto it = candidate_map.find(sr.element_id);
        if (it != candidate_map.end()) {
            it->second.raw_semantic_score = static_cast<double>(sr.similarity_score);
            it->second.normalized_semantic_score = norm_sem_scores[i];
            it->second.semantic_rank = static_cast<uint32_t>(i + 1);
        } else {
            // Retrieve element metadata from InvertedIndex
            try {
                const auto& indexed_elem = index_.get_element(sr.element_id);
                if (options.kind_filter.has_value() &&
                    indexed_elem.element.kind != *options.kind_filter) {
                    continue;
                }
                const auto& indexed_file = index_.get_file(indexed_elem.file_id);

                candidate_map[sr.element_id] = ScoredCandidate{
                    .element_id = sr.element_id,
                    .element = indexed_elem.element,
                    .file_path = indexed_file.file_path,
                    .language = indexed_file.language,
                    .raw_lexical_score = 0.0,
                    .normalized_lexical_score = 0.0,
                    .raw_semantic_score = static_cast<double>(sr.similarity_score),
                    .normalized_semantic_score = norm_sem_scores[i],
                    .fused_score = 0.0,
                    .lexical_rank = 0,
                    .semantic_rank = static_cast<uint32_t>(i + 1),
                };
            } catch (const std::out_of_range&) {
                continue;
            }
        }
    }

    std::vector<ScoredCandidate> candidates;
    candidates.reserve(candidate_map.size());
    for (auto& [id, cand] : candidate_map) {
        candidates.push_back(std::move(cand));
    }

    // 6. Candidate Fusion & Sorting
    std::vector<ScoredCandidate> fused;
    if (options.fusion_method == FusionMethod::WeightedScore) {
        fused = FusionStrategy::fuse_weighted(std::move(candidates), options.alpha);
    } else {
        fused = FusionStrategy::fuse_rrf(std::move(candidates), options.rrf_k);
    }

    // 7. Build final search results
    const std::size_t limit = std::min(options.max_results, fused.size());
    std::vector<HybridSearchResult> results;
    results.reserve(limit);

    for (std::size_t i = 0; i < limit; ++i) {
        const auto& c = fused[i];
        results.push_back(HybridSearchResult{
            .element_id = c.element_id,
            .element = c.element,
            .file_path = c.file_path,
            .language = c.language,
            .lexical_score = c.raw_lexical_score,
            .normalized_lexical_score = c.normalized_lexical_score,
            .semantic_score = c.raw_semantic_score,
            .normalized_semantic_score = c.normalized_semantic_score,
            .hybrid_score = c.fused_score,
            .lexical_rank = c.lexical_rank,
            .semantic_rank = c.semantic_rank,
        });
    }

    return results;
}

}  // namespace amoeba::hybrid
