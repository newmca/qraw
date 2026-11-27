// Layered ("flow") layout after the documented behaviour of mxHierarchicalLayout
// (draw.io, Apache-2.0; the source is not in the upstream clone, so this is a
// clean-room Sugiyama layout, DECISIONS.md D-039): a pure function over a
// LayoutGraph. The model updates (edge style, waypoints) are the caller's
// (ops_layout_graph.cpp).
//
// Pipeline per connected component, in a frame where edges point down the
// "rank axis" (V) and the order of a rank runs along the "order axis" (U);
// the frame is turned into the requested direction at the very end:
//   1. cycle removal: depth-first search, back edges are reversed;
//   2. ranks: longest path, then nodes with more outgoing than incoming links
//      move down next to their successors;
//   3. long links get dummy nodes, one per rank they cross;
//   4. crossing reduction: barycenter sweeps and adjacent swaps, keeping the
//      best order seen;
//   5. coordinates: sweeps that move every rank to the position nearest (least
//      squares) to its neighbours under the minimum gaps, solved exactly per
//      rank by pooling adjacent violators;
//   6. ranks get their V coordinate; edges become orthogonal routes whose bends
//      lie in the gaps between ranks, through the dummies of long links.
// Everything is deterministic: ties are decided by node and edge order.
#include "layout_internal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>
#include <utility>

namespace qraw::commands::layout {

namespace {

using core::Point;
using core::Rect;

constexpr int kOrderSweeps = 24;
constexpr int kTranspositionPasses = 8;
constexpr int kPositionSweeps = 24;
// Straightening weights (Gansner et al.): the longer a run of dummies, the
// straighter it should be.
constexpr double kWeightReal = 1;
constexpr double kWeightMixed = 2;
constexpr double kWeightDummies = 8;

// Node of the layered graph: a real node of a component or a dummy of a long link.
struct Item {
    int node = -1;       // component-local node, -1 for a dummy
    int rank = 0;
    double extentU = 0;  // size along the order axis
    double extentV = 0;  // size along the rank axis
    double u = 0;        // centre along the order axis
    std::vector<std::pair<int, double>> up;   // neighbours in the rank above, with weight
    std::vector<std::pair<int, double>> down; // neighbours in the rank below
    [[nodiscard]] bool dummy() const { return node < 0; }
};

// A connection between two nodes of a component (an original edge).
struct Connection {
    int a = 0; // component-local source
    int b = 0; // component-local target
    std::size_t edge = 0; // index into Graph::edges
};

// What flows out of laying out one component, in frame coordinates (U, V; the
// component's top-left is the origin).
struct FrameLayout {
    std::vector<Point> centres;                       // per component node
    double sizeU = 0;
    double sizeV = 0;
    std::vector<std::pair<std::size_t, std::vector<Point>>> waypoints; // edge index, frame points
};

class Component {
public:
    Component(std::vector<double> extentU, std::vector<double> extentV, std::vector<Connection> connections,
              const FlowOptions& options)
        : m_extentU(std::move(extentU)), m_extentV(std::move(extentV)), m_connections(std::move(connections)),
          m_options(options)
    {
    }

    FrameLayout run()
    {
        removeCycles();
        assignRanks();
        buildLayers();
        reduceCrossings();
        assignPositions();
        return frame();
    }

private:
    struct Link {
        int from = 0;
        int to = 0;
        std::vector<std::size_t> connections; // indices into m_connections, in edge order
        std::vector<int> chain;               // items from `from` to `to`, dummies between
    };

    std::size_t nodeCount() const { return m_extentU.size(); }

    // --- 1. cycles ---------------------------------------------------------------
    void removeCycles()
    {
        const std::size_t count = nodeCount();
        std::vector<std::vector<std::size_t>> out(count);
        for (std::size_t i = 0; i < m_connections.size(); ++i)
            out[static_cast<std::size_t>(m_connections[i].a)].push_back(i);

        m_reversed.assign(m_connections.size(), false);
        std::vector<int> state(count, 0); // 0 new, 1 on the stack, 2 done
        std::vector<std::pair<std::size_t, std::size_t>> stack;
        for (std::size_t root = 0; root < count; ++root) {
            if (state[root] != 0)
                continue;
            state[root] = 1;
            stack.emplace_back(root, 0);
            while (!stack.empty()) {
                const std::size_t node = stack.back().first;
                if (stack.back().second == out[node].size()) {
                    state[node] = 2;
                    stack.pop_back();
                    continue;
                }
                const std::size_t connection = out[node][stack.back().second++];
                const auto target = static_cast<std::size_t>(m_connections[connection].b);
                if (state[target] == 1) {
                    m_reversed[connection] = true;
                } else if (state[target] == 0) {
                    state[target] = 1;
                    stack.emplace_back(target, 0);
                }
            }
        }

        // One link per ordered pair; parallel and opposite connections share it.
        std::vector<std::tuple<int, int, std::size_t>> pairs;
        for (std::size_t i = 0; i < m_connections.size(); ++i) {
            const Connection& c = m_connections[i];
            pairs.emplace_back(m_reversed[i] ? c.b : c.a, m_reversed[i] ? c.a : c.b, i);
        }
        std::sort(pairs.begin(), pairs.end());
        for (const auto& [from, to, connection] : pairs) {
            if (m_links.empty() || m_links.back().from != from || m_links.back().to != to)
                m_links.push_back({from, to, {}, {}});
            m_links.back().connections.push_back(connection);
        }
    }

    // --- 2. ranks ----------------------------------------------------------------
    void assignRanks()
    {
        const std::size_t count = nodeCount();
        std::vector<std::vector<int>> succ(count);
        std::vector<int> inDegree(count, 0);
        std::vector<int> outDegree(count, 0);
        for (const Link& link : m_links) {
            succ[static_cast<std::size_t>(link.from)].push_back(link.to);
            ++inDegree[static_cast<std::size_t>(link.to)];
            ++outDegree[static_cast<std::size_t>(link.from)];
        }

        // Kahn's algorithm, lowest node first among the ready ones.
        std::vector<int> remaining = inDegree;
        std::vector<int> order;
        std::vector<int> ready;
        for (std::size_t i = 0; i < count; ++i) {
            if (remaining[i] == 0)
                ready.push_back(static_cast<int>(i));
        }
        m_rank.assign(count, 0);
        for (std::size_t head = 0; head < ready.size(); ++head) {
            const int node = ready[head];
            order.push_back(node);
            for (const int next : succ[static_cast<std::size_t>(node)]) {
                m_rank[static_cast<std::size_t>(next)] =
                    std::max(m_rank[static_cast<std::size_t>(next)], m_rank[static_cast<std::size_t>(node)] + 1);
                if (--remaining[static_cast<std::size_t>(next)] == 0)
                    ready.push_back(next);
            }
        }

        // Promotion: a node with more links out than in is moved down as far as
        // its successors allow, which shortens more links than it lengthens.
        // Successors are final when a node is looked at (reverse topological order).
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            const auto node = static_cast<std::size_t>(*it);
            if (outDegree[node] <= inDegree[node] || succ[node].empty())
                continue;
            int nearest = std::numeric_limits<int>::max();
            for (const int next : succ[node])
                nearest = std::min(nearest, m_rank[static_cast<std::size_t>(next)]);
            m_rank[node] = std::max(m_rank[node], nearest - 1);
        }
    }

    // --- 3. dummies --------------------------------------------------------------
    int addItem(int node, int rank, double extentU, double extentV)
    {
        Item item;
        item.node = node;
        item.rank = rank;
        item.extentU = extentU;
        item.extentV = extentV;
        m_items.push_back(std::move(item));
        return static_cast<int>(m_items.size()) - 1;
    }

    void buildLayers()
    {
        for (std::size_t i = 0; i < nodeCount(); ++i)
            addItem(static_cast<int>(i), m_rank[i], m_extentU[i], m_extentV[i]);

        for (Link& link : m_links) {
            const int fromRank = m_rank[static_cast<std::size_t>(link.from)];
            const int toRank = m_rank[static_cast<std::size_t>(link.to)];
            link.chain.push_back(link.from);
            for (int rank = fromRank + 1; rank < toRank; ++rank)
                link.chain.push_back(addItem(-1, rank, 0, 0));
            link.chain.push_back(link.to);
            for (std::size_t i = 0; i + 1 < link.chain.size(); ++i) {
                Item& upper = m_items[static_cast<std::size_t>(link.chain[i])];
                Item& lower = m_items[static_cast<std::size_t>(link.chain[i + 1])];
                const double weight = upper.dummy() && lower.dummy() ? kWeightDummies
                                      : upper.dummy() || lower.dummy() ? kWeightMixed
                                                                       : kWeightReal;
                upper.down.emplace_back(link.chain[i + 1], weight);
                lower.up.emplace_back(link.chain[i], weight);
                m_segments.push_back({link.chain[i], link.chain[i + 1]});
            }
        }

        int rankCount = 0;
        for (const Item& item : m_items)
            rankCount = std::max(rankCount, item.rank + 1);
        m_layers.assign(static_cast<std::size_t>(rankCount), {});

        // Start order: depth-first from the items without an upper neighbour.
        std::vector<bool> seen(m_items.size(), false);
        std::vector<std::pair<int, std::size_t>> stack;
        for (std::size_t root = 0; root < m_items.size(); ++root) {
            if (seen[root] || !m_items[root].up.empty())
                continue;
            seen[root] = true;
            m_layers[static_cast<std::size_t>(m_items[root].rank)].push_back(static_cast<int>(root));
            stack.emplace_back(static_cast<int>(root), 0);
            while (!stack.empty()) {
                const auto [item, cursor] = stack.back();
                const auto& down = m_items[static_cast<std::size_t>(item)].down;
                if (cursor == down.size()) {
                    stack.pop_back();
                    continue;
                }
                ++stack.back().second;
                const int next = down[cursor].first;
                if (!seen[static_cast<std::size_t>(next)]) {
                    seen[static_cast<std::size_t>(next)] = true;
                    m_layers[static_cast<std::size_t>(m_items[static_cast<std::size_t>(next)].rank)].push_back(next);
                    stack.emplace_back(next, 0);
                }
            }
        }
        m_position.assign(m_items.size(), 0);
        refreshPositions();
    }

    void refreshPositions()
    {
        for (const auto& layer : m_layers) {
            for (std::size_t i = 0; i < layer.size(); ++i)
                m_position[static_cast<std::size_t>(layer[i])] = static_cast<int>(i);
        }
    }

    // --- 4. crossings ------------------------------------------------------------
    struct Segment {
        int upper;
        int lower;
    };

    // Total crossings of the current order: per pair of ranks, the inversions of
    // the lower positions among the segments sorted by upper position.
    [[nodiscard]] long long countCrossings() const
    {
        std::vector<std::vector<std::pair<int, int>>> byRank(m_layers.size());
        for (const Segment& s : m_segments) {
            byRank[static_cast<std::size_t>(m_items[static_cast<std::size_t>(s.upper)].rank)].emplace_back(
                m_position[static_cast<std::size_t>(s.upper)], m_position[static_cast<std::size_t>(s.lower)]);
        }
        long long total = 0;
        for (std::size_t rank = 0; rank + 1 < m_layers.size(); ++rank) {
            auto& pairs = byRank[rank];
            std::sort(pairs.begin(), pairs.end());
            std::vector<int> tree(m_layers[rank + 1].size() + 1, 0); // Fenwick tree over lower positions
            long long seenSoFar = 0;
            for (const auto& [upper, lower] : pairs) {
                int le = 0; // segments seen with a lower position <= `lower`
                for (int i = lower + 1; i > 0; i -= i & -i)
                    le += tree[static_cast<std::size_t>(i)];
                total += seenSoFar - le;
                for (int i = lower + 1; i < static_cast<int>(tree.size()); i += i & -i)
                    ++tree[static_cast<std::size_t>(i)];
                ++seenSoFar;
            }
        }
        return total;
    }

    // Orders `layer` by the barycenter of the neighbours on one side; items
    // without neighbours keep their position as their value.
    void sortByBarycenter(std::size_t rank, bool useUp)
    {
        auto& layer = m_layers[rank];
        std::vector<std::pair<double, int>> keyed; // barycenter, item
        keyed.reserve(layer.size());
        for (const int id : layer) {
            const Item& item = m_items[static_cast<std::size_t>(id)];
            const auto& neighbours = useUp ? item.up : item.down;
            double sum = 0;
            for (const auto& [other, weight] : neighbours)
                sum += m_position[static_cast<std::size_t>(other)];
            keyed.emplace_back(neighbours.empty() ? m_position[static_cast<std::size_t>(id)]
                                                  : sum / static_cast<double>(neighbours.size()),
                               id);
        }
        // Ties keep the current order: stable_sort over the current sequence.
        std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (std::size_t i = 0; i < layer.size(); ++i) {
            layer[i] = keyed[i].second;
            m_position[static_cast<std::size_t>(layer[i])] = static_cast<int>(i);
        }
    }

    // Crossings between the segments of `left` and `right` towards one neighbouring rank.
    static int crossingPairs(const std::vector<std::pair<int, double>>& left,
                             const std::vector<std::pair<int, double>>& right, const std::vector<int>& position,
                             bool leftIsFirst)
    {
        int count = 0;
        for (const auto& [a, wa] : left) {
            for (const auto& [b, wb] : right) {
                const int pa = position[static_cast<std::size_t>(a)];
                const int pb = position[static_cast<std::size_t>(b)];
                if (leftIsFirst ? pa > pb : pa < pb)
                    ++count;
            }
        }
        return count;
    }

    // Swaps neighbours of a rank while that removes crossings.
    void transpose(std::size_t rank)
    {
        auto& layer = m_layers[rank];
        for (int pass = 0; pass < kTranspositionPasses; ++pass) {
            bool improved = false;
            for (std::size_t i = 0; i + 1 < layer.size(); ++i) {
                const Item& u = m_items[static_cast<std::size_t>(layer[i])];
                const Item& v = m_items[static_cast<std::size_t>(layer[i + 1])];
                const int keep = crossingPairs(u.up, v.up, m_position, true) + crossingPairs(u.down, v.down, m_position, true);
                const int swap = crossingPairs(u.up, v.up, m_position, false) + crossingPairs(u.down, v.down, m_position, false);
                if (swap < keep) {
                    std::swap(layer[i], layer[i + 1]);
                    m_position[static_cast<std::size_t>(layer[i])] = static_cast<int>(i);
                    m_position[static_cast<std::size_t>(layer[i + 1])] = static_cast<int>(i + 1);
                    improved = true;
                }
            }
            if (!improved)
                break;
        }
    }

    void reduceCrossings()
    {
        auto best = m_layers;
        long long bestCrossings = countCrossings();
        for (int sweep = 0; sweep < kOrderSweeps && bestCrossings > 0; ++sweep) {
            if (sweep % 2 == 0) {
                for (std::size_t rank = 1; rank < m_layers.size(); ++rank)
                    sortByBarycenter(rank, true);
            } else {
                for (std::size_t rank = m_layers.size(); rank-- > 1;)
                    sortByBarycenter(rank - 1, false);
            }
            for (std::size_t rank = 0; rank < m_layers.size(); ++rank)
                transpose(rank);
            const long long crossings = countCrossings();
            if (crossings < bestCrossings) {
                bestCrossings = crossings;
                best = m_layers;
            }
        }
        m_layers = std::move(best);
        refreshPositions();
    }

    // --- 5. coordinates ------------------------------------------------------------
    [[nodiscard]] double gap(const Item& a, const Item& b) const
    {
        // Edges that pass a node keep half the spacing from it, like the lanes of two edges.
        const double spacing = a.dummy() || b.dummy() ? m_options.intraCellSpacing / 2 : m_options.intraCellSpacing;
        return (a.extentU + b.extentU) / 2 + spacing;
    }

    // Moves the rank to the positions closest (weighted least squares) to the
    // neighbours' centres that keep the gaps: with y_i = u_i - offset_i the
    // constraint is y non-decreasing, solved by pooling adjacent violators.
    void placeRank(std::size_t rank)
    {
        const auto& layer = m_layers[rank];
        const std::size_t n = layer.size();
        std::vector<double> offset(n, 0);
        for (std::size_t i = 1; i < n; ++i) {
            offset[i] = offset[i - 1] + gap(m_items[static_cast<std::size_t>(layer[i - 1])],
                                            m_items[static_cast<std::size_t>(layer[i])]);
        }
        struct Block {
            double weight, mean;
            std::size_t first, count;
        };
        std::vector<Block> blocks;
        for (std::size_t i = 0; i < n; ++i) {
            const Item& item = m_items[static_cast<std::size_t>(layer[i])];
            double weight = 0;
            double sum = 0;
            for (const auto* side : {&item.up, &item.down}) {
                for (const auto& [other, w] : *side) {
                    weight += w;
                    sum += w * m_items[static_cast<std::size_t>(other)].u;
                }
            }
            if (weight == 0) { // isolated in the layered graph: stay where it is
                weight = 1e-3;
                sum = weight * item.u;
            }
            blocks.push_back({weight, sum / weight - offset[i], i, 1});
            while (blocks.size() > 1 && blocks[blocks.size() - 2].mean > blocks.back().mean) {
                const Block top = blocks.back();
                blocks.pop_back();
                Block& below = blocks.back();
                below.mean = (below.mean * below.weight + top.mean * top.weight) / (below.weight + top.weight);
                below.weight += top.weight;
                below.count += top.count;
            }
        }
        for (const Block& block : blocks) {
            for (std::size_t i = block.first; i < block.first + block.count; ++i)
                m_items[static_cast<std::size_t>(layer[i])].u = block.mean + offset[i];
        }
    }

    void assignPositions()
    {
        for (const auto& layer : m_layers) { // packed to start with
            double u = 0;
            for (std::size_t i = 0; i < layer.size(); ++i) {
                Item& item = m_items[static_cast<std::size_t>(layer[i])];
                if (i > 0)
                    u += gap(m_items[static_cast<std::size_t>(layer[i - 1])], item);
                item.u = u;
            }
        }
        for (int sweep = 0; sweep < kPositionSweeps; ++sweep) {
            for (std::size_t rank = 0; rank < m_layers.size(); ++rank)
                placeRank(rank);
            for (std::size_t rank = m_layers.size(); rank-- > 0;)
                placeRank(rank);
        }
    }

    // --- 6. the frame ---------------------------------------------------------------
    FrameLayout frame()
    {
        // V: rank r is as thick as its thickest node, ranks are interRankCellSpacing apart.
        m_rankTop.assign(m_layers.size(), 0);
        m_rankBottom.assign(m_layers.size(), 0);
        double top = 0;
        for (std::size_t rank = 0; rank < m_layers.size(); ++rank) {
            double thickness = 0;
            for (const int id : m_layers[rank])
                thickness = std::max(thickness, m_items[static_cast<std::size_t>(id)].extentV);
            m_rankTop[rank] = top;
            m_rankBottom[rank] = top + thickness;
            top += thickness + m_options.interRankCellSpacing;
        }

        double minU = std::numeric_limits<double>::max();
        double maxU = std::numeric_limits<double>::lowest();
        for (const Item& item : m_items) {
            minU = std::min(minU, item.u - item.extentU / 2);
            maxU = std::max(maxU, item.u + item.extentU / 2);
        }
        m_minU = minU;
        FrameLayout result;
        result.sizeU = maxU - minU;
        result.sizeV = top - m_options.interRankCellSpacing;
        for (std::size_t i = 0; i < nodeCount(); ++i)
            result.centres.push_back(centreOf(m_items[i]));

        for (const Link& link : m_links) {
            const std::size_t k = link.connections.size();
            for (std::size_t j = 0; j < k; ++j) {
                // Parallel connections of a link are spread along the order axis around it.
                const double shift = (static_cast<double>(j) - static_cast<double>(k - 1) / 2) *
                                     m_options.parallelEdgeSpacing;
                std::vector<Point> points = route(link, shift);
                const std::size_t connection = link.connections[j];
                if (m_reversed[connection])
                    std::reverse(points.begin(), points.end());
                result.waypoints.emplace_back(m_connections[connection].edge, std::move(points));
            }
        }
        return result;
    }

    // Frame position (U, V) of an item's centre: x is U, y is V.
    [[nodiscard]] Point centreOf(const Item& item) const
    {
        const auto rank = static_cast<std::size_t>(item.rank);
        return {item.u - m_minU, (m_rankTop[rank] + m_rankBottom[rank]) / 2};
    }

    // The V coordinate `fraction` of the way across the gap below `rank`.
    [[nodiscard]] double gapV(int rank, double fraction) const
    {
        const auto r = static_cast<std::size_t>(rank);
        return m_rankBottom[r] + fraction * (m_rankTop[r + 1] - m_rankBottom[r]);
    }

    // The waypoints of an orthogonal route from the first to the last item of
    // the link: it runs along the rank axis and changes its order position only
    // in the gap between two ranks, so that the segments next to the nodes
    // stay straight. The dummies of a long link carry `shift` (a lone link
    // between neighbouring ranks gets a middle station that does).
    [[nodiscard]] std::vector<Point> route(const Link& link, double shift) const
    {
        struct Station {
            double u;
            int rank; // the rank the station is in; the extra middle station has the rank of the one above
            bool middle = false;
        };
        std::vector<Station> stations;
        for (std::size_t i = 0; i < link.chain.size(); ++i) {
            const Item& item = m_items[static_cast<std::size_t>(link.chain[i])];
            const bool inner = i > 0 && i + 1 < link.chain.size();
            stations.push_back({item.u - m_minU + (inner ? shift : 0), item.rank});
        }
        if (stations.size() == 2 && shift != 0)
            stations.insert(stations.begin() + 1, {(stations[0].u + stations[1].u) / 2 + shift, stations[0].rank, true});

        std::vector<Point> points;
        const auto jog = [&](double v, double from, double to) {
            if (from == to)
                return;
            points.push_back({from, v});
            points.push_back({to, v});
        };
        for (std::size_t i = 0; i + 1 < stations.size(); ++i) {
            const Station& a = stations[i];
            const Station& b = stations[i + 1];
            if (a.middle || b.middle) // the two halves of a gap
                jog(gapV(a.rank, a.middle ? 0.75 : 0.25), a.u, b.u);
            else
                jog(gapV(a.rank, 0.5), a.u, b.u);
        }
        return points;
    }

    std::vector<double> m_extentU;
    std::vector<double> m_extentV;
    std::vector<Connection> m_connections;
    FlowOptions m_options;

    std::vector<bool> m_reversed;
    std::vector<Link> m_links;
    std::vector<int> m_rank;
    std::vector<Item> m_items;
    std::vector<Segment> m_segments;
    std::vector<std::vector<int>> m_layers;
    std::vector<int> m_position;
    std::vector<double> m_rankTop;
    std::vector<double> m_rankBottom;
    double m_minU = 0;
};

bool isHorizontal(FlowDirection direction) { return direction == FlowDirection::Right || direction == FlowDirection::Left; }

} // namespace

Placement flowLayout(const Graph& graph, const FlowOptions& options)
{
    const std::size_t count = graph.nodes.size();
    Placement placement;
    placement.bounds.assign(count, std::nullopt);
    placement.edgePoints.assign(graph.edges.size(), std::nullopt);

    // Locked nodes and the edges to them are out; so are self loops.
    const auto usable = [&](int node) {
        return node >= 0 && graph.nodes[static_cast<std::size_t>(node)].movable;
    };
    std::vector<std::size_t> parent(count);
    std::iota(parent.begin(), parent.end(), 0);
    const auto find = [&](std::size_t node) {
        while (parent[node] != node)
            node = parent[node] = parent[parent[node]];
        return node;
    };
    for (const Edge& edge : graph.edges) {
        if (usable(edge.source) && usable(edge.target) && edge.source != edge.target) {
            const std::size_t a = find(static_cast<std::size_t>(edge.source));
            const std::size_t b = find(static_cast<std::size_t>(edge.target));
            parent[std::max(a, b)] = std::min(a, b); // the smallest node index leads
        }
    }
    std::vector<std::vector<std::size_t>> members(count); // by leader, nodes in node order
    for (std::size_t i = 0; i < count; ++i) {
        if (usable(static_cast<int>(i)))
            members[find(i)].push_back(i);
    }

    const bool horizontal = isHorizontal(options.direction);
    std::vector<std::vector<Connection>> connections(count);
    std::vector<std::size_t> localIndex(count, 0);
    for (const auto& nodes : members) {
        for (std::size_t k = 0; k < nodes.size(); ++k)
            localIndex[nodes[k]] = k;
    }
    for (std::size_t e = 0; e < graph.edges.size(); ++e) {
        const Edge& edge = graph.edges[e];
        if (usable(edge.source) && usable(edge.target) && edge.source != edge.target) {
            const auto source = static_cast<std::size_t>(edge.source);
            connections[find(source)].push_back({static_cast<int>(localIndex[source]),
                                                 static_cast<int>(localIndex[static_cast<std::size_t>(edge.target)]), e});
            placement.edgePoints[e] = std::vector<Point>{};
        }
    }

    struct Placed {
        std::vector<std::size_t> nodes;
        FrameLayout frame;
        double offsetU = 0;
    };
    std::vector<Placed> placed;
    for (std::size_t leader = 0; leader < count; ++leader) {
        if (members[leader].empty())
            continue;
        std::vector<double> extentU;
        std::vector<double> extentV;
        for (const std::size_t node : members[leader]) {
            const Rect& r = graph.nodes[node].bounds;
            extentU.push_back(horizontal ? r.height : r.width);
            extentV.push_back(horizontal ? r.width : r.height);
        }
        placed.push_back({members[leader], Component(std::move(extentU), std::move(extentV),
                                                     std::move(connections[leader]), options).run(), 0});
    }
    if (placed.empty())
        return placement;

    // Components side by side along U, top aligned along V.
    double sizeU = 0;
    double sizeV = 0;
    for (Placed& component : placed) {
        component.offsetU = sizeU;
        sizeU += component.frame.sizeU + options.interHierarchySpacing;
        sizeV = std::max(sizeV, component.frame.sizeV);
    }
    sizeU -= options.interHierarchySpacing;

    Point origin;
    if (options.origin) {
        origin = *options.origin;
    } else {
        origin = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
        for (const Placed& component : placed) {
            for (const std::size_t node : component.nodes) {
                origin.x = std::min(origin.x, graph.nodes[node].bounds.x);
                origin.y = std::min(origin.y, graph.nodes[node].bounds.y);
            }
        }
    }

    // Frame (U, V) to the page: Down is (U, V), Up mirrors V, Right and Left swap the axes.
    const auto toPage = [&](const Point& frame, double offsetU) {
        const double u = frame.x + offsetU;
        const double v = options.direction == FlowDirection::Up || options.direction == FlowDirection::Left
                             ? sizeV - frame.y
                             : frame.y;
        return horizontal ? Point{origin.x + v, origin.y + u} : Point{origin.x + u, origin.y + v};
    };
    for (const Placed& component : placed) {
        for (std::size_t k = 0; k < component.nodes.size(); ++k) {
            const std::size_t node = component.nodes[k];
            const Point centre = toPage(component.frame.centres[k], component.offsetU);
            Rect bounds = graph.nodes[node].bounds;
            bounds.x = centre.x - bounds.width / 2;
            bounds.y = centre.y - bounds.height / 2;
            placement.bounds[node] = bounds;
        }
        for (const auto& [edge, points] : component.frame.waypoints) {
            std::vector<Point> route;
            for (const Point& point : points)
                route.push_back(toPage(point, component.offsetU));
            placement.edgePoints[edge] = std::move(route);
        }
    }
    return placement;
}

} // namespace qraw::commands::layout
