#pragma once

// Shared by layout_tree.cpp and layout_graph.cpp. Internal to qraw_commands.

#include "qraw/commands/layout_graph.h"

#include <cstddef>
#include <vector>

namespace qraw::commands::layout::detail {

// A node of a tree: the graph node, its parent and its children (indices into
// Tree::nodes). Nodes are in depth-first pre-order, so every child comes after
// its parent and iterating backwards visits children first.
struct TreeNode {
    std::size_t node = 0;
    int parent = -1;
    std::vector<int> children;
};
struct Tree {
    std::vector<TreeNode> nodes; // nodes[0] is the root
};
using Forest = std::vector<Tree>;

// The spanning trees of `roots` (findTreeRoots when empty): a depth-first walk
// along the edges (against them with `invert`) taking the targets in node
// order; a node belongs to the first tree that reaches it. With
// `includeUnreached` every node left over becomes the root of a tree of its
// own, in node order.
[[nodiscard]] Forest buildForest(const Graph& graph, std::vector<std::size_t> roots, bool invert,
                                 bool includeUnreached);

} // namespace qraw::commands::layout::detail
