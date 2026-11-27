#include "qraw/ui/snapping.h"

#include "qraw/render/page_view.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"
#include "qraw/ui/interaction.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_set>

namespace qraw::ui {

namespace {

// The three guide lines of one axis: leading edge, centre, trailing edge
// (mxGuide.move compares exactly these).
struct AxisLines {
    double values[3];
};

AxisLines xLines(const QRectF& r)
{
    return {{r.left(), r.center().x(), r.right()}};
}

AxisLines yLines(const QRectF& r)
{
    return {{r.top(), r.center().y(), r.bottom()}};
}

// The best match found on one axis.
struct AxisMatch {
    double distance = std::numeric_limits<double>::infinity(); // signed: state line - moved line
    double line = 0;                                           // the state's line value (guide position)
    QRectF state;
};

// mxGuide.move: the closest of the 3x3 line pairs within `tolerance`.
void matchAxis(const AxisLines& moved, const AxisLines& other, const QRectF& otherRect, double tolerance,
               AxisMatch& best)
{
    for (const double m : moved.values) {
        for (const double o : other.values) {
            const double d = o - m;
            if (std::abs(d) <= tolerance && std::abs(d) < std::abs(best.distance)) {
                best.distance = d;
                best.line = o;
                best.state = otherRect;
            }
        }
    }
}

bool found(const AxisMatch& match)
{
    return std::isfinite(match.distance);
}

QRectF toQRect(const core::Rect& r)
{
    return {r.x, r.y, r.width, r.height};
}

// True when the state, or one of its ancestors, is moving.
bool isMovingOrInside(const std::vector<render::CellState>& states, const render::CellState& state,
                      const std::unordered_set<std::string>& moving)
{
    std::size_t guard = 0;
    for (const render::CellState* s = &state; s != nullptr && guard <= states.size(); ++guard) {
        if (moving.count(s->id) != 0)
            return true;
        s = s->parent == render::CellState::npos ? nullptr : &states[s->parent];
    }
    return false;
}

} // namespace

Snapper::Snapper(const DiagramScene& scene, bool guidesEnabled) : scene_(scene), guidesEnabled_(guidesEnabled) {}

double Snapper::snap(double value) const
{
    const core::Page* page = scene_.session().currentPage();
    if (page == nullptr || !page->gridEnabled())
        return value;
    const double size = page->gridSize();
    if (size <= 0)
        return value;
    return std::round(value / size) * size;
}

QPointF Snapper::snap(QPointF point) const
{
    return {snap(point.x()), snap(point.y())};
}

SnapResult Snapper::snapMove(const QRectF& bounds, QPointF delta, const std::vector<std::string>& moving,
                             Qt::KeyboardModifiers modifiers, double pixel) const
{
    SnapResult result{delta, {}};
    if (modifiers & Qt::AltModifier)
        return result; // mxGraphHandler.isGridEnabledEvent: Alt disables grid and guides

    const core::Page* page = scene_.session().currentPage();
    const bool gridOn = page != nullptr && page->gridEnabled() && page->gridSize() > 0;

    AxisMatch matchX;
    AxisMatch matchY;
    if (guidesEnabled_) {
        // mxGuide.getGuideTolerance
        const double tolerance = gridOn ? page->gridSize() / 2 : overlay::kHitTolerance * pixel;
        const QRectF moved = bounds.translated(delta);
        const std::unordered_set<std::string> movingSet(moving.begin(), moving.end());
        const auto& states = scene_.pageView().states();
        for (const render::CellState& state : states) {
            if (state.kind != render::CellState::Kind::Vertex || isMovingOrInside(states, state, movingSet))
                continue;
            const QRectF other = toQRect(state.bounds);
            matchAxis(xLines(moved), xLines(other), other, tolerance, matchX);
            matchAxis(yLines(moved), yLines(other), other, tolerance, matchY);
        }
    }

    // An axis without a guide falls back to the grid: the moved top-left
    // lands on a grid multiple (mxGraphHandler.snap).
    if (found(matchX))
        result.delta.rx() += matchX.distance;
    else if (gridOn)
        result.delta.rx() = snap(bounds.x() + delta.x()) - bounds.x();
    if (found(matchY))
        result.delta.ry() += matchY.distance;
    else if (gridOn)
        result.delta.ry() = snap(bounds.y() + delta.y()) - bounds.y();

    // Guide lines span the moved bounds and the matched state (final position).
    const QRectF finalBounds = bounds.translated(result.delta);
    if (found(matchX)) {
        const QRectF& other = matchX.state;
        result.guides.emplace_back(matchX.line, std::min(finalBounds.top(), other.top()), matchX.line,
                                   std::max(finalBounds.bottom(), other.bottom()));
    }
    if (found(matchY)) {
        const QRectF& other = matchY.state;
        result.guides.emplace_back(std::min(finalBounds.left(), other.left()), matchY.line,
                                   std::max(finalBounds.right(), other.right()), matchY.line);
    }
    return result;
}

QRectF Snapper::snapResize(const QRectF& bounds, Qt::KeyboardModifiers modifiers) const
{
    if (modifiers & Qt::AltModifier)
        return bounds;
    const double left = snap(bounds.left());
    const double top = snap(bounds.top());
    const double right = snap(bounds.right());
    const double bottom = snap(bounds.bottom());
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

} // namespace qraw::ui
