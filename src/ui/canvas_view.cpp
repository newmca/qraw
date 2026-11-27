#include "qraw/ui/canvas_view.h"

#include "canvas_layout.h"
#include "qraw/ui/accessibility.h"
#include "qraw/ui/cell_navigation.h"
#include "qraw/ui/active_layer.h"
#include "qraw/render/compositor.h"
#include "qraw/render/image_cache.h"
#include "qraw/render/perf_scope.h"
#include "qraw/render/stats.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/cell_tooltip.h"
#include "qraw/ui/data_actions.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"
#include "qraw/ui/freehand_tool.h"
#include "qraw/ui/label_editor.h"

#include <QDragEnterEvent>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace qraw::ui {

namespace {

constexpr double kFitBorder = 10;         // Graph.fitWindow border
constexpr double kPageMargin = 20;        // gap between the view edge and the page on reset (Actions.js resetView)
constexpr double kFitWindowMaxZoom = 1;   // the toolbar Fit only zooms out
constexpr double kFitSelectionMaxZoom = 2;
constexpr double kClickSlop = 3;          // px a right press may move and still be a click
constexpr int kWheelStep = 120;           // angle units per mouse wheel notch
constexpr int kLinesPerNotch = 3;         // QApplication::wheelScrollLines default
constexpr double kOverlayFringePx = 2;    // repainted around the overlay bounds (antialiasing, handle outlines)
constexpr double kContentFringePx = 2;    // repainted around changed content
constexpr double kNavigateMarginPx = 40;  // room kept around a cell that Tab selected

// Per-view interaction state that the public header has no member for
// (state of a pan drag and of the wheel accumulator). Keyed by view; erased
// in ~CanvasView. GUI thread only.
struct ViewState {
    Qt::MouseButton panButton = Qt::NoButton; // the button that owns the current pan/click
    QPoint pressPos;                          // where it went down (viewport pixels)
    int wheelRemainder = 0;                   // Ctrl+wheel angle units not yet turned into a zoom step
};

std::unordered_map<const CanvasView*, ViewState>& viewStates()
{
    static std::unordered_map<const CanvasView*, ViewState> states;
    return states;
}

ViewState& stateOf(const CanvasView* view)
{
    return viewStates()[view];
}

QRectF toQRect(const core::Rect& r)
{
    return {r.x, r.y, r.width, r.height};
}

QPointF viewportCenter(const QGraphicsView& view)
{
    return {view.viewport()->width() / 2.0, view.viewport()->height() / 2.0};
}

QPointF viewToScene(const QGraphicsView& view, QPointF viewPos)
{
    return view.viewportTransform().inverted().map(viewPos);
}

void scrollBy(QGraphicsView& view, QPoint delta)
{
    view.horizontalScrollBar()->setValue(view.horizontalScrollBar()->value() + delta.x());
    view.verticalScrollBar()->setValue(view.verticalScrollBar()->value() + delta.y());
}

// Scrolls so that `scenePos` appears at `viewPos` (viewport pixels).
void placeScenePoint(QGraphicsView& view, QPointF scenePos, QPointF viewPos)
{
    const QPointF current = view.viewportTransform().map(scenePos);
    scrollBy(view, QPoint(qRound(current.x() - viewPos.x()), qRound(current.y() - viewPos.y())));
}

// draw.io selects the cell under a right click unless it is selected, then
// shows the popup menu.
void requestContextMenu(CanvasView& view, const QMouseEvent& event)
{
    const QPointF scenePos = view.mapToScene(event.position().toPoint());
    const core::Page* page = view.session().currentPage();
    if (page != nullptr) {
        const std::string hit = view.diagramScene().cellAt(scenePos, overlay::kHitTolerance / view.zoom());
        const std::string target =
            hit.empty() ? std::string() : ancestorInScope(*page, hit, view.session().enteredGroup());
        if (!target.empty() && !view.session().selection().contains(target))
            view.session().selection().set({target});
    }
    emit view.contextMenuRequested(event.globalPosition().toPoint(), scenePos);
}

} // namespace

CanvasView::CanvasView(DocumentSession& session, QWidget* parent)
    : QGraphicsView(parent), session_(session), scene_(std::make_unique<DiagramScene>(session))
{
    tiles_ = new TileScheduler(this); // before anything that scrolls or resizes
    setScene(scene_.get());
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    // The cells are painted from tiles, not by items: scene updates are only the
    // label editor's proxy widget. Everything else asks for the rectangle it
    // changed (requestOverlayUpdate, requestContentUpdate). Minimal mode also
    // makes scrolling blit: QGraphicsView::scrollContentsBy scrolls the viewport
    // and exposes only the strips, which are painted from cached tiles.
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    // Every pixel of the viewport is painted by drawBackground (needed by
    // QWidget::scroll's accelerated blit as well).
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    // No frame: every scroll bar change repaints the frame under it, and a style
    // that draws it expensively (a blurred shadow in Darkly: 7 to 20 ms per step
    // at 1600x1000) would then cost more than the whole canvas paint.
    setFrameShape(QFrame::NoFrame);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setDragMode(QGraphicsView::NoDrag);
    setMouseTracking(true);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Diagram"));
    installAccessibility();
    tiles_->setSnapshot(scene_->tileSnapshot(), render::Damage());
    activeLayer_ = new ActiveLayer(*this, *scene_, *tiles_);
    connect(tiles_, &TileScheduler::areaChanged, this, [this](const QRect& rect) { viewport()->update(rect); });
    connect(tiles_, &TileScheduler::displayChanged, this, [this] { viewport()->update(); });
    interaction_ = std::make_unique<InteractionController>(ToolContext{&session_, scene_.get(), this});
    installDefaultHandlers(*interaction_);
    interaction_->freehand()->setActiveChangedCallback([this](bool on) {
        updateCursor();
        emit freehandActiveChanged(on);
    });

    connect(scene_.get(), &DiagramScene::contentDamaged, this,
            [this](const render::Damage& damage) { onContentDamaged(damage); });
    connect(scene_.get(), &DiagramScene::projectionChanged, this, [this] {
        interaction_->reset();
        updateSceneRect();
        requestOverlayUpdate(); // handles follow the changed cells; the content is updated by its damage
    });
    connect(&session_.selection(), &SelectionModel::selectionChanged, this, [this] {
        interaction_->reset();
        requestOverlayUpdate();
        notifyCanvasChanged(*this);
    });
    connect(&session_, &DocumentSession::documentChanged, this, [this] { notifyCanvasChanged(*this); });
    connect(&session_, &DocumentSession::currentPageChanged, this, [this] {
        interaction_->cancelGesture();
        tiles_->reset(); // no tile of the previous page may show
        // The scene rebuilt already (it connected first): show the new page's origin.
        placeScenePoint(*this, scene_->pageArea().topLeft(), QPointF(kPageMargin, kPageMargin));
    });
    updateSceneRect();
    placeScenePoint(*this, scene_->pageArea().topLeft(), QPointF(kPageMargin, kPageMargin));
}

CanvasView::~CanvasView()
{
    viewStates().erase(this);
    activeLayer_->discard(); // nothing to restore: the view goes away with its scene
    // Handlers reference the scene and the view.
    interaction_.reset();
}

void CanvasView::setZoom(double zoom, std::optional<QPointF> viewAnchor)
{
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    if (zoom == zoom_)
        return;
    const QPointF anchor = viewAnchor.value_or(viewportCenter(*this));
    const QPointF sceneAnchor = viewToScene(*this, anchor);
    zoom_ = zoom;
    setTransform(QTransform::fromScale(zoom_, zoom_));
    updateSceneRect(); // the margin is one viewport in scene units: it changes with the zoom
    placeScenePoint(*this, sceneAnchor, anchor);
    tiles_->setView(tileViewport());
    emit zoomChanged(zoom_);
}

void CanvasView::zoomIn()
{
    setZoom(canvas::nextZoomStep(zoom_, true));
}

void CanvasView::zoomOut()
{
    setZoom(canvas::nextZoomStep(zoom_, false));
}

void CanvasView::resetZoom()
{
    setZoom(1);
    placeScenePoint(*this, scene_->pageArea().topLeft(), QPointF(kPageMargin, kPageMargin));
}

namespace {

// Graph.fitWindow: the largest 5 % grid zoom (at most maxZoom) that shows
// `bounds` with a border, then centres it.
void fitRect(CanvasView& view, const QRectF& bounds, double maxZoom)
{
    if (bounds.width() > 0 && bounds.height() > 0) {
        const double cw = view.viewport()->width() - kFitBorder;
        const double ch = view.viewport()->height() - kFitBorder;
        const double scale = canvas::fitScale(std::min(cw / bounds.width(), ch / bounds.height()));
        view.setZoom(std::max(std::min(scale, maxZoom), canvas::zoomSteps().front()));
    }
    view.centerOn(bounds);
}

} // namespace

void CanvasView::fitPage()
{
    const core::Page* page = session_.currentPage();
    if (page == nullptr || !page->pageVisible()) {
        fitWindow();
        return;
    }
    fitRect(*this, QRectF(QPointF(0, 0), canvas::pageSize(*page)), kMaxZoom);
}

void CanvasView::fitWindow()
{
    const render::PageView& content = scene_->pageView();
    fitRect(*this, content.states().empty() ? scene_->pageArea() : toQRect(content.bounds()), kFitWindowMaxZoom);
}

void CanvasView::fitSelection()
{
    const QRectF bounds = scene_->boundsOf(session_.selection().cells());
    if (bounds.isEmpty()) {
        fitWindow();
        return;
    }
    fitRect(*this, bounds, kFitSelectionMaxZoom);
}

void CanvasView::centerOn(const QRectF& sceneRect)
{
    placeScenePoint(*this, sceneRect.center(), viewportCenter(*this));
}

QPointF CanvasView::centerInsertPoint() const
{
    const core::Page* page = session_.currentPage();
    const QPointF center = viewToScene(*this, viewportCenter(*this));
    return page != nullptr ? canvas::snapToGrid(*page, center) : center;
}

QPointF CanvasView::insertPoint() const
{
    // Editor.js getInsertPoint: the mouse position while the pointer is over the canvas.
    if (!lastMouseViewPos_ || !viewport()->rect().contains(lastMouseViewPos_->toPoint()))
        return centerInsertPoint();
    return viewToScene(*this, *lastMouseViewPos_);
}

void CanvasView::setGuidesEnabled(bool on)
{
    guidesEnabled_ = on;
}

bool CanvasView::freehandActive() const
{
    return interaction_->freehand()->isActive();
}

void CanvasView::setFreehandActive(bool on)
{
    interaction_->freehand()->setActive(on);
}

bool CanvasView::editLabel(const std::string& id)
{
    LabelEditor* editor = interaction_->labelEditor();
    return editor != nullptr && editor->startEditing(id);
}

bool CanvasView::navigateCells(CellStep step)
{
    const core::Page* page = session_.currentPage();
    if (page == nullptr)
        return false;
    const std::string current = session_.selection().first();
    const std::string scope = session_.enteredGroup();
    std::string target;
    if (step == CellStep::Container) {
        target = navigation::container(*page, current, scope);
    } else {
        const std::vector<std::string> order = navigation::tabOrder(*page, scene_->pageView(), scope, current);
        if (step == CellStep::FirstChild)
            target = current.empty() ? std::string() : navigation::firstChild(*page, order, current);
        else
            target = navigation::neighbour(order, current, step == CellStep::Next);
    }
    if (target.empty())
        return false;
    session_.selection().set({target});
    const QRectF bounds = scene_->boundsOf({target});
    if (!bounds.isNull())
        ensureVisible(bounds, kNavigateMarginPx, kNavigateMarginPx);
    return true;
}

bool CanvasView::focusNextPrevChild(bool next)
{
    // Tab belongs to the cells while the canvas has the focus. A scene editor (rotated label) and a running
    // gesture keep Qt's behaviour, and so does an empty canvas: Tab must never trap the keyboard.
    const LabelEditor* editor = interaction_->labelEditor();
    const bool editing = editor != nullptr && editor->isEditing();
    if (!editing && !interaction_->isGestureActive()
        && navigateCells(next ? CellStep::Next : CellStep::Previous))
        return true;
    return QGraphicsView::focusNextPrevChild(next);
}

void CanvasView::addDropHandler(CanvasDropHandler* handler)
{
    if (handler != nullptr && std::find(dropHandlers_.begin(), dropHandlers_.end(), handler) == dropHandlers_.end())
        dropHandlers_.push_back(handler);
}

void CanvasView::removeDropHandler(CanvasDropHandler* handler)
{
    std::erase(dropHandlers_, handler);
    if (activeDrop_ == handler)
        activeDrop_ = nullptr;
}

CanvasEvent CanvasView::toCanvasEvent(const QMouseEvent& event) const
{
    CanvasEvent e;
    e.viewPos = event.position();
    e.scenePos = mapToScene(event.position().toPoint());
    e.button = event.button();
    e.buttons = event.buttons();
    e.modifiers = event.modifiers();
    e.scale = zoom_;
    return e;
}

void CanvasView::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        // One zoom step per notch; smooth-scroll devices accumulate to a notch.
        ViewState& state = stateOf(this);
        state.wheelRemainder += event->angleDelta().y();
        const int notches = state.wheelRemainder / kWheelStep;
        state.wheelRemainder -= notches * kWheelStep;
        double target = zoom_;
        for (int i = 0; i < std::abs(notches); ++i)
            target = canvas::nextZoomStep(target, notches > 0);
        setZoom(target, event->position());
        event->accept();
        return;
    }
    if (event->modifiers() & Qt::ShiftModifier) {
        const int delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
        QScrollBar* bar = horizontalScrollBar();
        bar->setValue(bar->value() - qRound(double(delta) / kWheelStep * bar->singleStep() * kLinesPerNotch));
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

bool CanvasView::sceneEditorWantsMouse(const QMouseEvent& event) const
{
    const LabelEditor* editor = interaction_->labelEditor();
    if (editor == nullptr || !editor->isEditing())
        return false;
    return scene()->mouseGrabberItem() != nullptr || editor->sceneEditorAt(mapToScene(event.position().toPoint()));
}

void CanvasView::mousePressEvent(QMouseEvent* event)
{
    if (sceneEditorWantsMouse(*event)) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    // Panning and the context click never reach the interaction controller
    // (mxPanningHandler runs before mxGraphHandler in draw.io).
    const Qt::MouseButton button = event->button();
    ViewState& state = stateOf(this);
    const bool startsPan = button == Qt::MiddleButton || button == Qt::RightButton
        || (button == Qt::LeftButton && spaceHeld_);
    if (startsPan && state.panButton == Qt::NoButton && !interaction_->isGestureActive()) {
        state.panButton = button;
        state.pressPos = event->position().toPoint();
        panStart_ = state.pressPos;
        // A right press only becomes a pan once it moved (else it is a click).
        panning_ = button != Qt::RightButton;
        event->accept();
        updateCursor();
        return;
    }
    if (interaction_->mousePress(toCanvasEvent(*event)))
        event->accept();
    updateCursor();
    requestOverlayUpdate();
}

void CanvasView::mouseMoveEvent(QMouseEvent* event)
{
    if (sceneEditorWantsMouse(*event) && scene()->mouseGrabberItem() != nullptr) {
        QGraphicsView::mouseMoveEvent(event);
        return;
    }
    const CanvasEvent e = toCanvasEvent(*event);
    lastMouseScenePos_ = e.scenePos;
    lastMouseViewPos_ = event->position();
    emit mouseScenePosChanged(e.scenePos);
    ViewState& state = stateOf(this);
    if (state.panButton != Qt::NoButton) {
        const QPoint pos = event->position().toPoint();
        if (!panning_ && std::hypot(pos.x() - state.pressPos.x(), pos.y() - state.pressPos.y()) >= kClickSlop)
            panning_ = true;
        if (panning_) {
            scrollBy(*this, panStart_ - pos);
            panStart_ = pos;
        }
        event->accept();
        updateCursor();
        return;
    }
    if (!(spaceHeld_ && event->buttons() == Qt::NoButton))
        interaction_->mouseMove(e);
    updateCursor();
    requestOverlayUpdate();
}

void CanvasView::mouseReleaseEvent(QMouseEvent* event)
{
    if (scene()->mouseGrabberItem() != nullptr && sceneEditorWantsMouse(*event)) {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }
    ViewState& state = stateOf(this);
    if (state.panButton != Qt::NoButton && state.panButton == event->button()) {
        const bool wasClick = !panning_ && state.panButton == Qt::RightButton;
        state.panButton = Qt::NoButton;
        panning_ = false;
        event->accept();
        updateCursor();
        if (wasClick)
            requestContextMenu(*this, *event);
        return;
    }
    interaction_->mouseRelease(toCanvasEvent(*event));
    updateCursor();
    requestOverlayUpdate();
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (sceneEditorWantsMouse(*event)) {
        QGraphicsView::mouseDoubleClickEvent(event);
        return;
    }
    interaction_->mouseDoubleClick(toCanvasEvent(*event));
    requestOverlayUpdate();
}

QString CanvasView::tooltipAt(QPointF scenePos) const
{
    const core::Page* page = session_.currentPage();
    if (page == nullptr || interaction_->isGestureActive())
        return {};
    if (const LabelEditor* editor = interaction_->labelEditor(); editor != nullptr && editor->isEditing())
        return {};
    const std::string id = scene_->cellAt(scenePos, 4 / zoom_);
    const core::Cell* cell = id.empty() ? nullptr : page->findCell(id);
    if (cell == nullptr)
        return {};
    return cellTooltip(*page, *cell, session_.pageRenderContext().placeholders,
                       [this](const std::string& link) { return actions::linkDisplayText(session_, link); });
}

bool CanvasView::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::Leave)
        lastMouseViewPos_.reset(); // Editor.js mouseleave: the pointer no longer names an insert point
    if (event->type() != QEvent::ToolTip)
        return QGraphicsView::viewportEvent(event);
    const auto* help = static_cast<QHelpEvent*>(event);
    const QString text = tooltipAt(mapToScene(help->pos()));
    if (text.isEmpty()) {
        QToolTip::hideText();
        event->ignore();
    } else {
        QToolTip::showText(help->globalPos(), text, viewport());
    }
    return true;
}

void CanvasView::keyPressEvent(QKeyEvent* event)
{
    if (interaction_->keyPress(*event)) {
        event->accept();
        requestOverlayUpdate();
        return;
    }
    // The scene editor of a rotated label gets all keys (arrows, space).
    if (const LabelEditor* editor = interaction_->labelEditor(); editor != nullptr && editor->isEditing()) {
        QGraphicsView::keyPressEvent(event);
        return;
    }
    // Alt+Tab and Alt+Shift+Tab go into and out of containers (Qt only moves the focus on a plain Tab);
    // Ctrl+Tab and Ctrl+Shift+Tab take the focus out of the canvas, where Tab does not.
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        const Qt::KeyboardModifiers modifiers = event->modifiers();
        const bool backwards = event->key() == Qt::Key_Backtab || (modifiers & Qt::ShiftModifier);
        if (modifiers & Qt::ControlModifier) {
            event->setAccepted(QWidget::focusNextPrevChild(!backwards));
            return;
        }
        if (modifiers & Qt::AltModifier) {
            navigateCells(backwards ? CellStep::Container : CellStep::FirstChild);
            event->accept(); // consumed even when there is no cell to go to: the key is not for a widget
            return;
        }
    }
    switch (event->key()) {
    case Qt::Key_Space:
        // Held Space turns the left button into a pan (draw.io: Space + drag).
        if (!event->isAutoRepeat() && !spaceHeld_) {
            spaceHeld_ = true;
            updateCursor();
        }
        event->accept();
        return;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
        event->accept(); // arrows move cells, they never scroll the canvas
        return;
    default:
        break;
    }
    QGraphicsView::keyPressEvent(event);
}

void CanvasView::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        spaceHeld_ = false;
        updateCursor();
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void CanvasView::focusOutEvent(QFocusEvent* event)
{
    interaction_->cancelGesture();
    // Key and button releases that happen elsewhere never arrive.
    spaceHeld_ = false;
    panning_ = false;
    stateOf(this).panButton = Qt::NoButton;
    updateCursor();
    QGraphicsView::focusOutEvent(event);
}

void CanvasView::requestFullUpdate()
{
    render::stats::add(render::stats::Counter::viewportFullUpdates);
    viewport()->update();
}

void CanvasView::updateSceneArea(const QRectF& sceneRect, double padPx)
{
    const QRectF view = viewportTransform().mapRect(sceneRect).adjusted(-padPx, -padPx, padPx, padPx);
    viewport()->update(view.toAlignedRect());
}

void CanvasView::requestContentUpdate(const QRectF& sceneRect)
{
    updateSceneArea(sceneRect, kContentFringePx);
}

void CanvasView::requestOverlayUpdate()
{
    const OverlayBounds now = interaction_->overlayBounds(OverlayContext{zoom_, {}, session_.diagramDark()});
    const std::optional<QRectF> before = overlayPainted_;
    overlayPainted_ = now;
    if (!now || !before) {
        requestFullUpdate(); // unknown extent: the safe answer
        return;
    }
    QRectF damage = *before;
    if (!now->isNull())
        damage = damage.isNull() ? *now : damage.united(*now);
    if (!damage.isNull())
        updateSceneArea(damage, kOverlayFringePx);
}

double CanvasView::rasterScale() const
{
    const double scale = tiles_->displayScale();
    return scale > 0 ? scale : zoom_ * viewport()->devicePixelRatioF();
}

void CanvasView::setSynchronousRasterization(bool on)
{
    tiles_->setSynchronous(on);
}

bool CanvasView::isRenderIdle() const
{
    return tiles_->isIdle() && activeLayer_->state() != ActiveLayer::State::Retiring;
}

TileViewport CanvasView::tileViewport() const
{
    const double dpr = viewport()->devicePixelRatioF();
    const QTransform transform = viewportTransform();
    TileViewport view;
    view.zoom = zoom_;
    view.dpr = dpr;
    // Scene (0, 0) sits at this whole device pixel of the viewport; the tiles
    // are in device pixels of the scaled scene, so they land on whole pixels.
    view.origin = QPoint(qRound(transform.dx() * dpr), qRound(transform.dy() * dpr));
    view.deviceSize = (viewport()->size() * dpr);
    return view;
}

void CanvasView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    tiles_->setView(tileViewport()); // re-prioritises the tiles for the new area
}

void CanvasView::onContentDamaged(const render::Damage& damage)
{
    tiles_->setSnapshot(scene_->tileSnapshot(), damage);
    if (damage.everything) {
        requestFullUpdate(); // page switch or page properties
        return;
    }
    for (const QRectF& rect : damage.rects)
        requestContentUpdate(rect);
}

void CanvasView::drawBackground(QPainter* painter, const QRectF& rect)
{
    (void)rect; // the exposed region of the paint event is more exact (a hover that moved far has two small rectangles)
    const TileViewport view = tileViewport();
    render::FrameDescription frame;
    frame.devicePixelRatio = view.dpr;
    frame.deviceSize = view.deviceSize;
    frame.clip = QRect(QPoint(0, 0), view.deviceSize);
    frame.tiles = tiles_->compose(view, exposed_);
    if (activeLayer_->isActive()) {
        // Scene units to viewport device pixels, as the tiles are placed.
        frame.active = activeLayer_->items(QTransform::fromScale(view.zoom * view.dpr, view.zoom * view.dpr)
                                           * QTransform::fromTranslate(view.origin.x(), view.origin.y()));
    }
    render::CpuCompositor(*painter).present(frame);
}

void CanvasView::paintEvent(QPaintEvent* event)
{
    QRAW_PERF_SCOPE(ViewPaint);
    render::ImageCache::instance().beginFrame(); // images used by this paint are not evicted during it
    exposed_ = event->region();
    QGraphicsView::paintEvent(event);
    exposed_ = QRegion();
}

void CanvasView::drawForeground(QPainter* painter, const QRectF& rect)
{
    QRAW_PERF_SCOPE(Foreground);
    painter->save();
    // The tiles sit at the whole device pixel `origin` (tileViewport), a scroll offset in logical
    // pixels being a fractional number of device pixels at 125 % or 150 %: the overlay is moved
    // by that rounding (under half a device pixel) so that handles and outlines are drawn where
    // the cells' own pixels are, not beside them.
    const double dpr = viewport()->devicePixelRatioF();
    const QTransform transform = viewportTransform();
    const QPoint origin = tileViewport().origin;
    painter->translate((origin.x() / dpr - transform.dx()) / transform.m11(),
                       (origin.y() / dpr - transform.dy()) / transform.m22());
    interaction_->paintOverlay(*painter, OverlayContext{zoom_, rect, session_.diagramDark()});
    painter->restore();
}

void CanvasView::dragEnterEvent(QDragEnterEvent* event)
{
    activeDrop_ = nullptr;
    for (CanvasDropHandler* handler : dropHandlers_) {
        if (handler->canDrop(*event->mimeData())) {
            activeDrop_ = handler;
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void CanvasView::dragMoveEvent(QDragMoveEvent* event)
{
    if (activeDrop_ == nullptr) {
        event->ignore();
        return;
    }
    activeDrop_->dragMove(*event->mimeData(), mapToScene(event->position().toPoint()));
    event->acceptProposedAction();
    requestFullUpdate();
}

void CanvasView::dragLeaveEvent(QDragLeaveEvent* event)
{
    if (activeDrop_ != nullptr)
        activeDrop_->dragLeave();
    activeDrop_ = nullptr;
    QGraphicsView::dragLeaveEvent(event);
}

void CanvasView::dropEvent(QDropEvent* event)
{
    CanvasDropHandler* handler = activeDrop_;
    activeDrop_ = nullptr;
    if (handler != nullptr && handler->drop(*event->mimeData(), mapToScene(event->position().toPoint()))) {
        event->acceptProposedAction();
        setFocus(Qt::OtherFocusReason);
        return;
    }
    event->ignore();
}

void CanvasView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    updateSceneRect();
    tiles_->setView(tileViewport());
}

void CanvasView::updateSceneRect()
{
    // The page area grown by one viewport (in scene units) on every side, so
    // any content can be scrolled to any viewport edge like draw.io's
    // scrollbars allow. The scene point at the viewport's top-left stays put
    // when the rectangle moves.
    const QSizeF margin(viewport()->width() / zoom_, viewport()->height() / zoom_);
    const QRectF rect = scene_->pageArea().adjusted(-margin.width(), -margin.height(), margin.width(), margin.height());
    if (rect == sceneRect())
        return;
    const QPointF topLeft = viewToScene(*this, QPointF(0, 0));
    setSceneRect(rect);
    placeScenePoint(*this, topLeft, QPointF(0, 0));
}

void CanvasView::updateCursor()
{
    Qt::CursorShape shape = interaction_->cursor();
    if (panning_)
        shape = Qt::ClosedHandCursor;
    else if (spaceHeld_)
        shape = Qt::OpenHandCursor;
    viewport()->setCursor(shape);
}

} // namespace qraw::ui
