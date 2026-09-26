#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"

#include "amoeba/semantic/semantic_document.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_map>

namespace amoeba::retrieval {

using Clock = std::chrono::steady_clock;

// ─────────────────────────────────────────────────────────────────────────────
// ElementMatchKey — same approach as HybridRetriever for deterministic matching
// of SearchResult (which does not carry ElementId) back to InvertedIndex entries.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct ElementMatchKey {
    std::string file_path;
    std::string name;
    uint32_t start_line{0};
    uint32_t start_col{0};

    bool operator==(const ElementMatchKey& o) const noexcept {
        return start_line == o.start_line && start_col == o.start_col && name == o.name &&
               file_path == o.file_path;
    }
};

struct ElementMatchKeyHash {
    std::size_t operator()(const ElementMatchKey& k) const noexcept {
        const std::size_t h1 = std::hash<std::string>{}(k.file_path);
        const std::size_t h2 = std::hash<std::string>{}(k.name);
        const std::size_t h3 = std::hash<uint32_t>{}(k.start_line);
        const std::size_t h4 = std::hash<uint32_t>{}(k.start_col);
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
    }
};

using ElementToIdMap = std::unordered_map<ElementMatchKey, index::ElementId, ElementMatchKeyHash>;

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

PrimaryRetrievalPipeline::PrimaryRetrievalPipeline(std::span<const parser::ParsedFile> parsed_files,
                                                   const index::InvertedIndex& index,
                                                   const semantic::EmbeddingProvider& provider)
    : index_(index), provider_(provider), search_engine_(index),
      semantic_retriever_(semantic_index_) {
    build_units(parsed_files);
    reconcile_element_ids();  // Phase 6.4: patch primary_element_id from InvertedIndex
    build_semantic_index();
    build_element_id_map();
}

void PrimaryRetrievalPipeline::build_units(std::span<const parser::ParsedFile> parsed_files) {
    units_ = SupportingEvidenceResolver::resolve_units(parsed_files);
}

void PrimaryRetrievalPipeline::build_semantic_index() {
    // Embed each primary RetrievalUnit using its enriched text representation.
    // Uses primary_element_id as the key — patched by reconcile_element_ids() to
    // match the InvertedIndex ElementId, ensuring consistent identity across modalities.
    for (const auto& unit : units_) {
        const std::string text = semantic::SemanticTextFormatter::format_unit(unit);
        const auto values = provider_.embed(text);
        if (values.empty()) {
            continue;
        }
        semantic_index_.add(semantic::Embedding{
            .element_id = unit.primary_element_id,
            .values = values,
        });
    }
}

void PrimaryRetrievalPipeline::reconcile_element_ids() {
    // The SupportingEvidenceResolver assigns primary_element_id as the index of
    // the element within its ParsedFile (i.e., position in ParsedFile::elements).
    // This is NOT the same as the InvertedIndex ElementId, which is global and
    // monotonically increases across all files.
    //
    // This method patches each RetrievalUnit's primary_element_id to the correct
    // InvertedIndex ElementId by matching via (file_path, name, start_line, start_col).
    // Without this step, two units from different files that share position 0 in their
    // respective ParsedFile::elements would both have primary_element_id = 0, causing
    // collisions in the SemanticIndex.
    //
    // Build a ElementMatchKey -> InvertedIndex ElementId map once, then update units.
    ElementToIdMap index_key_to_id;
    index_key_to_id.reserve(index_.element_count());
    for (index::ElementId id = 0; id < static_cast<index::ElementId>(index_.element_count());
         ++id) {
        const auto& ie = index_.get_element(id);
        const auto& file = index_.get_file(ie.file_id);
        index_key_to_id.emplace(
            ElementMatchKey{
                .file_path = file.file_path.generic_string(),
                .name = ie.element.name,
                .start_line = ie.element.location.start.line,
                .start_col = ie.element.location.start.column,
            },
            id);
    }

    for (auto& unit : units_) {
        const ElementMatchKey key{
            .file_path = unit.file_path.generic_string(),
            .name = unit.primary_element.name,
            .start_line = unit.primary_element.location.start.line,
            .start_col = unit.primary_element.location.start.column,
        };
        const auto it = index_key_to_id.find(key);
        if (it != index_key_to_id.end()) {
            unit.primary_element_id = it->second;
        }
        // Synthetic module units (no matching InvertedIndex entry) keep their
        // default primary_element_id = 0. These are edge cases for pure CSS/config
        // files that have no primary element in the InvertedIndex at all.
    }
}

void PrimaryRetrievalPipeline::build_element_id_map() {
    // Build a lookup: InvertedIndex ElementId → index in units_.
    // We cover both:
    //   (a) primary element IDs → their own unit index
    //   (b) supporting element IDs → their owning unit index
    // This enables post-retrieval mapping of any lexical or semantic hit —
    // regardless of whether it matched a primary or supporting element —
    // to the correct owning primary unit.
    element_id_to_unit_.reserve(units_.size() * 8);
    for (std::size_t ui = 0; ui < units_.size(); ++ui) {
        const auto& unit = units_[ui];
        element_id_to_unit_.emplace(unit.primary_element_id, ui);
        for (const auto sid : unit.supporting_element_ids) {
            element_id_to_unit_.try_emplace(static_cast<index::ElementId>(sid), ui);
        }
    }
}

std::size_t PrimaryRetrievalPipeline::supporting_element_count() const noexcept {
    std::size_t total = 0;
    for (const auto& unit : units_) {
        total += unit.supporting_elements.size();
    }
    return total;
}

// ─────────────────────────────────────────────────────────────────────────────
// Search
// ─────────────────────────────────────────────────────────────────────────────

std::vector<PrimarySearchResult>
PrimaryRetrievalPipeline::search(std::string_view query,
                                 const PrimarySearchOptions& options) const {
    PipelineMetrics ignored;
    return search_with_metrics(query, options, ignored);
}

std::vector<PrimarySearchResult> PrimaryRetrievalPipeline::search_with_metrics(
    std::string_view query, const PrimarySearchOptions& options, PipelineMetrics& metrics) const {
    if (query.empty() || units_.empty()) {
        return {};
    }

    const auto query_rep = QueryUnderstanding::analyze(query);
    if (query_rep.all_search_terms.empty() && query_rep.collapsed_query.empty()) {
        return {};
    }

    const auto t_start = Clock::now();

    metrics.total_elements = index_.element_count();
    metrics.primary_unit_count = units_.size();
    metrics.supporting_count = supporting_element_count();

    // ─── 0. Build ElementMatchKey → ElementId map for this query ─────────────
    // This is the same approach used by HybridRetriever. It resolves SearchResult
    // (which doesn't carry element_id) back to an InvertedIndex ElementId in O(1)
    // per result, at the cost of one O(N_elements) construction.
    // We build it once per search call and reuse for all lex_results.
    ElementToIdMap elem_key_to_id;
    elem_key_to_id.reserve(index_.element_count());
    for (index::ElementId id = 0; id < static_cast<index::ElementId>(index_.element_count());
         ++id) {
        const auto& ie = index_.get_element(id);
        const auto& file = index_.get_file(ie.file_id);
        elem_key_to_id.emplace(
            ElementMatchKey{
                .file_path = file.file_path.generic_string(),
                .name = ie.element.name,
                .start_line = ie.element.location.start.line,
                .start_col = ie.element.location.start.column,
            },
            id);
    }

    // ─── 1. Lexical Retrieval ─────────────────────────────────────────────────
    const auto t_lex_start = Clock::now();

    const index::SearchOptions lex_opts{
        .match_mode = index::MatchMode::AnyTerm,
        .ranker_type = options.lexical_ranker,
        .max_results = options.lexical_top_k,
        .kind_filter = options.kind_filter,
    };
    const auto lex_results = search_engine_.search(query, lex_opts);
    metrics.lexical_candidates = lex_results.size();

    const auto t_lex_end = Clock::now();
    metrics.lexical_ms = std::chrono::duration<double, std::milli>(t_lex_end - t_lex_start).count();

    // ─── 2. Semantic Retrieval ────────────────────────────────────────────────
    const auto t_sem_start = Clock::now();

    const semantic::SemanticRetrievalOptions sem_opts{.top_k = options.semantic_top_k};
    const auto sem_results = semantic_retriever_.retrieve_text(query, provider_, sem_opts);
    metrics.semantic_candidates = sem_results.size();

    const auto t_sem_end = Clock::now();
    metrics.semantic_ms =
        std::chrono::duration<double, std::milli>(t_sem_end - t_sem_start).count();

    // ─── 3. Build Fused Candidate Map keyed by unit index ────────────────────
    const auto t_fuse_start = Clock::now();

    // Normalize scores
    std::vector<double> raw_lex;
    raw_lex.reserve(lex_results.size());
    for (const auto& r : lex_results) {
        raw_lex.push_back(r.score);
    }
    const auto norm_lex = hybrid::ScoreNormalizer::min_max_normalize(raw_lex);

    std::vector<double> raw_sem;
    raw_sem.reserve(sem_results.size());
    for (const auto& r : sem_results) {
        raw_sem.push_back(static_cast<double>(r.similarity_score));
    }
    const auto norm_sem = hybrid::ScoreNormalizer::min_max_normalize(raw_sem);

    // unit_index → ScoredCandidate
    std::unordered_map<std::size_t, hybrid::ScoredCandidate> candidate_map;

    // Ingest lexical results
    for (std::size_t i = 0; i < lex_results.size(); ++i) {
        const auto& lr = lex_results[i];

        const ElementMatchKey key{
            .file_path = lr.file_path.generic_string(),
            .name = lr.element.name,
            .start_line = lr.element.location.start.line,
            .start_col = lr.element.location.start.column,
        };
        const auto eid_it = elem_key_to_id.find(key);
        if (eid_it == elem_key_to_id.end()) {
            continue;  // Element not found in index (should not happen)
        }
        const index::ElementId eid = eid_it->second;

        const auto unit_it = element_id_to_unit_.find(eid);
        if (unit_it == element_id_to_unit_.end()) {
            continue;  // Not mapped to any primary unit (filtered or unresolved)
        }
        const std::size_t ui = unit_it->second;

        auto& cand = candidate_map[ui];
        // Keep the lexical score of the PRIMARY match if the primary element
        // itself was hit, otherwise keep the best raw score (first hit wins
        // since lex_results is already ranked highest-first).
        if (cand.lexical_rank == 0) {
            cand.element_id = units_[ui].primary_element_id;
            cand.element = units_[ui].primary_element;
            cand.file_path = units_[ui].file_path;
            cand.language = units_[ui].language;
            cand.raw_lexical_score = lr.score;
            cand.normalized_lexical_score = norm_lex[i];
            cand.lexical_rank = static_cast<uint32_t>(i + 1);
        }
    }

    // Ingest semantic results
    // The SemanticIndex was built from primary units only, so every
    // SemanticSearchResult.element_id is a primary_element_id.
    for (std::size_t i = 0; i < sem_results.size(); ++i) {
        const auto& sr = sem_results[i];
        const auto unit_it = element_id_to_unit_.find(sr.element_id);
        if (unit_it == element_id_to_unit_.end()) {
            continue;
        }
        const std::size_t ui = unit_it->second;

        auto& cand = candidate_map[ui];
        cand.raw_semantic_score = static_cast<double>(sr.similarity_score);
        cand.normalized_semantic_score = norm_sem[i];
        cand.semantic_rank = static_cast<uint32_t>(i + 1);

        if (cand.lexical_rank == 0) {
            // Semantic-only candidate: fill in element metadata
            cand.element_id = units_[ui].primary_element_id;
            cand.element = units_[ui].primary_element;
            cand.file_path = units_[ui].file_path;
            cand.language = units_[ui].language;
        }
    }

    // Convert to vector
    std::vector<hybrid::ScoredCandidate> candidates;
    candidates.reserve(candidate_map.size());
    for (auto& [ui, cand] : candidate_map) {
        candidates.push_back(std::move(cand));
    }
    metrics.fused_candidates = candidates.size();

    // ─── 4. Fusion ────────────────────────────────────────────────────────────
    double effective_alpha = options.alpha;
    if (options.adaptive_fusion && options.alpha > 0.0 && options.alpha < 1.0) {
        effective_alpha = query_rep.recommended_alpha;
    }

    std::vector<hybrid::ScoredCandidate> fused;
    if (options.fusion_method == hybrid::FusionMethod::WeightedScore) {
        fused = hybrid::FusionStrategy::fuse_weighted(std::move(candidates), effective_alpha);
    } else {
        fused = hybrid::FusionStrategy::fuse_rrf(std::move(candidates), options.rrf_k);
    }

    const auto t_fuse_end = Clock::now();
    metrics.fusion_ms =
        std::chrono::duration<double, std::milli>(t_fuse_end - t_fuse_start).count();

    // ─── 5. Build PrimarySearchResult[] ──────────────────────────────────────
    std::vector<PrimarySearchResult> results;
    results.reserve(std::min(options.max_results, fused.size()));

    for (std::size_t i = 0; i < fused.size() && results.size() < options.max_results; ++i) {
        const auto& c = fused[i];
        const bool has_lex = c.lexical_rank > 0;
        const bool has_sem = c.semantic_rank > 0;

        if (effective_alpha >= 1.0 && !has_lex) {
            continue;
        }
        if (effective_alpha <= 0.0 && !has_sem) {
            continue;
        }

        const auto prov = (has_lex && has_sem) ? RetrievalProvenance::HybridBoth
                          : has_sem            ? RetrievalProvenance::SemanticOnly
                                               : RetrievalProvenance::LexicalOnly;

        // Recover unit index from the primary_element_id stored in fused candidate
        const auto unit_it = element_id_to_unit_.find(c.element_id);
        if (unit_it == element_id_to_unit_.end()) {
            continue;
        }
        const RetrievalUnit& unit = units_[unit_it->second];

        results.push_back(PrimarySearchResult{
            .unit = unit,
            .lexical_score = c.raw_lexical_score,
            .normalized_lexical_score = c.normalized_lexical_score,
            .lexical_rank = c.lexical_rank,
            .semantic_score = c.raw_semantic_score,
            .normalized_semantic_score = c.normalized_semantic_score,
            .semantic_rank = c.semantic_rank,
            .hybrid_score = c.fused_score,
            .provenance = prov,
        });
    }

    metrics.final_results = results.size();
    metrics.total_ms = std::chrono::duration<double, std::milli>(Clock::now() - t_start).count();

    return results;
}

}  // namespace amoeba::retrieval
