// Outline / minimap (OutlineWindow, js/grapheditor/Dialogs.js:4632, mxOutline).

#include "qraw/ui/outline_view.h"

#include "qraw/ui/canvas_view.h"
#include "qraw/ui/diagram_scene.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>

namespace qraw::ui {

namespace {

constexpr int kViewportRed = 0x3d;
constexpr int kViewportGreen = 0x9e;
constexpr int kViewportBlue = 0xf1;
constexpr qreal kViewportPenWidth = 2;

// The canvas scene rectangle fitted, centred and aspect-preserving, into the
// widget (mxOutline's scale: the whole scene is always visible).
struct Fit {
    QRectF scene;  // the canvas scene rectangle
    QRectF target; // where it is drawn in widget coordinates
    qreal scale = 1;

    static Fit of(const QRectF& scene, const QSizeF& widget)
    {
        Fit fit;
        fit.scene = scene;
        if (scene.isEmpty() || widget.isEmpty()) {
            fit.target = QRectF(QPointF(0, 0), widget);
            return fit;
        }
        fit.scale = std::min(widget.width() / scene.width(), widget.height() / scene.height());
        const QSizeF drawn = scene.size() * fit.scale;
        fit.target = QRectF(QPointF((widget.width() - drawn.width()) / 2, (widget.height() - drawn.height()) / 2),
                            drawn);
        return fit;
    }

    [[nodiscard]] QPointF toWidget(const QPointF& p) const
    {
        return target.topLeft() + (p - scene.topLeft()) * scale;
    }

    [[nodiscard]] QPointF toScene(const QPointF& p) const
    {
        return scene.topLeft() + (p - target.topLeft()) / scale;
    }

    [[nodiscard]] QRectF toWidget(const QRectF& r) const { return QRectF(toWidget(r.topLeft()), r.size() * scale); }
};

} // namespace

struct OutlineView::Impl {
    explicit Impl(CanvasView& view) : canvas(view) {}

    CanvasView& canvas;
    QPixmap cache;          // the scene, rendered at the widget's device pixel ratio
    bool cacheValid = false;
    bool dragging = false;
    QPointF dragOffset;     // scene distance from the grabbed point to the viewport centre

    // Renders the whole canvas scene into the cache (white where no scene is drawn).
    void render(const QSize& size, qreal devicePixelRatio)
    {
        cacheValid = true;
        if (size.isEmpty())
            return;
        cache = QPixmap(QSize(qRound(size.width() * devicePixelRatio), qRound(size.height() * devicePixelRatio)));
        cache.setDevicePixelRatio(devicePixelRatio);
        cache.fill(Qt::white);
        const Fit fit = Fit::of(canvas.sceneRect(), size);
        QPainter painter(&cache);
        canvas.diagramScene().render(&painter, fit.target, fit.scene, Qt::IgnoreAspectRatio);
    }

    // The part of the scene the canvas currently shows.
    [[nodiscard]] QRectF visibleScene() const
    {
        return canvas.mapToScene(canvas.viewport()->rect()).boundingRect();
    }

    [[nodiscard]] QPointF visibleCentre() const { return canvas.mapToScene(canvas.viewport()->rect().center()); }
};

OutlineView::OutlineView(CanvasView& canvas, QWidget* parent) : QWidget(parent), d(std::make_unique<Impl>(canvas))
{
    setMinimumSize(120, 90);

    const auto invalidate = [this] {
        d->cacheValid = false;
        update();
    };
    connect(&canvas.diagramScene(), &DiagramScene::projectionChanged, this, invalidate);
    connect(&canvas, &CanvasView::zoomChanged, this, [this] { update(); });
    connect(canvas.horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { update(); });
    connect(canvas.verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { update(); });
}

OutlineView::~OutlineView() = default;

QRectF OutlineView::viewportRect() const
{
    return Fit::of(d->canvas.sceneRect(), size()).toWidget(d->visibleScene());
}

void OutlineView::paintEvent(QPaintEvent* /*event*/)
{
    if (!d->cacheValid)
        d->render(size(), devicePixelRatioF());

    QPainter painter(this);
    painter.drawPixmap(QPointF(0, 0), d->cache);

    QPen pen(QColor(kViewportRed, kViewportGreen, kViewportBlue));
    pen.setWidthF(kViewportPenWidth);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(viewportRect());
}

// Pressing inside the viewport rectangle grabs it; pressing elsewhere centres
// the canvas on the press point first (mxOutline behaviour). Dragging keeps
// the grabbed point at the same offset from the viewport centre.
void OutlineView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPointF scenePoint = Fit::of(d->canvas.sceneRect(), size()).toScene(event->position());
    if (!viewportRect().contains(event->position()))
        d->canvas.centerOn(QRectF(scenePoint, QSizeF()));
    d->dragOffset = scenePoint - d->visibleCentre();
    d->dragging = true;
    event->accept();
}

void OutlineView::mouseMoveEvent(QMouseEvent* event)
{
    if (!d->dragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const QPointF scenePoint = Fit::of(d->canvas.sceneRect(), size()).toScene(event->position());
    d->canvas.centerOn(QRectF(scenePoint - d->dragOffset, QSizeF()));
    event->accept();
}

void OutlineView::mouseReleaseEvent(QMouseEvent* event)
{
    d->dragging = false;
    QWidget::mouseReleaseEvent(event);
}

void OutlineView::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    if (delta > 0)
        d->canvas.zoomIn();
    else if (delta < 0)
        d->canvas.zoomOut();
    event->accept();
}

void OutlineView::resizeEvent(QResizeEvent* event)
{
    d->cacheValid = false;
    QWidget::resizeEvent(event);
}

} // namespace qraw::ui
