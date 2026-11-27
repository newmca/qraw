// Organic layout after the documented behaviour of mxFastOrganicLayout (draw.io,
// Apache-2.0; the source is not in the upstream clone, so this is a clean-room
// Fruchterman-Reingold layout, DECISIONS.md D-039): a pure function over a
// LayoutGraph.
//
// Every pair of nodes repels with force k^2 / d, every edge attracts with
// d^2 / k, where d is the gap between the node borders (not the centres, so
// large nodes keep their distance) and k is the force constant. The step of an
// iteration is the force, clipped to a temperature that falls linearly to zero.
// Nodes that still overlap at the end are pushed apart.
#include "layout_internal.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace qraw::commands::layout {

namespace {

using core::Point;
using core::Rect;

constexpr double kGoldenAngle = 2.399963229728653; // radians
constexpr double kOverlapGap = 10;                 // minimum gap the final pass leaves
constexpr int kOverlapPasses = 200;

// Linear congruential generator: the same sequence on every platform.
class Random {
public:
    explicit Random(unsigned seed) : m_state(seed * 2654435761u + 12345u) {}
    // In [-1, 1).
    double next()
    {
        m_state = m_state * 1664525u + 1013904223u;
        return static_cast<double>(m_state >> 8) / static_cast<double>(1u << 23) - 1;
    }

private:
    unsigned m_state;
};

struct Body {
    double x = 0, y = 0;   // centre
    double hw = 0, hh = 0; // half size
    double dx = 0, dy = 0; // displacement of this iteration
};

// Where two boxes meet along the line of their centres: the unit vector from
// `a` to `b` and `gap`, how far apart the boxes are along that line (negative
// while they overlap; positive exactly when they are disjoint, since the line
// leaves the Minkowski sum of the boxes where their borders touch). `padding`
// inflates both boxes.
struct Separation {
    double ux, uy;
    double gap;
};

Separation separationOf(const Body& a, const Body& b, std::size_t ia, std::size_t ib, double padding = 0)
{
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    double distance = std::sqrt(dx * dx + dy * dy);
    if (distance < 1e-9) { // on top of each other: a direction that depends only on the order
        dx = ib > ia ? 1 : -1;
        dy = 0.5;
        distance = std::sqrt(dx * dx + dy * dy);
    }
    const double ux = dx / distance;
    const double uy = dy / distance;
    const double inf = std::numeric_limits<double>::max();
    const double tx = std::abs(ux) > 1e-12 ? (a.hw + b.hw + padding) / std::abs(ux) : inf;
    const double ty = std::abs(uy) > 1e-12 ? (a.hh + b.hh + padding) / std::abs(uy) : inf;
    return {ux, uy, distance - std::min(tx, ty)};
}

// Pushes overlapping pairs apart along the line of their centres until every
// pair is `kOverlapGap` apart (on one axis at least).
void separateOverlaps(std::vector<Body>& bodies)
{
    for (int pass = 0; pass < kOverlapPasses; ++pass) {
        bool moved = false;
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            for (std::size_t j = i + 1; j < bodies.size(); ++j) {
                const Separation s = separationOf(bodies[i], bodies[j], i, j, kOverlapGap);
                if (s.gap >= 0)
                    continue;
                moved = true;
                const double push = -s.gap / 2 + 1e-6;
                bodies[i].x -= s.ux * push;
                bodies[i].y -= s.uy * push;
                bodies[j].x += s.ux * push;
                bodies[j].y += s.uy * push;
            }
        }
        if (!moved)
            return;
    }
}

} // namespace

Placement organicLayout(const Graph& graph, const OrganicOptions& options)
{
    Placement placement;
    placement.bounds.assign(graph.nodes.size(), std::nullopt);

    std::vector<std::size_t> active; // graph node of each body
    std::vector<int> bodyOf(graph.nodes.size(), -1);
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        if (graph.nodes[i].movable) {
            bodyOf[i] = static_cast<int>(active.size());
            active.push_back(i);
        }
    }
    const std::size_t count = active.size();
    if (count == 0)
        return placement;

    std::vector<std::pair<std::size_t, std::size_t>> links;
    for (const Edge& edge : graph.edges) {
        if (edge.source < 0 || edge.target < 0 || edge.source == edge.target)
            continue;
        const int a = bodyOf[static_cast<std::size_t>(edge.source)];
        const int b = bodyOf[static_cast<std::size_t>(edge.target)];
        if (a >= 0 && b >= 0)
            links.emplace_back(std::min(a, b), std::max(a, b));
    }
    std::sort(links.begin(), links.end());
    links.erase(std::unique(links.begin(), links.end()), links.end());

    std::vector<Body> bodies(count);
    double largest = 0;
    Point origin{std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    for (std::size_t i = 0; i < count; ++i) {
        const Rect& r = graph.nodes[active[i]].bounds;
        bodies[i].hw = r.width / 2;
        bodies[i].hh = r.height / 2;
        largest = std::max(largest, std::max(r.width, r.height));
        origin.x = std::min(origin.x, r.x);
        origin.y = std::min(origin.y, r.y);
    }
    if (options.origin)
        origin = *options.origin;

    // Start: a sunflower spiral in node order, with a little seeded jitter.
    const double spacing = options.forceConstant + largest;
    Random random(options.seed);
    for (std::size_t i = 0; i < count; ++i) {
        const double angle = static_cast<double>(i) * kGoldenAngle;
        const double radius = spacing * std::sqrt(static_cast<double>(i) + 0.5);
        bodies[i].x = radius * std::cos(angle) + random.next() * spacing * 0.1;
        bodies[i].y = radius * std::sin(angle) + random.next() * spacing * 0.1;
    }

    const double k = options.forceConstant;
    const int iterations = options.maxIterations > 0
                               ? options.maxIterations
                               : std::max(1, static_cast<int>(std::lround(20 * std::sqrt(static_cast<double>(count)))));
    for (int iteration = 0; iteration < iterations && count > 1; ++iteration) {
        const double temperature = options.initialTemp * (1 - static_cast<double>(iteration) / iterations);
        for (Body& body : bodies)
            body.dx = body.dy = 0;

        for (std::size_t i = 0; i < count; ++i) {
            for (std::size_t j = i + 1; j < count; ++j) {
                const Separation s = separationOf(bodies[i], bodies[j], i, j);
                if (s.gap > options.maxDistanceLimit)
                    continue;
                const double force = k * k / std::max(s.gap, options.minDistanceLimit);
                bodies[i].dx -= s.ux * force;
                bodies[i].dy -= s.uy * force;
                bodies[j].dx += s.ux * force;
                bodies[j].dy += s.uy * force;
            }
        }
        for (const auto& [a, b] : links) {
            const Separation s = separationOf(bodies[a], bodies[b], a, b);
            const double gap = std::max(s.gap, 0.0);
            const double force = gap * gap / k;
            bodies[a].dx += s.ux * force;
            bodies[a].dy += s.uy * force;
            bodies[b].dx -= s.ux * force;
            bodies[b].dy -= s.uy * force;
        }
        for (Body& body : bodies) {
            const double length = std::max(options.minDistanceLimit, std::sqrt(body.dx * body.dx + body.dy * body.dy));
            const double step = std::min(length, temperature) / length;
            body.x += body.dx * step;
            body.y += body.dy * step;
        }
    }
    separateOverlaps(bodies);

    // Top-left of the result at the origin; whole numbers like the other layouts.
    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    for (const Body& body : bodies) {
        minX = std::min(minX, body.x - body.hw);
        minY = std::min(minY, body.y - body.hh);
    }
    for (std::size_t i = 0; i < count; ++i) {
        Rect bounds = graph.nodes[active[i]].bounds;
        bounds.x = std::round(origin.x + bodies[i].x - bodies[i].hw - minX);
        bounds.y = std::round(origin.y + bodies[i].y - bodies[i].hh - minY);
        placement.bounds[active[i]] = bounds;
    }
    return placement;
}

} // namespace qraw::commands::layout
