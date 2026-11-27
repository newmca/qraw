#include "select_resize.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_properties.h"
#include "qraw/commands/ops_tables.h"
#include "qraw/commands/ops_transform.h"
#include "qraw/commands/queries.h"
#include "qraw/core/number.h"
#include "qraw/geometry/path.h"
#include "qraw/ui/active_layer.h"
#include "qraw/ui/canvas_view.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"
#include "qraw/ui/snapping.h"

#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace qraw::ui {

namespace {

constexpr double kMinSize = 1;

// mxVertexHandler.rotationRaster: the angle step depends on how far the
// pointer is beyond the handle's start distance (15, 5 or 1 degrees).
double rasterFor(double distancePx, double startDistancePx)
{
    const double beyond = distancePx - startDistancePx;
    if (beyond < 2)
        return 15;
    return beyond < 25 ? 5 : 1;
}

QPointF fractionPoint(const QRectF& rect, SizerSpec at)
{
    return {rect.left() + at.fx * rect.width(), rect.top() + at.fy * rect.height()};
}

double round2(double value)
{
    return std::floor(value * 100 + 0.5) / 100;
}

// The cells' state for the previews of the union gestures.
std::vector<UnionCell> snapshotCells(const ToolContext& ctx, const std::vector<std::string>& ids)
{
    std::vector<UnionCell> cells;
    for (const std::string& id : ids) {
        const render::CellState* state = ctx.scene != nullptr ? ctx.scene->state(id) : nullptr;
        if (state == nullptr || state->cell == nullptr)
            continue;
        UnionCell cell;
        cell.id = id;
        cell.vertex = state->kind == render::CellState::Kind::Vertex;
        cell.bounds = toQRect(state->bounds);
        cell.rotation = state->style.rotation();
        cell.style = state->cell->style();
        cell.resizable = styleAllows(*state, "resizable");
        // Graph.isMultiSelectionRotatable (without the table rows and cells,
        // which never get a union box).
        cell.rotatable = styleAllows(*state, "rotatable") && !commands::ops::isTable(*state->cell) &&
                         cell.style.value("transparentBounds", "0") != "1" &&
                         cell.style.value("labelAutoRotate", "0") != "1";
        for (const auto& p : state->points)
            cell.points.emplace_back(p.x, p.y);
        cells.push_back(std::move(cell));
    }
    return cells;
}

// Dashed outline of a cell after the transform: `mapPoint` takes absolute
// points; `scale` is the factor of its size and `turn` its added rotation.
void paintMappedCell(QPainter& painter, const UnionCell& cell, const std::function<QPointF(QPointF)>& mapPoint,
                     QPointF scale, double turn)
{
    if (cell.vertex) {
        const QPointF size(cell.bounds.width() * scale.x(), cell.bounds.height() * scale.y());
        const QPointF center = mapPoint(cell.bounds.center());
        const QRectF rect(center.x() - size.x() / 2, center.y() - size.y() / 2, size.x(), size.y());
        paintRotatedRect(painter, rect, cell.rotation + turn);
        return;
    }
    QPolygonF polyline;
    for (const QPointF& p : cell.points)
        polyline << mapPoint(p);
    painter.save();
    painter.setPen(selectionPen());
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(polyline);
    painter.restore();
}

} // namespace

// --- pure resize math --------------------------------------------------------------

QRectF resizeFromSizer(const QRectF& original, SizerSpec sizer, QPointF delta, bool keepAspect)
{
    double left = original.left() + (sizer.movesLeft() ? delta.x() : 0);
    double right = original.right() + (sizer.movesRight() ? delta.x() : 0);
    double top = original.top() + (sizer.movesTop() ? delta.y() : 0);
    double bottom = original.bottom() + (sizer.movesBottom() ? delta.y() : 0);

    double width = right - left;
    double height = bottom - top;
    if (keepAspect && original.width() > 0 && original.height() > 0) {
        if (sizer.isCorner()) {
            const double scale = std::max(width / original.width(), height / original.height());
            width = original.width() * scale;
            height = original.height() * scale;
        } else if (sizer.fx == 0.5) {
            width = original.width() * height / original.height();
        } else {
            height = original.height() * width / original.width();
        }
    }
    width = std::max(width, kMinSize);
    height = std::max(height, kMinSize);

    // Anchor on the fixed side; a sizer that does not move an axis keeps it
    // centred (only matters when the aspect ratio changed that axis).
    left = sizer.movesLeft() ? original.right() - width
           : sizer.movesRight() ? original.left()
                                : original.center().x() - width / 2;
    top = sizer.movesTop() ? original.bottom() - height
          : sizer.movesBottom() ? original.top()
                                : original.center().y() - height / 2;
    return {left, top, width, height};
}

QRectF resizeUnionBounds(const QRectF& b, SizerSpec sizer, QPointF delta, const UnionResizeOptions& options)
{
    const auto snap = [&](double value) { return options.snap ? options.snap(value) : value; };
    double left = b.left();
    double top = b.top();
    double right = b.right();
    double bottom = b.bottom();
    if (sizer.movesBottom())
        bottom = snap(bottom + delta.y());
    else if (sizer.movesTop())
        top = snap(top + delta.y());
    if (sizer.movesLeft())
        left = snap(left + delta.x());
    else if (sizer.movesRight())
        right = snap(right + delta.x());

    double width = right - left;
    double height = bottom - top;
    if (options.keepAspect && b.width() > 0 && b.height() > 0) {
        const double aspect = b.width() / b.height();
        // The top and bottom sizers and the right corners follow the height.
        if (sizer.fy != 0.5 && sizer.fx != 0)
            width = height * aspect;
        else
            height = width / aspect;
        if (sizer.fx == 0 && sizer.fy == 0) {
            left = right - width;
            top = bottom - height;
        }
    }
    if (options.centered) {
        width += width - b.width();
        height += height - b.height();
        left = b.center().x() - width / 2;
        top = b.center().y() - height / 2;
    }
    // A minimum size of one unit (less when the cells are smaller).
    const double minWidth = std::min(1.0, b.width());
    const double minHeight = std::min(1.0, b.height());
    if (width < minWidth) {
        left = options.centered ? b.center().x() - minWidth / 2 : (sizer.movesLeft() ? right - minWidth : left);
        width = minWidth;
    }
    if (height < minHeight) {
        top = options.centered ? b.center().y() - minHeight / 2 : (sizer.movesTop() ? bottom - minHeight : top);
        height = minHeight;
    }
    return {round2(left), round2(top), round2(width), round2(height)};
}

QRectF keepFixedPoint(const QRectF& original, const QRectF& resized, SizerSpec sizer, double rotationDegrees)
{
    if (std::fmod(rotationDegrees, 360.0) == 0)
        return resized;
    const SizerSpec fixed = sizer.opposite();
    const QPointF before = rotateAround(fractionPoint(original, fixed), original.center(), rotationDegrees);
    const QPointF after = rotateAround(fractionPoint(resized, fixed), resized.center(), rotationDegrees);
    return resized.translated(before - after);
}

// --- ResizeGesture ----------------------------------------------------------------------

ResizeGesture::ResizeGesture(ToolContext context, std::shared_ptr<SelectionUiState> state,
                             const VertexHandles& handles, SizerSpec sizer, const CanvasEvent& press)
    : ctx_(context),
      hidingHandles_(std::move(state)),
      id_(handles.id()),
      original_(handles.bounds()),
      rotation_(handles.rotation()),
      sizer_(sizer),
      pressScene_(press.scenePos),
      result_(handles.bounds())
{
}

// mxVertexHandler.resizeVertex: the pointer delta is taken in the vertex's
// unrotated frame; grid snapping moves the dragged edges only (and is
// skipped for rotated vertices, whose edges are not axis aligned).
void ResizeGesture::update(const CanvasEvent& event)
{
    const QPointF pointerDelta = rotateAround(event.scenePos - pressScene_, QPointF(), -rotation_);
    QRectF rect = resizeFromSizer(original_, sizer_, pointerDelta, false);
    if (rotation_ == 0 && ctx_.scene != nullptr) {
        const QRectF snapped = Snapper(*ctx_.scene).snapResize(rect, event.modifiers);
        rect = QRectF(QPointF(sizer_.movesLeft() ? snapped.left() : rect.left(),
                              sizer_.movesTop() ? snapped.top() : rect.top()),
                      QPointF(sizer_.movesRight() ? snapped.right() : rect.right(),
                              sizer_.movesBottom() ? snapped.bottom() : rect.bottom()));
    }
    // The (snapped) movement of the dragged edges; Shift then constrains it.
    const QPointF edgeDelta(sizer_.movesLeft()    ? rect.left() - original_.left()
                            : sizer_.movesRight() ? rect.right() - original_.right()
                                                  : 0,
                            sizer_.movesTop()      ? rect.top() - original_.top()
                            : sizer_.movesBottom() ? rect.bottom() - original_.bottom()
                                                   : 0);
    rect = resizeFromSizer(original_, sizer_, edgeDelta, event.modifiers & Qt::ShiftModifier);
    result_ = keepFixedPoint(original_, rect, sizer_, rotation_);
}

void ResizeGesture::move(const CanvasEvent& event)
{
    update(event);
    showLive();
}

// The vertex leaves the tiles with its first change of size and is drawn
// from a record of the previewed geometry; the edges attached to it follow.
void ResizeGesture::showLive()
{
    if (ctx_.view == nullptr || (result_ == original_ && !lifted_))
        return;
    ActiveLayer& layer = ctx_.view->activeLayer();
    if (!lifted_)
        lifted_ = layer.liftVertex(id_);
    if (lifted_)
        layer.setVertexGeometry(result_, std::nullopt);
}

void ResizeGesture::endLive(bool edited)
{
    if (!lifted_)
        return;
    lifted_ = false;
    edited ? ctx_.view->activeLayer().commit() : ctx_.view->activeLayer().cancel();
}

void ResizeGesture::cancel()
{
    endLive(false);
}

void ResizeGesture::release(const CanvasEvent& event)
{
    update(event);
    if (ctx_.session == nullptr || result_ == original_) {
        endLive(false);
        return;
    }
    const core::Page* page = ctx_.session->currentPage();
    const core::Cell* cell = page != nullptr ? page->findCell(id_) : nullptr;
    if (cell == nullptr) {
        endLive(false);
        return;
    }
    // Geometry is stored in the parent's coordinates (mxGraph.cellsResized).
    const core::Point origin = commands::origin(*page, cell->parent());
    const core::Rect bounds{result_.x() - origin.x, result_.y() - origin.y, result_.width(), result_.height()};
    const std::size_t pageIndex = ctx_.session->currentPageIndex();
    const commands::Result result = ctx_.session->edit(tr("Resize"), [&](commands::Transaction& tx) {
        return commands::ops::resizeCells(tx, pageIndex, {id_}, {bounds});
    });
    endLive(static_cast<bool>(result));
}

void ResizeGesture::paint(QPainter& painter, const OverlayContext& context) const
{
    (void)context;
    paintRotatedRect(painter, result_, rotation_);
}

namespace {

// The box of `rect` rotated by `degrees` around its centre, grown by the outline's width.
QRectF rotatedOutlineBounds(const QRectF& rect, double degrees, double margin)
{
    const core::Rect box = geometry::rotatedBounds(toCoreRect(rect), degrees);
    return QRectF(box.x, box.y, box.width, box.height).adjusted(-margin, -margin, margin, margin);
}

} // namespace

OverlayBounds ResizeGesture::overlayBounds(const OverlayContext& context) const
{
    return rotatedOutlineBounds(result_, rotation_, context.pixels(8));
}

// --- RotateGesture ----------------------------------------------------------------------

RotateGesture::RotateGesture(ToolContext context, std::shared_ptr<SelectionUiState> state,
                             const VertexHandles& handles, const CanvasEvent& press)
    : ctx_(context),
      hidingHandles_(std::move(state)),
      id_(handles.id()),
      bounds_(handles.bounds()),
      startRotation_(handles.rotation()),
      alpha_(handles.rotation())
{
    const QPointF offset = handles.rotationHandleOffset();
    startAngle_ = angleOf(offset);
    startDistance_ = std::hypot(offset.x(), offset.y()) * press.scale;
}

// mxVertexHandler.rotateVertex: the angle of the pointer around the centre
// minus the angle of the handle's unrotated position, rastered.
void RotateGesture::move(const CanvasEvent& event)
{
    const QPointF vector = event.scenePos - bounds_.center();
    double alpha = angleOf(vector) - startAngle_;
    if (event.modifiers & Qt::AltModifier) {
        alpha = std::round(alpha * 10) / 10; // roundAngle: no raster
    } else {
        const double raster = rasterFor(std::hypot(vector.x(), vector.y()) * event.scale, startDistance_);
        alpha = std::round(alpha / raster) * raster;
    }
    alpha_ = normalizeAngle(alpha);
    showLive();
}

void RotateGesture::showLive()
{
    if (ctx_.view == nullptr || (alpha_ == normalizeAngle(startRotation_) && !lifted_))
        return;
    ActiveLayer& layer = ctx_.view->activeLayer();
    if (!lifted_)
        lifted_ = layer.liftVertex(id_);
    if (lifted_)
        layer.setVertexGeometry(bounds_, alpha_);
}

void RotateGesture::endLive(bool edited)
{
    if (!lifted_)
        return;
    lifted_ = false;
    edited ? ctx_.view->activeLayer().commit() : ctx_.view->activeLayer().cancel();
}

void RotateGesture::cancel()
{
    endLive(false);
}

void RotateGesture::release(const CanvasEvent& event)
{
    move(event);
    if (ctx_.session == nullptr || alpha_ == normalizeAngle(startRotation_)) {
        endLive(false);
        return;
    }
    const std::size_t pageIndex = ctx_.session->currentPageIndex();
    std::optional<std::string> value;
    if (alpha_ != 0)
        value = core::formatNumber(alpha_);
    const commands::Result result = ctx_.session->edit(tr("Rotate"), [&](commands::Transaction& tx) {
        return commands::ops::setStyle(tx, pageIndex, {id_}, "rotation", value);
    });
    endLive(static_cast<bool>(result));
}

void RotateGesture::paint(QPainter& painter, const OverlayContext& context) const
{
    (void)context;
    paintRotatedRect(painter, bounds_, alpha_);
}

OverlayBounds RotateGesture::overlayBounds(const OverlayContext& context) const
{
    return rotatedOutlineBounds(bounds_, alpha_, context.pixels(8));
}

// --- MultiResizeGesture -----------------------------------------------------------------

MultiResizeGesture::MultiResizeGesture(ToolContext context, std::shared_ptr<SelectionUiState> state,
                                       const VertexHandles& handles, SizerSpec sizer, const CanvasEvent& press)
    : ctx_(context),
      hidingHandles_(std::move(state)),
      cells_(snapshotCells(context, handles.cells())),
      original_(handles.contentBounds()),
      sizer_(sizer),
      pressScene_(press.scenePos),
      padding_(original_.left() - handles.bounds().left()),
      result_(handles.contentBounds())
{
}

// MultiSelectionHandler.getResizedBounds: the pointer delta moves the dragged
// sides of the union of the cells; Alt also switches the grid off.
void MultiResizeGesture::update(const CanvasEvent& event)
{
    UnionResizeOptions options;
    options.keepAspect = event.modifiers & Qt::ShiftModifier;
    options.centered = event.modifiers & Qt::AltModifier;
    if (!options.centered && ctx_.scene != nullptr) {
        const Snapper snapper(*ctx_.scene);
        options.snap = [snapper](double value) { return snapper.snap(value); };
    }
    result_ = resizeUnionBounds(original_, sizer_, event.scenePos - pressScene_, options);
}

void MultiResizeGesture::release(const CanvasEvent& event)
{
    update(event);
    constexpr double kEpsilon = 0.01;
    if (ctx_.session == nullptr || cells_.empty() ||
        (std::abs(result_.x() - original_.x()) <= kEpsilon && std::abs(result_.y() - original_.y()) <= kEpsilon &&
         std::abs(result_.width() - original_.width()) <= kEpsilon &&
         std::abs(result_.height() - original_.height()) <= kEpsilon))
        return;
    std::vector<std::string> ids;
    for (const UnionCell& cell : cells_)
        ids.push_back(cell.id);
    const bool scaleStyles = event.modifiers & (Qt::ControlModifier | Qt::MetaModifier);
    const std::size_t pageIndex = ctx_.session->currentPageIndex();
    ctx_.session->edit(tr("Resize"), [&](commands::Transaction& tx) {
        return commands::ops::scaleCellsToBounds(tx, pageIndex, ids, toCoreRect(original_), toCoreRect(result_),
                                                 scaleStyles);
    });
}

void MultiResizeGesture::paint(QPainter& painter, const OverlayContext& context) const
{
    (void)context;
    const double sx = original_.width() > 0 ? result_.width() / original_.width() : 1;
    const double sy = original_.height() > 0 ? result_.height() / original_.height() : 1;
    const auto map = [&](QPointF p) {
        return QPointF(result_.x() + (p.x() - original_.x()) * sx, result_.y() + (p.y() - original_.y()) * sy);
    };
    for (const UnionCell& cell : cells_) {
        QPointF scale(1, 1);
        if (cell.vertex && cell.resizable) {
            const core::Point factor = commands::ops::multiSelectionVertexScale(cell.style, sx, sy);
            scale = {factor.x, factor.y};
        }
        paintMappedCell(painter, cell, map, scale, 0);
    }
    paintRotatedRect(painter, result_.adjusted(-padding_, -padding_, padding_, padding_), 0);
}

// --- MultiRotateGesture -----------------------------------------------------------------

MultiRotateGesture::MultiRotateGesture(ToolContext context, std::shared_ptr<SelectionUiState> state,
                                       const VertexHandles& handles, const CanvasEvent& press)
    : ctx_(context),
      hidingHandles_(std::move(state)),
      cells_(snapshotCells(context, handles.cells())),
      original_(handles.contentBounds()),
      padding_(original_.left() - handles.bounds().left())
{
    const QPointF offset = handles.rotationHandleOffset();
    startAngle_ = angleOf(offset);
    startDistance_ = std::hypot(offset.x(), offset.y()) * press.scale;
}

// MultiSelectionHandler.getAngle: the raster of the single-vertex handle,
// Shift snaps to 15 degrees, Alt switches the raster off; the result is
// between -180 and 180 degrees with one decimal.
void MultiRotateGesture::update(const CanvasEvent& event)
{
    const QPointF vector = event.scenePos - original_.center();
    double alpha = angleOf(vector) - startAngle_;
    if (event.modifiers & Qt::ShiftModifier)
        alpha = std::round(alpha / 15) * 15;
    else if (!(event.modifiers & Qt::AltModifier)) {
        const double raster = rasterFor(std::hypot(vector.x(), vector.y()) * event.scale, startDistance_);
        alpha = std::round(alpha / raster) * raster;
    }
    alpha_ = std::round((normalizeAngle(alpha + 180) - 180) * 10) / 10;
}

void MultiRotateGesture::move(const CanvasEvent& event)
{
    moved_ = true;
    update(event);
}

void MultiRotateGesture::release(const CanvasEvent& event)
{
    if (moved_)
        update(event);
    else
        alpha_ = 90; // a click on the handle turns the cells by a quarter
    if (ctx_.session == nullptr || cells_.empty() || alpha_ == 0)
        return;
    std::vector<std::string> ids;
    for (const UnionCell& cell : cells_)
        ids.push_back(cell.id);
    const std::size_t pageIndex = ctx_.session->currentPageIndex();
    const core::Point center{original_.center().x(), original_.center().y()};
    ctx_.session->edit(tr("Rotate"), [&](commands::Transaction& tx) {
        return commands::ops::rotateCellsAround(tx, pageIndex, ids, alpha_, center);
    });
}

void MultiRotateGesture::paint(QPainter& painter, const OverlayContext& context) const
{
    (void)context;
    const QPointF center = original_.center();
    const auto map = [&](QPointF p) { return rotateAround(p, center, alpha_); };
    for (const UnionCell& cell : cells_)
        paintMappedCell(painter, cell, map, {1, 1}, cell.rotatable ? alpha_ : 0);
    paintRotatedRect(painter, original_.adjusted(-padding_, -padding_, padding_, padding_), alpha_);
}

} // namespace qraw::ui
