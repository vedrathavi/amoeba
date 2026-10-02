#pragma once

#include "amoeba/graph/relationship_evidence_resolver.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/index/inverted_index.hpp"
#include "amoeba/parser/parsed_file.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"
#include "amoeba/source/source_snippet_reader.hpp"
#include "amoeba/tools/repository_access_policy.hpp"

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace amoeba::tools {

/**
 * @brief Bounded execution context passed to repository tools during invocation.
 *
 * Enforces least privilege:
 * - Exposes only necessary read-only engine capabilities.
 * - Does NOT expose shell, process execution, network, or arbitrary OS APIs.
 */
struct ToolExecutionContext {
    /// Canonical or validated root directory of the target repository.
    std::filesystem::path repository_root;

    /// Primary retrieval pipeline for semantic/lexical search.
    const retrieval::PrimaryRetrievalPipeline* retrieval_pipeline{nullptr};

    /// High-performance source snippet reader.
    const source::SourceSnippetReader* snippet_reader{nullptr};

    /// Inverted index for symbol and token lookups.
    const index::InvertedIndex* index{nullptr};

    /// Parsed repository files with structured AST elements.
    const std::vector<parser::ParsedFile>* parsed_files{nullptr};

    /// Code relationship dependency multi-graph.
    const graph::RelationshipGraph* relationship_graph{nullptr};

    /// 1-hop relationship evidence resolver.
    const graph::RelationshipEvidenceResolver* relationship_resolver{nullptr};

    /// Set of known repository-relative file paths recognized by the repository scanner/index.
    const std::unordered_set<std::string>* known_indexed_files{nullptr};

    /// Security policy enforcing repository root boundaries and sensitive file protection.
    const RepositoryAccessPolicy* access_policy{nullptr};
};

}  // namespace amoeba::tools
