#include "qraw/ui/freehand_tool.h"

#include "default_styles.h"

#include "qraw/commands/default_style.h"
#include "qraw/commands/freehand.h"
#include "qraw/format/compression.h"
#include "qraw/render/stylesheet.h"
#include "qraw/ui/app_settings.h"
#include "qraw/ui/canvas_view.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/cell_transfer.h"
#include "qraw/ui/document_session.h"
#include "qraw/ui/insert_actions.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <vector>

namespace qraw::ui {

namespace {

constexpr char kStrokeShapeStyle[] = "fillColor=none;whiteSpace=wrap;html=1;";
constexpr char kStrokeId[] = "freehand";

// Pointer positions closer than this (screen pixels) to the last one are not kept.
constexpr double kMinStepPixels = 1;

std::vector<core::Point> toCorePoints(std::span<const QPointF> points)
{
    std::vector<core::Point> result;
    result.reserve(points.size());
    for (const QPointF& p : points)
        result.push_back({p.x(), p.y()});
    return result;
}

// The stencil of a stroke as a style value: draw.io's compressed form.
std::string shapeValue(const commands::freehand::Stroke& stroke)
{
    return "stencil(" + format::compressDiagram(stroke.stencilXml) + ")";
}

core::Style strokeStyle(const std::string& shape)
{
    const core::Style entry("shape=" + shape + ";" + kStrokeShapeStyle);
    core::Style styled = commands::applyDefaultStyle(entry, AppSettings().defaultStyles(), commands::StyleTarget::Vertex);
    if (!styled.has("strokeWidth"))
        styled.set("strokeWidth", std::to_string(static_cast<int>(kDefaultFreehandWidth)));
    return styled;
}

// What the live stroke looks like: the style resolved in the diagram's scheme.
class Drawing final : public Gesture {
public:
    Drawing(ToolContext context, const CanvasEvent& start)
        : context_(context), brush_(freehandBrush(*context.session)), scale_(start.scale)
    {
        points_.push_back(start.scenePos);
    }

    void move(const CanvasEvent& event) override
    {
        scale_ = event.scale;
        const QPointF last = points_.back();
        if (std::hypot(event.scenePos.x() - last.x(), event.scenePos.y() - last.y()) * scale_ < kMinStepPixels)
            return;
        points_.push_back(event.scenePos);
    }

    void release(const CanvasEvent& event) override
    {
        move(event);
        actions::insertFreehandStroke(*context_.session, points_,
                                      std::max(kFreehandSmoothingPixels / scale_, 0.25));
        points_.clear(); // the overlay is repainted without the stroke
        context_.view->requestOverlayUpdate();
    }

    void cancel() override
    {
        points_.clear();
        context_.view->requestOverlayUpdate();
    }

    void paint(QPainter& painter, const OverlayContext&) const override
    {
        if (points_.size() < 2 || !brush_.color.isValid())
            return;
        painter.save();
        QPen pen(brush_.color, brush_.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.drawPolyline(points_.data(), static_cast<int>(points_.size()));
        painter.restore();
    }

    [[nodiscard]] OverlayBounds overlayBounds(const OverlayContext& context) const override
    {
        if (points_.empty())
            return QRectF();
        double left = points_.front().x();
        double right = left;
        double top = points_.front().y();
        double bottom = top;
        for (const QPointF& p : points_) {
            left = std::min(left, p.x());
            right = std::max(right, p.x());
            top = std::min(top, p.y());
            bottom = std::max(bottom, p.y());
        }
        // The stroke reaches half its width beyond the path; the rest is antialiasing.
        const double pad = brush_.width / 2 + context.pixels(2);
        return QRectF(QPointF(left - pad, top - pad), QPointF(right + pad, bottom + pad));
    }

    [[nodiscard]] Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

private:
    ToolContext context_;
    FreehandBrush brush_;
    double scale_ = 1;
    std::vector<QPointF> points_;
};

} // namespace

FreehandBrush freehandBrush(const DocumentSession& session)
{
    FreehandBrush brush;
    brush.style = strokeStyle("rectangle"); // the shape does not matter for the colours
    const render::RenderContext& context = session.renderContext();
    const render::Stylesheet& sheet =
        context.stylesheet != nullptr ? *context.stylesheet : render::Stylesheet::drawioDefault();
    render::ResolveOptions options;
    // The page's own scheme: a diagram that says adaptiveColors=none stays light in the dark view.
    options.theme = context.theme;
    if (const core::Page* page = session.currentPage())
        options.theme = context.theme.forPage(render::parseAdaptiveColors(page->adaptiveColors()));
    const render::ResolvedStyle resolved = sheet.resolve(brush.style, render::CellKind::Vertex, options);
    const std::optional<QColor> color = resolved.strokeColor();
    // A stroke colour of none draws nothing (and is still a valid style).
    brush.color = color.value_or(QColor());
    if (brush.color.isValid() && resolved.strokeOpacity() < 100)
        brush.color.setAlphaF(brush.color.alphaF() * resolved.strokeOpacity() / 100);
    brush.width = std::max(resolved.strokeWidth(), 0.5);
    return brush;
}

namespace actions {

bool insertFreehandStroke(DocumentSession& session, std::span<const QPointF> points, double tolerance)
{
    if (!canInsert(session))
        return false;
    const std::optional<commands::freehand::Stroke> stroke =
        commands::freehand::buildStroke(toCorePoints(points), tolerance);
    if (!stroke)
        return false;
    const core::Rect bounds{stroke->bounds.x, stroke->bounds.y, stroke->bounds.width, stroke->bounds.height};
    CellBundle bundle;
    bundle.cells.push_back(core::Cell::makeVertex(kStrokeId, "", "", strokeStyle(shapeValue(*stroke)).toString(), bounds));
    const std::size_t page = session.currentPageIndex();
    const std::string parent = session.insertionParent();
    // The defaults are applied already (strokeStyle), and the stroke stays where it was drawn: no snapping.
    const commands::Result result = session.edit(QCoreApplication::translate("qraw::ui::actions", "Freehand"),
                                                 [&](commands::Transaction& tx) {
                                                     return insertBundle(tx, page, bundle, parent, 0, 0);
                                                 });
    return result.ok;
}

} // namespace actions

FreehandTool::FreehandTool(ToolContext context) : context_(context) {}

void FreehandTool::setActive(bool active)
{
    if (active_ == active)
        return;
    active_ = active;
    if (activeChanged_)
        activeChanged_(active_);
}

std::optional<Qt::CursorShape> FreehandTool::hover(const CanvasEvent&)
{
    if (!active_)
        return std::nullopt;
    return Qt::CrossCursor;
}

std::unique_ptr<Gesture> FreehandTool::press(const CanvasEvent& event)
{
    if (!active_ || event.button != Qt::LeftButton || !actions::canInsert(*context_.session))
        return nullptr;
    context_.session->selection().clear();
    return std::make_unique<Drawing>(context_, event);
}

bool FreehandTool::keyPress(QKeyEvent& event)
{
    // A running stroke is cancelled by the controller; Escape between strokes leaves the tool.
    if (!active_ || event.key() != Qt::Key_Escape)
        return false;
    setActive(false);
    return true;
}

OverlayBounds FreehandTool::overlayBounds(const OverlayContext&) const
{
    return QRectF(); // the stroke being drawn is the gesture's overlay
}

} // namespace qraw::ui
