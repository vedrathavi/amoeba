#include "amoeba/evidence/evidence_assembler.hpp"

namespace amoeba::evidence {

EvidenceAssembler::EvidenceAssembler(
    const graph::RelationshipEvidenceResolver& relationship_resolver)
    : default_snippet_reader_(), snippet_reader_(default_snippet_reader_),
      relationship_resolver_(relationship_resolver) {}

EvidenceAssembler::EvidenceAssembler(
    const source::SourceSnippetReader& snippet_reader,
    const graph::RelationshipEvidenceResolver& relationship_resolver)
    : default_snippet_reader_(), snippet_reader_(snippet_reader),
      relationship_resolver_(relationship_resolver) {}

EvidenceItem EvidenceAssembler::assemble_item(const retrieval::PrimarySearchResult& result,
                                              const EvidenceAssemblerOptions& options) const {
    EvidenceItem item;
    item.primary_result = result;

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
    bundle.items.reserve(search_results.size());

    for (const auto& result : search_results) {
        bundle.items.push_back(assemble_item(result, options));
    }

    return bundle;
}

}  // namespace amoeba::evidence
