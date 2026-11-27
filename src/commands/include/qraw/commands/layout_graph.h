#pragma once

// Graph layouts: pure functions from a LayoutGraph (vertex bounds and edges,
// no document access) to new vertex bounds. The Arrange > Layout menu
// (ops_layout.h arrangeLayout), the `childLayout` values treeLayout and
// circleLayout, and later CSV/Mermaid import all feed the same module, so a
// layout is written once and tested without a document.
//
// Sources (DECISIONS.md D-038):
//  * circleLayout is a port of mxCircleLayout
//    (mxgraph/src/layout/mxCircleLayout.js, Apache-2.0).
//  * treeLayout is a clean-room tidy tree after the documented behaviour of
//    mxCompactTreeLayout (levelDistance, nodeDistance, horizontal, invert,
//    root anchored in place); the original source is not in the upstream
//    clone. Subtrees are packed by their contours, a parent is centred over
//    its first and last child.
//  * radialLayout is a clean-room radial tree after mxRadialTreeLayout (root
//    in the centre, levels on concentric rings, a subtree's angular sector
//    proportional to its leaf count).
//  * flowLayout is a clean-room layered (Sugiyama) layout after the
//    documented behaviour of mxHierarchicalLayout (orientation,
//    intraCellSpacing, interRankCellSpacing, interHierarchySpacing,
//    parallelEdgeSpacing); the original source is not in the upstream clone.
//  * organicLayout is a clean-room Fruchterman-Reingold layout after
//    mxFastOrganicLayout (forceConstant, initialTemp, maxIterations,
//    minDistanceLimit, maxDistanceLimit), with a deterministic start.
// Upstream paths are relative to
// $DRAWIO_SRC/src/main/webapp/.
//
// std-only: no Qt, no model classes.

#include "qraw/core/geometry.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace qraw::commands::layout {

struct Node {
    std::string id;
    core::Rect bounds;
    bool movable = true; // locked cells keep their position (mxGraphLayout.isVertexMovable)
};

// An edge between node indices; -1 is a terminal that is not in the graph.
struct Edge {
    std::string id;
    int source = -1;
    int target = -1;
};

struct Graph {
    // In z-order: it decides the order of siblings in a tree.
    std::vector<Node> nodes;
    std::vector<Edge> edges; // in z-order
};

// New bounds, parallel to Graph::nodes; nullopt for a node the layout left
// alone (not part of a tree, or not movable).
struct Placement {
    std::vector<std::optional<core::Rect>> bounds;
    // Layouts that route edges (flow): the waypoints of each edge of
    // Graph::edges from its source to its target, empty for a straight
    // connection; nullopt for an edge the layout did not look at. Empty
    // vector for layouts that do not route.
    std::vector<std::optional<std::vector<core::Point>>> edgePoints;
};

// mxGraph.findTreeRoots (mxgraph/src/view/mxGraph.js): the vertices that have
// outgoing and no incoming edges (the reverse with `invert`); when there are
// none, the vertex with the most outgoing minus incoming edges. Self loops
// count as both. Indices in node order.
[[nodiscard]] std::vector<std::size_t> findTreeRoots(const Graph& graph, bool invert = false);

struct TreeOptions {
    bool horizontal = false;       // levels go left to right, siblings top to bottom
    bool invert = false;           // levels go bottom to top / right to left
    double levelDistance = 30;     // gap between a level and the next
    double nodeDistance = 20;      // minimum gap between neighbours of a level
    // Every node that is not in the tree of a root becomes the root of a tree
    // of its own (containers lay out all children; the menu only moves trees).
    bool includeUnreached = false;
    // The top-left of the laid out forest. Without it the first root stays
    // where it is and the rest is placed around it.
    std::optional<core::Point> origin;
};

// Tidy tree of every root's tree. Edges are followed source to target
// (target to source with `invert`); a node reached twice belongs to the first
// tree that reaches it. Trees are side by side in the order of `roots`, each
// level at the same depth (rank aligned). Empty `roots`: findTreeRoots.
[[nodiscard]] Placement treeLayout(const Graph& graph, std::vector<std::size_t> roots, const TreeOptions& options);

struct RadialOptions {
    // Minimum gap between the nodes of one ring and the next; the rings grow
    // beyond it where the nodes of a ring would otherwise touch.
    double levelDistance = 40;
    double nodeDistance = 20; // minimum gap between neighbours on a ring
    bool includeUnreached = false;
    std::optional<core::Point> origin;
};

// Radial tree: the root of each tree in the centre of its own layout, levels on
// concentric rings, children in the sector of their parent. Trees are
// side by side in the order of `roots`. Empty `roots`: findTreeRoots.
[[nodiscard]] Placement radialLayout(const Graph& graph, std::vector<std::size_t> roots, const RadialOptions& options);

// mxCircleLayout options with the same meaning and defaults.
struct CircleOptions {
    double radius = 100;
    bool moveCircle = false; // put the circle's top-left at (x0, y0)
    double x0 = 0;
    double y0 = 0;
};

// mxCircleLayout.execute/circle: nodes on a circle in node order, starting at
// the top and going clockwise; the radius is at least count * largest side / pi
// so that nodes do not overlap. Coordinates are rounded as Math.round does.
[[nodiscard]] Placement circleLayout(const Graph& graph, const CircleOptions& options);

// The direction the edges of a flow layout point in.
enum class FlowDirection {
    Down,  // mxHierarchicalLayout orientation north (the default there)
    Up,    // south
    Right, // west: left to right, what Arrange > Layout > Horizontal Flow does
    Left,  // east
};

// mxHierarchicalLayout options with the same meaning and defaults.
struct FlowOptions {
    FlowDirection direction = FlowDirection::Down;
    double intraCellSpacing = 30;      // gap between neighbours in a rank
    double interRankCellSpacing = 100; // gap between a rank and the next
    double interHierarchySpacing = 60; // gap between disconnected components
    double parallelEdgeSpacing = 10;   // distance between edges joining the same two nodes
    // The top-left of the laid out graph. Without it the top-left of the
    // current bounds of the nodes that move.
    std::optional<core::Point> origin;
};

// Layered layout: cycles are broken by reversing edges found by a depth-first
// search, nodes get the rank of their longest path (pulled towards the side
// with more edges), long edges run through dummy nodes, crossings are reduced
// by barycenter sweeps and adjacent swaps, and positions inside a rank
// minimise the squared distance to the neighbours. Every edge points in
// `direction` except those reversed to break a cycle. Disconnected components
// stand side by side. Locked nodes are left out (and so are their edges).
// Edge waypoints run through the dummy nodes of long edges, and offset
// parallel edges from each other.
[[nodiscard]] Placement flowLayout(const Graph& graph, const FlowOptions& options);

// mxFastOrganicLayout options with the same meaning and defaults.
struct OrganicOptions {
    double forceConstant = 50;         // ideal gap between the borders of connected nodes
    double minDistanceLimit = 2;       // distances are never smaller than this
    double maxDistanceLimit = 500;     // nodes further apart do not repel each other
    double initialTemp = 200;          // the largest step of the first iteration
    int maxIterations = 0;             // 0: 20 * sqrt(node count)
    unsigned seed = 1;                 // start positions: spiral plus seeded jitter
    std::optional<core::Point> origin; // top-left of the result; default: of the current bounds
};

// Fruchterman-Reingold: every pair of nodes repels, edges attract, the step
// size cools linearly. Distances are measured between node borders. The start
// is a deterministic function of the node order and the seed (not of the
// current positions), so the layout is repeatable; overlaps left over at the
// end are pushed apart. Locked nodes are left out (and so are their edges).
[[nodiscard]] Placement organicLayout(const Graph& graph, const OrganicOptions& options);

// The bounding box of the placed nodes (nullopt when none).
[[nodiscard]] std::optional<core::Rect> placedBounds(const Placement& placement);

} // namespace qraw::commands::layout
