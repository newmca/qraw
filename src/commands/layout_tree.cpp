// Tree and radial tree layouts (layout_graph.h). Clean-room implementations:
// the upstream mxCompactTreeLayout and mxRadialTreeLayout sources are not in
// the clone, so the algorithms follow their documented behaviour.
#include "layout_internal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace qraw::commands::layout {

namespace {

using detail::Forest;
using detail::Tree;
using detail::TreeNode;

[[nodiscard]] core::Point centreOf(const core::Rect& r) { return {r.x + r.width / 2, r.y + r.height / 2}; }

// Moves every placed rectangle so that the top-left of their bounding box is `origin`.
void moveToOrigin(Placement& placement, const core::Point& origin)
{
    const auto box = placedBounds(placement);
    if (!box)
        return;
    const double dx = origin.x - box->x;
    const double dy = origin.y - box->y;
    for (auto& rect : placement.bounds) {
        if (rect) {
            rect->x += dx;
            rect->y += dy;
        }
    }
}

// Writes `rect` for a node unless it is locked.
void place(const Graph& graph, Placement& placement, std::size_t node, const core::Rect& rect)
{
    if (graph.nodes[node].movable)
        placement.bounds[node] = rect;
}

// --- tidy tree ---------------------------------------------------------------------

// The extent of a subtree at each level below (and including) its root, as
// distances from the root's centre along the breadth axis.
struct Contour {
    std::vector<double> left;
    std::vector<double> right;
};

// One tree laid out along the breadth axis: centres relative to the root's
// centre, and the extent of the whole tree.
struct BreadthLayout {
    std::vector<double> centre; // per tree node
    double minLeft = 0;
    double maxRight = 0;
};

BreadthLayout layoutBreadth(const Tree& tree, const std::vector<double>& breadth, double nodeDistance)
{
    const std::size_t count = tree.nodes.size();
    std::vector<Contour> contours(count);
    std::vector<double> offsetInParent(count, 0); // relative to the parent's centre once the parent is done

    for (std::size_t i = count; i-- > 0;) {
        const TreeNode& node = tree.nodes[i];
        const double half = breadth[i] / 2;
        Contour& own = contours[i];
        if (node.children.empty()) {
            own.left = {-half};
            own.right = {half};
            continue;
        }

        // Place the children one after the other against the contour of those
        // already placed (all relative to the first child's centre).
        std::vector<double> childCentre; // where each child's centre is
        Contour placed;
        for (const int child : node.children) {
            Contour& c = contours[static_cast<std::size_t>(child)];
            double centre = 0;
            if (!childCentre.empty()) {
                centre = -std::numeric_limits<double>::infinity();
                const std::size_t common = std::min(placed.right.size(), c.left.size());
                for (std::size_t level = 0; level < common; ++level)
                    centre = std::max(centre, placed.right[level] + nodeDistance - c.left[level]);
            }
            childCentre.push_back(centre);
            if (placed.left.size() < c.left.size()) {
                const std::size_t old = placed.left.size();
                placed.left.resize(c.left.size());
                placed.right.resize(c.left.size());
                for (std::size_t level = old; level < c.left.size(); ++level) {
                    placed.left[level] = centre + c.left[level];
                    placed.right[level] = centre + c.right[level];
                }
                for (std::size_t level = 0; level < old; ++level) {
                    placed.left[level] = std::min(placed.left[level], centre + c.left[level]);
                    placed.right[level] = std::max(placed.right[level], centre + c.right[level]);
                }
            } else {
                for (std::size_t level = 0; level < c.left.size(); ++level) {
                    placed.left[level] = std::min(placed.left[level], centre + c.left[level]);
                    placed.right[level] = std::max(placed.right[level], centre + c.right[level]);
                }
            }
            c = Contour{}; // merged: free it
        }

        // The parent sits over the middle between the first and the last child.
        const double parentCentre = (childCentre.front() + childCentre.back()) / 2;
        for (std::size_t k = 0; k < node.children.size(); ++k)
            offsetInParent[static_cast<std::size_t>(node.children[k])] = childCentre[k] - parentCentre;
        own.left.reserve(placed.left.size() + 1);
        own.right.reserve(placed.right.size() + 1);
        own.left.push_back(-half);
        own.right.push_back(half);
        for (std::size_t level = 0; level < placed.left.size(); ++level) {
            own.left.push_back(placed.left[level] - parentCentre);
            own.right.push_back(placed.right[level] - parentCentre);
        }
    }

    BreadthLayout result;
    result.centre.assign(count, 0);
    result.minLeft = *std::min_element(contours[0].left.begin(), contours[0].left.end());
    result.maxRight = *std::max_element(contours[0].right.begin(), contours[0].right.end());
    for (std::size_t i = 1; i < count; ++i)
        result.centre[i] = result.centre[static_cast<std::size_t>(tree.nodes[i].parent)] + offsetInParent[i];
    return result;
}

} // namespace

Placement treeLayout(const Graph& graph, std::vector<std::size_t> roots, const TreeOptions& options)
{
    Placement placement;
    placement.bounds.assign(graph.nodes.size(), std::nullopt);
    const Forest forest = detail::buildForest(graph, std::move(roots), options.invert, options.includeUnreached);
    if (forest.empty())
        return placement;

    const bool horizontal = options.horizontal;
    const auto breadthOf = [&](std::size_t node) {
        const core::Rect& b = graph.nodes[node].bounds;
        return horizontal ? b.height : b.width;
    };
    const auto depthOf = [&](std::size_t node) {
        const core::Rect& b = graph.nodes[node].bounds;
        return horizontal ? b.width : b.height;
    };

    // Levels are shared by all trees: a level is as thick as its largest node.
    std::vector<std::vector<int>> depth(forest.size());
    std::vector<double> thickness;
    for (std::size_t t = 0; t < forest.size(); ++t) {
        const Tree& tree = forest[t];
        depth[t].assign(tree.nodes.size(), 0);
        for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
            if (tree.nodes[i].parent >= 0)
                depth[t][i] = depth[t][static_cast<std::size_t>(tree.nodes[i].parent)] + 1;
            const auto level = static_cast<std::size_t>(depth[t][i]);
            if (thickness.size() <= level)
                thickness.resize(level + 1, 0);
            thickness[level] = std::max(thickness[level], depthOf(tree.nodes[i].node));
        }
    }
    std::vector<double> levelOffset(thickness.size(), 0);
    for (std::size_t level = 1; level < thickness.size(); ++level)
        levelOffset[level] = levelOffset[level - 1] + thickness[level - 1] + options.levelDistance;

    // The first root stays where it is.
    const core::Rect& anchor = graph.nodes[forest.front().nodes.front().node].bounds;
    const double breadthStart = horizontal ? centreOf(anchor).y : centreOf(anchor).x;
    const double depthStart = horizontal ? anchor.x : anchor.y;
    const double anchorDepth = horizontal ? anchor.width : anchor.height;

    double right = 0; // right edge of the trees so far, relative to the first root's centre
    for (std::size_t t = 0; t < forest.size(); ++t) {
        const Tree& tree = forest[t];
        std::vector<double> breadth(tree.nodes.size());
        for (std::size_t i = 0; i < breadth.size(); ++i)
            breadth[i] = breadthOf(tree.nodes[i].node);
        const BreadthLayout layout = layoutBreadth(tree, breadth, options.nodeDistance);
        const double shift = t == 0 ? 0 : right + options.nodeDistance - layout.minLeft;
        right = shift + layout.maxRight;

        for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
            const std::size_t node = tree.nodes[i].node;
            const core::Rect& b = graph.nodes[node].bounds;
            const double centre = breadthStart + shift + layout.centre[i];
            const double offset = levelOffset[static_cast<std::size_t>(depth[t][i])];
            // Inverted trees grow the other way; each node keeps the edge facing the root on its level.
            const double top = options.invert ? depthStart + anchorDepth - offset - depthOf(node) : depthStart + offset;
            place(graph, placement, node,
                  horizontal ? core::Rect{top, centre - b.height / 2, b.width, b.height}
                             : core::Rect{centre - b.width / 2, top, b.width, b.height});
        }
    }
    if (options.origin)
        moveToOrigin(placement, *options.origin);
    return placement;
}

// --- radial tree ---------------------------------------------------------------------

namespace {

struct RingNode {
    double angle = 0;
    double halfDiagonal = 0;
};

// Laid out around the root at (0, 0): the centre of every node of the tree.
std::vector<core::Point> layoutRadial(const Graph& graph, const Tree& tree, const RadialOptions& options)
{
    constexpr double kTwoPi = 2 * std::numbers::pi;
    const std::size_t count = tree.nodes.size();

    std::vector<int> leaves(count, 1);
    for (std::size_t i = count; i-- > 0;) {
        if (!tree.nodes[i].children.empty()) {
            leaves[i] = 0;
            for (const int child : tree.nodes[i].children)
                leaves[i] += leaves[static_cast<std::size_t>(child)];
        }
    }

    // Sectors: a node's sector is divided among its children by leaf count.
    // The first child of the root is centred on the top.
    std::vector<double> sectorStart(count, 0);
    std::vector<double> sectorWidth(count, kTwoPi);
    std::vector<double> angle(count, 0);
    std::vector<int> depth(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        const TreeNode& node = tree.nodes[i];
        double start = sectorStart[i];
        if (i == 0 && !node.children.empty()) {
            const double first = kTwoPi * leaves[static_cast<std::size_t>(node.children.front())] / leaves[0];
            start = -std::numbers::pi / 2 - first / 2;
        }
        for (const int child : node.children) {
            const auto c = static_cast<std::size_t>(child);
            sectorWidth[c] = sectorWidth[i] * leaves[c] / leaves[i];
            sectorStart[c] = start;
            angle[c] = start + sectorWidth[c] / 2;
            depth[c] = depth[i] + 1;
            start += sectorWidth[c];
        }
    }

    // Ring radii: far enough apart for the nodes of two rings, and for the
    // neighbours on one ring, not to touch. Nodes are circles of their half diagonal.
    std::vector<std::vector<RingNode>> rings;
    for (std::size_t i = 0; i < count; ++i) {
        const core::Rect& b = graph.nodes[tree.nodes[i].node].bounds;
        const auto level = static_cast<std::size_t>(depth[i]);
        if (rings.size() <= level)
            rings.resize(level + 1);
        rings[level].push_back({angle[i], std::hypot(b.width, b.height) / 2});
    }
    std::vector<double> radius(rings.size(), 0);
    double previousLargest = rings[0][0].halfDiagonal;
    for (std::size_t level = 1; level < rings.size(); ++level) {
        const auto& ring = rings[level];
        double largest = 0;
        for (const RingNode& n : ring)
            largest = std::max(largest, n.halfDiagonal);
        double r = radius[level - 1] + previousLargest + largest + options.levelDistance;
        // Neighbours on the ring, the last with the first; two nodes are one pair.
        const std::size_t pairs = ring.size() < 2 ? 0 : (ring.size() == 2 ? 1 : ring.size());
        for (std::size_t k = 0; k < pairs; ++k) {
            const RingNode& a = ring[k];
            const RingNode& b = ring[(k + 1) % ring.size()];
            double between = std::fmod(b.angle - a.angle + kTwoPi, kTwoPi);
            between = std::min(between, kTwoPi - between);
            const double chord = std::sin(between / 2);
            if (chord > 1e-9)
                r = std::max(r, (a.halfDiagonal + b.halfDiagonal + options.nodeDistance) / (2 * chord));
        }
        radius[level] = r;
        previousLargest = largest;
    }

    std::vector<core::Point> centres(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double r = radius[static_cast<std::size_t>(depth[i])];
        centres[i] = {r * std::cos(angle[i]), r * std::sin(angle[i])};
    }
    return centres;
}

} // namespace

Placement radialLayout(const Graph& graph, std::vector<std::size_t> roots, const RadialOptions& options)
{
    Placement placement;
    placement.bounds.assign(graph.nodes.size(), std::nullopt);
    const Forest forest = detail::buildForest(graph, std::move(roots), false, options.includeUnreached);
    if (forest.empty())
        return placement;

    // The first root keeps its centre; further trees follow on its right, centred on the same height.
    const core::Point anchor = centreOf(graph.nodes[forest.front().nodes.front().node].bounds);
    double right = 0;
    for (std::size_t t = 0; t < forest.size(); ++t) {
        const Tree& tree = forest[t];
        const std::vector<core::Point> centres = layoutRadial(graph, tree, options);

        double minX = 0;
        double maxX = 0;
        for (std::size_t i = 0; i < centres.size(); ++i) {
            const core::Rect& b = graph.nodes[tree.nodes[i].node].bounds;
            minX = i == 0 ? centres[i].x - b.width / 2 : std::min(minX, centres[i].x - b.width / 2);
            maxX = i == 0 ? centres[i].x + b.width / 2 : std::max(maxX, centres[i].x + b.width / 2);
        }
        const double shift = t == 0 ? 0 : right + options.nodeDistance - minX;
        right = shift + maxX;

        for (std::size_t i = 0; i < centres.size(); ++i) {
            const std::size_t node = tree.nodes[i].node;
            const core::Rect& b = graph.nodes[node].bounds;
            place(graph, placement, node,
                  {anchor.x + shift + centres[i].x - b.width / 2, anchor.y + centres[i].y - b.height / 2, b.width,
                   b.height});
        }
    }
    if (options.origin)
        moveToOrigin(placement, *options.origin);
    return placement;
}

} // namespace qraw::commands::layout
