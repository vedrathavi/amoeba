#pragma once

#include "amoeba/graph/focused_subgraph.hpp"
#include "amoeba/graph/relationship_graph.hpp"
#include "amoeba/graph/traversal.hpp"

#include <cstddef>
#include <vector>

namespace amoeba::graph {

/**
 * @brief Query engine and focused subgraph generator for RelationshipGraph.
 *
 * Provides high-level semantic queries:
 * - Call hierarchy (`callers`, `callees`)
 * - Structural hierarchy (`parents`, `children`)
 * - Type hierarchy (`base_types`, `derived_types`)
 * - Dependency tracing (`dependencies`, `dependents`)
 * - Contextual slices (`neighborhood_subgraph`, `call_graph_subgraph`, `type_hierarchy_subgraph`)
 */
class GraphQueryService {
public:
    /**
     * @brief Constructs a query service wrapping a RelationshipGraph.
     */
    explicit GraphQueryService(const RelationshipGraph& graph) noexcept : graph_(graph) {}

    // -------------------------------------------------------------------------
    // 1. Primitive Composable Queries
    // -------------------------------------------------------------------------

    /**
     * @brief Returns all direct callers invoking the specified function/method.
     */
    [[nodiscard]] std::vector<ElementId> callers(ElementId element_id) const;

    /**
     * @brief Returns all direct callees invoked by the specified function/method.
     */
    [[nodiscard]] std::vector<ElementId> callees(ElementId element_id) const;

    /**
     * @brief Traces outgoing dependencies (Calls, References, Imports, Includes, InheritsFrom,
     * Implements).
     */
    [[nodiscard]] std::vector<ElementId> dependencies(ElementId element_id,
                                                      std::size_t depth = 1) const;

    /**
     * @brief Traces inbound dependents (elements depending on this element).
     */
    [[nodiscard]] std::vector<ElementId> dependents(ElementId element_id,
                                                    std::size_t depth = 1) const;

    /**
     * @brief Returns enclosing parent elements (e.g. enclosing Class for a Method).
     */
    [[nodiscard]] std::vector<ElementId> parents(ElementId element_id) const;

    /**
     * @brief Returns child structural elements contained within this element.
     */
    [[nodiscard]] std::vector<ElementId> children(ElementId element_id) const;

    /**
     * @brief Returns direct base classes and implemented interfaces.
     */
    [[nodiscard]] std::vector<ElementId> base_types(ElementId element_id) const;

    /**
     * @brief Returns direct derived subclasses and implementers.
     */
    [[nodiscard]] std::vector<ElementId> derived_types(ElementId element_id) const;

    // -------------------------------------------------------------------------
    // 2. Focused Subgraph Queries
    // -------------------------------------------------------------------------

    /**
     * @brief Generates a directional focused subgraph from a root node within depth limits.
     */
    [[nodiscard]] FocusedSubgraph focused_subgraph(ElementId root_node,
                                                   const TraversalOptions& options = {}) const;

    /**
     * @brief Generates a bi-directional neighborhood subgraph around a focal root element.
     */
    [[nodiscard]] FocusedSubgraph neighborhood_subgraph(ElementId root_node,
                                                        std::size_t depth = 1) const;

    /**
     * @brief Generates a focused call graph slice (both callers and callees) around a function.
     */
    [[nodiscard]] FocusedSubgraph call_graph_subgraph(ElementId root_node,
                                                      std::size_t depth = 2) const;

    /**
     * @brief Generates a focused type hierarchy slice (base and derived classes) around a type.
     */
    [[nodiscard]] FocusedSubgraph type_hierarchy_subgraph(ElementId root_node,
                                                          std::size_t depth = 2) const;

    /**
     * @brief Extracts an induced subgraph connecting a collection of elements.
     */
    [[nodiscard]] FocusedSubgraph induced_subgraph(const std::vector<ElementId>& element_ids) const;

private:
    const RelationshipGraph& graph_;
};

}  // namespace amoeba::graph
