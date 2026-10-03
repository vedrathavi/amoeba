#include "amoeba/evidence/evidence_assembler.hpp"

namespace amoeba::evidence {

EvidenceAssembler::EvidenceAssembler(
    const graph::RelationshipEvidenceResolver& relationship_resolver)
    : default_snippet_reader_(), snippet_reader_(default_snippet_reader_),
      relationship_resolver_(relationship_resolver), relationship_expander_(nullptr) {}

EvidenceAssembler::EvidenceAssembler(
    const source::SourceSnippetReader& snippet_reader,
    const graph::RelationshipEvidenceResolver& relationship_resolver)
    : default_snippet_reader_(), snippet_reader_(snippet_reader),
      relationship_resolver_(relationship_resolver), relationship_expander_(nullptr) {}

EvidenceAssembler::EvidenceAssembler(
    const source::SourceSnippetReader& snippet_reader,
    const graph::RelationshipEvidenceResolver& relationship_resolver,
    const graph::RelationshipExpander& relationship_expander)
    : default_snippet_reader_(), snippet_reader_(snippet_reader),
      relationship_resolver_(relationship_resolver),
      relationship_expander_(&relationship_expander) {}

EvidenceItem EvidenceAssembler::assemble_item(const retrieval::PrimarySearchResult& result,
                                              const EvidenceAssemblerOptions& options) const {
    EvidenceItem item;
    item.primary_result = result;
    item.is_expanded_relationship = false;
    item.expansion_depth = 0;

    if (options.include_source_snippets) {
        const auto& path = result.unit.file_path;
        const auto& loc = result.unit.primary_element.location;
        item.source_excerpt =
            snippet_reader_.try_read_range(path, loc, options.source_context_lines);
    }

    if (options.include_relationships) {
        item.direct_relationships =
            relationship_resolver_.resolve(result, options.relationship_options);
    }

    return item;
}

EvidenceBundle
EvidenceAssembler::assemble(std::string_view query,
                            std::span<const retrieval::PrimarySearchResult> search_results,
                            const EvidenceAssemblerOptions& options) const {
    EvidenceBundle bundle;
    bundle.query = std::string(query);

    std::size_t reserve_count = search_results.size();
    if (options.enable_expansion) {
        reserve_count += options.expansion_config.max_units;
    }
    bundle.items.reserve(reserve_count);

    // 1. Assemble primary search results
    for (const auto& result : search_results) {
        bundle.items.push_back(assemble_item(result, options));
    }

    // 2. Perform bounded relationship expansion if enabled
    if (options.enable_expansion && options.expansion_config.max_depth > 0 &&
        options.expansion_config.max_units > 0 && relationship_expander_ != nullptr) {

        auto expanded_candidates =
            relationship_expander_->expand(query, search_results, options.expansion_config);

        for (auto& cand : expanded_candidates) {
            EvidenceItem item;
            item.primary_result.unit.primary_element.name = cand.name;
            item.primary_result.unit.primary_element.kind = cand.kind;
            item.primary_result.unit.primary_element.location = cand.location;
            item.primary_result.unit.primary_element.parent_context = cand.parent_context;
            item.primary_result.unit.primary_element.detail = cand.detail;
            item.primary_result.unit.file_path = cand.file_path;
            item.primary_result.unit.primary_element_id = cand.element_id;
            item.primary_result.hybrid_score = cand.relevance_score;
            item.primary_result.provenance = retrieval::RetrievalProvenance::HybridBoth;

            if (!cand.source_excerpt_text.empty()) {
                source::SourceExcerpt exc;
                exc.text = std::move(cand.source_excerpt_text);
                exc.start_line = cand.location.start.line;
                exc.end_line = cand.location.end.line;
                exc.file_path = cand.file_path;
                item.source_excerpt = std::move(exc);
            }

            item.is_expanded_relationship = true;
            item.expansion_depth = cand.depth;
            item.expansion_relationship_type =
                std::string(graph::to_string(cand.relationship_kind));
            item.expansion_direction = std::string(graph::to_string(cand.direction));
            item.expansion_seed_id = cand.seed_element_id;
            item.expansion_seed_name = cand.seed_symbol_name;

            bundle.items.push_back(std::move(item));
        }
    }

    return bundle;
}

}  // namespace amoeba::evidence
