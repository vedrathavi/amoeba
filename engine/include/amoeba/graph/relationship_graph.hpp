#pragma once

#include "amoeba/graph/relationship.hpp"
#include "amoeba/graph/relationship_kind.hpp"
#include "amoeba/graph/traversal.hpp"

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace amoeba::graph {

/**
 * @brief In-memory directed graph representing code element relationships.
 *
 * Implements bidirectional adjacency lookups (outgoing and incoming) using compact ElementIds.
 * Does not duplicate CodeElement AST data or repository source text.
 */
class RelationshipGraph {
public:
    RelationshipGraph() = default;
    ~RelationshipGraph() = default;

    RelationshipGraph(const RelationshipGraph&) = default;
    RelationshipGraph& operator=(const RelationshipGraph&) = default;
    RelationshipGraph(RelationshipGraph&&) noexcept = default;
    RelationshipGraph& operator=(RelationshipGraph&&) noexcept = default;

    /**
     * @brief Adds a directed relationship to the graph.
     * @param rel The relationship to add.
     * @return true if the relationship was newly added, false if already present.
     */
    bool add_relationship(const Relationship& rel);

    /**
     * @brief Convenience overload to add a directed relationship.
     */
    bool add_relationship(ElementId source, ElementId target, RelationshipKind kind);

    /**
     * @brief Removes a relationship from the graph.
     * @param rel The relationship to remove.
     * @return true if removed, false if not found.
     */
    bool remove_relationship(const Relationship& rel);

    /**
     * @brief Convenience overload to remove a relationship.
     */
    bool remove_relationship(ElementId source, ElementId target, RelationshipKind kind);

    /**
     * @brief Removes all outgoing and incoming relationships involving the specified element ID.
     * @param id The element ID whose relationships should be removed.
     * @return Total number of relationships removed.
     */
    std::size_t remove_relationships_for_element(ElementId id);

    /**
     * @brief Removes all relationships involving any of the specified element IDs.
     * @param ids The list of element IDs.
     * @return Total number of relationships removed.
     */
    std::size_t remove_relationships_for_elements(const std::vector<ElementId>& ids);

    /**
     * @brief Checks if a specific relationship exists in the graph.
     */
    [[nodiscard]] bool has_relationship(const Relationship& rel) const noexcept;

    /**
     * @brief Checks if a specific directed edge exists between source and target.
     */
    [[nodiscard]] bool has_relationship(ElementId source, ElementId target,
                                        RelationshipKind kind) const noexcept;

    /**
     * @brief Returns all outgoing relationships from the specified source element.
     */
    [[nodiscard]] std::vector<Relationship> outgoing_relationships(ElementId source) const;

    /**
     * @brief Returns all outgoing relationships of a specific kind from the source element.
     */
    [[nodiscard]] std::vector<Relationship> outgoing_relationships(ElementId source,
                                                                   RelationshipKind kind) const;

    /**
     * @brief Returns all incoming relationships into the specified target element.
     */
    [[nodiscard]] std::vector<Relationship> incoming_relationships(ElementId target) const;

    /**
     * @brief Returns all incoming relationships of a specific kind into the target element.
     */
    [[nodiscard]] std::vector<Relationship> incoming_relationships(ElementId target,
                                                                   RelationshipKind kind) const;

    /**
     * @brief Returns all relationships directed from source to target regardless of kind.
     */
    [[nodiscard]] std::vector<Relationship> relationships_between(ElementId source,
                                                                  ElementId target) const;

    /**
     * @brief Returns total number of edges/relationships in the graph.
     */
    [[nodiscard]] std::size_t relationship_count() const noexcept;

    /**
     * @brief Returns total number of edges of a specific kind in the graph.
     */
    [[nodiscard]] std::size_t relationship_count(RelationshipKind kind) const noexcept;

    /**
     * @brief Returns the number of distinct element IDs involved in relationships.
     */
    [[nodiscard]] std::size_t node_count() const noexcept;

    /**
     * @brief Checks whether an element ID participates in any relationship.
     */
    [[nodiscard]] bool has_node(ElementId id) const noexcept;

    /**
     * @brief Returns a sorted list of all unique element IDs present in the graph.
     */
    [[nodiscard]] std::vector<ElementId> all_nodes() const;

    /**
     * @brief Returns unique immediate outgoing neighbor element IDs from source.
     */
    [[nodiscard]] std::vector<ElementId> outgoing_neighbors(ElementId source) const;

    /**
     * @brief Returns unique immediate outgoing neighbor element IDs from source filtered by kind.
     */
    [[nodiscard]] std::vector<ElementId> outgoing_neighbors(ElementId source,
                                                            RelationshipKind kind) const;

    /**
     * @brief Returns unique immediate incoming neighbor element IDs to target.
     */
    [[nodiscard]] std::vector<ElementId> incoming_neighbors(ElementId target) const;

    /**
     * @brief Returns unique immediate incoming neighbor element IDs to target filtered by kind.
     */
    [[nodiscard]] std::vector<ElementId> incoming_neighbors(ElementId target,
                                                            RelationshipKind kind) const;

    /**
     * @brief Performs depth-bounded BFS traversal starting from start_node.
     * Safely handles cycles, self-loops, and disconnected subgraphs using visited tracking.
     * @param start_node The origin element ID.
     * @param options Traversal parameters (direction, max_depth, kind_filter).
     * @return Ordered list of TraversalStep records (in BFS level order).
     */
    [[nodiscard]] std::vector<TraversalStep> traverse(ElementId start_node,
                                                      const TraversalOptions& options = {}) const;

    /**
     * @brief Returns unique reachable node IDs reached from start_node within max_depth.
     */
    [[nodiscard]] std::vector<ElementId>
    reachable_nodes(ElementId start_node, const TraversalOptions& options = {}) const;

    /**
     * @brief Checks if the graph contains no relationships.
     */
    [[nodiscard]] bool empty() const noexcept;

    /**
     * @brief Estimates the total in-memory size in bytes for the graph data structures.
     */
    [[nodiscard]] std::size_t estimate_memory_bytes() const noexcept;

    /**
     * @brief Clears all relationships and adjacency maps.
     */
    void clear() noexcept;

private:
    std::unordered_set<Relationship, RelationshipHash> edges_;
    std::unordered_map<ElementId, std::vector<Relationship>> outgoing_adj_;
    std::unordered_map<ElementId, std::vector<Relationship>> incoming_adj_;
    std::unordered_map<ElementId, std::size_t> node_degree_;
};

}  // namespace amoeba::graph
