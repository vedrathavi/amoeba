#pragma once

#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/parsed_file.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::graph {

/**
 * @brief Resolution outcome status for a call or reference candidate.
 */
enum class ResolutionStatus : uint8_t {
    Resolved,   ///< Confidently resolved to a unique target element.
    Ambiguous,  ///< Multiple equally likely candidate declarations exist without disambiguation.
    Unresolved  ///< Target is external, standard library, or unresolvable in the repository.
};

[[nodiscard]] constexpr std::string_view to_string(ResolutionStatus status) noexcept {
    switch (status) {
    case ResolutionStatus::Resolved:
        return "RESOLVED";
    case ResolutionStatus::Ambiguous:
        return "AMBIGUOUS";
    case ResolutionStatus::Unresolved:
        return "UNRESOLVED";
    }
    return "UNKNOWN";
}

/**
 * @brief Detailed resolution record for a single call or symbol reference.
 */
struct CallResolution {
    ElementId source_element_id{0};                  ///< Caller element ID.
    std::string source_name;                         ///< Caller name.
    std::string raw_target_name;                     ///< Raw callee or reference expression.
    RelationshipKind kind{RelationshipKind::Calls};  ///< Calls or References.
    std::optional<ElementId> target_element_id{std::nullopt};
    ResolutionStatus status{ResolutionStatus::Unresolved};
    std::string resolution_reason;  ///< Explainable resolution rule or reason.
};

/**
 * @brief Aggregate statistics and detailed resolution records from call & reference extraction.
 */
struct CallExtractionResult {
    std::size_t total_call_candidates{0};
    std::size_t resolved_calls{0};
    std::size_t unresolved_calls{0};
    std::size_t ambiguous_calls{0};
    std::size_t resolved_references{0};
    std::vector<CallResolution> resolutions;
};

/**
 * @brief Conservative symbol resolver and call graph extractor.
 *
 * Core Principle: "Never manufacture certainty. Prefer unknown over guessing."
 * Implements a layered resolution hierarchy:
 * 1. Exact recursive / local definition
 * 2. Lexical & enclosing scope (same class, enclosing method, same file)
 * 3. Namespace / module context matching
 * 4. Imported symbols (following file imports/includes)
 * 5. Unique repository-wide declaration
 * 6. Deterministic ambiguity rejection when multiple candidates tie
 */
class CallExtractor {
public:
    CallExtractor() = default;
    ~CallExtractor() = default;

    /**
     * @brief Extracts CALLS and REFERENCES relationships from an InvertedIndex and populates the
     * graph.
     * @param index The indexed repository elements and files.
     * @param[out] graph The relationship graph to populate with resolved directed edges.
     * @return Detailed extraction metrics and individual resolution records.
     */
    static CallExtractionResult extract_and_populate(const index::InvertedIndex& index,
                                                     RelationshipGraph& graph);

    /**
     * @brief Extracts CALLS and REFERENCES relationships from a vector of ParsedFile
     * representations.
     * @param files The parsed source files.
     * @param[out] graph The relationship graph to populate.
     * @return Detailed extraction metrics.
     */
    static CallExtractionResult extract_and_populate(const std::vector<parser::ParsedFile>& files,
                                                     RelationshipGraph& graph);
};

}  // namespace amoeba::graph
