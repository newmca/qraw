#pragma once
// The editor canvas widget (docs/design/ui.md §3-4): event forwarding, zoom
// and the layered paint.

#include "qraw/render/tile_cache.h"
#include "qraw/ui/tile_scheduler.h"
#include "qraw/ui/interaction.h"

#include <QGraphicsView>
#include <QPointF>
#include <QRegion>

#include <memory>
#include <optional>
#include <string>
#include <vector>

class QMimeData;

namespace qraw::ui {

class ActiveLayer;
class DiagramScene;
class DocumentSession;

// Accepts drops onto the canvas (the palette's cell drag, files). Not owned
// by the view. The first handler whose canDrop() is true gets the drop.
class CanvasDropHandler {
public:
    virtual ~CanvasDropHandler() = default;
    [[nodiscard]] virtual bool canDrop(const QMimeData& mime) const = 0;
    // Drag feedback while hovering (e.g. a preview outline); default none.
    virtual void dragMove(const QMimeData& mime, QPointF scenePos)
    {
        (void)mime;
        (void)scenePos;
    }
    virtual void dragLeave() {}
    // Performs the drop at `scenePos` (page units). Returns true when
    // something was inserted.
    virtual bool drop(const QMimeData& mime, QPointF scenePos) = 0;
};

// QGraphicsView over a DiagramScene it owns. Responsibilities:
//  * zoom: Ctrl+wheel (around the cursor), zoomIn/zoomOut on draw.io's zoom
//    steps (Graph.getZoomSteps/getZoomStep, Graph.js:21672/21763; the
//    steps for zoomFactor 1.2), range [kMinZoom, kMaxZoom];
//  * pan: wheel scrolls (Shift+wheel horizontally), middle-button drag,
//    Space+left drag, right-button drag on blank canvas (draw.io
//    mxPanningHandler with useLeftButtonForPanning off);
//  * forwards all other mouse/key events, converted to CanvasEvent, to its
//    InteractionController and paints the overlay in drawForeground();
//  * page/content fitting, the scene rect (page area plus one viewport of
//    margin on every side, as draw.io's scrollbars allow), high-DPI
//    rendering (QGraphicsView handles devicePixelRatio; the overlay uses
//    cosmetic pens);
//  * drag-and-drop onto the canvas through CanvasDropHandlers.
class CanvasView : public QGraphicsView {
    Q_OBJECT

public:
    static constexpr double kMinZoom = 0.01;
    static constexpr double kMaxZoom = 160; // Graph.getZoomSteps tops out at 16000 %

    explicit CanvasView(DocumentSession& session, QWidget* parent = nullptr);
    ~CanvasView() override;

    [[nodiscard]] DocumentSession& session() const { return session_; }
    [[nodiscard]] DiagramScene& diagramScene() const { return *scene_; }
    [[nodiscard]] InteractionController& interaction() const { return *interaction_; }
    // The cells under the user's hands, drawn live above the tiles during a
    // gesture (active_layer.h).
    [[nodiscard]] ActiveLayer& activeLayer() const { return *activeLayer_; }

    // --- zoom ---------------------------------------------------------------
    [[nodiscard]] double zoom() const { return zoom_; }
    // Sets the zoom, keeping the scene point under `viewAnchor` (viewport
    // pixels; default the viewport centre) fixed. Clamped. Emits zoomChanged.
    void setZoom(double zoom, std::optional<QPointF> viewAnchor = std::nullopt);
    void zoomIn();   // next draw.io zoom step
    void zoomOut();  // previous step
    void resetZoom(); // 100 %, Actions.js 'resetView' (Home) also scrolls to the page origin
    void fitPage();   // Ctrl+J, Graph fitPage: whole first page tile in view
    void fitWindow(); // Ctrl+Shift+H, Graph.fitWindow: all content (or the page when empty)
    void fitSelection(); // selected cells' bounds; fitWindow when nothing is selected
    void centerOn(const QRectF& sceneRect);

    // The scene point at the viewport centre, snapped to the grid when the
    // page grid is on (Graph.getCenterInsertPoint, Graph.js:39240). Palette
    // click-to-insert and paste use it.
    [[nodiscard]] QPointF centerInsertPoint() const;
    // The last scene position of the mouse over the canvas, if any
    // (Graph.getInsertPoint, Graph.js:39202).
    [[nodiscard]] std::optional<QPointF> lastMouseScenePos() const { return lastMouseScenePos_; }
    // Where the Insert menu puts new cells (Editor.js:9971 overrides getInsertPoint
    // for the mouse): the scene point under the pointer while it is over the
    // canvas, else centerInsertPoint(). The pointer is over a menu when a menu
    // command runs, so menu commands land in the viewport centre; the point is
    // for the commands that run with the pointer over the canvas. Not snapped:
    // insertion snaps the new cells' corner (actions::insertBundle).
    [[nodiscard]] QPointF insertPoint() const;

    // --- view options (not part of the document) ----------------------------
    [[nodiscard]] bool guidesEnabled() const { return guidesEnabled_; }
    void setGuidesEnabled(bool on);
    // The Freehand tool (Insert > Freehand): a drag draws a stroke instead of selecting.
    [[nodiscard]] bool freehandActive() const;
    void setFreehandActive(bool on);

    // --- repaint ---------------------------------------------------------------
    // The canvas paints in layers (ARCHITECTURE.md, "Canvas rendering"): tiles
    // of the page content, rasterised once and blitted; the active layer
    // (the cells of a running gesture); and the overlay (selection, handles, hover, guides,
    // rubber band), which never invalidates a tile. Every repaint request of
    // the view and of its handlers goes through the three calls below (never
    // viewport()->update()), so that the paint benchmark counts the full ones.
    //
    // The overlay changed: repaints the union of what the interaction layer
    // painted at the last request and what it paints now (InteractionController::
    // overlayBounds, +2 px); the whole viewport, counted in viewportFullUpdates,
    // when a handler cannot say.
    void requestOverlayUpdate();
    // `sceneRect` (scene coordinates) shows different content: repaints it from
    // the tiles (the tiles themselves are invalidated by the scene's damage).
    void requestContentUpdate(const QRectF& sceneRect);
    // The whole viewport: only for changes that are genuinely global (zoom,
    // page switch, page properties, resize, theme, drag feedback of unknown
    // extent). Counted in render::stats viewportFullUpdates.
    void requestFullUpdate();
    // The tiles of the content layer (tests and the benchmark).
    [[nodiscard]] render::TileCache& tileCache() { return tiles_->cache(); }
    [[nodiscard]] TileScheduler& tileScheduler() { return *tiles_; }
    // The raster scale of the tiles on screen: the zoom bucket x device pixel
    // ratio. It equals zoom x ratio at every zoom step (the buckets are the
    // steps); between steps it is that of the bucket above the zoom.
    [[nodiscard]] double rasterScale() const;
    // Tiles are rasterised by worker threads: the GUI thread composes
    // what is finished, shows older or other-zoom tiles for the rest, and
    // never waits. Synchronous mode rasterises on the GUI thread whatever a
    // paint needs, so that the pixels are deterministic: the --screenshot
    // harness and tests that grab the viewport use it.
    void setSynchronousRasterization(bool on);
    [[nodiscard]] bool synchronousRasterization() const { return tiles_->synchronous(); }
    // True when no asynchronous work that changes the canvas pixels is
    // pending (nothing queued or running for the visible tiles, no settle
    // timer, no substitute on screen), so that the picture is final. The paint
    // benchmark waits for it to report the time to settled quality. The tile
    // part is always true while the canvas paints synchronously. A released
    // gesture's cells that the active layer still draws (until the tiles of
    // their new place are ready) count as pending too.
    [[nodiscard]] bool isRenderIdle() const;

    // --- tooltips --------------------------------------------------------------
    // The rich text of the tooltip of the cell under `scenePos` (cell_tooltip.h): its
    // tooltip property, else its properties, with the link of a linked cell first.
    // Empty over blank canvas and while a gesture or a label edit is running.
    [[nodiscard]] QString tooltipAt(QPointF scenePos) const;

    // --- label editing (Edit > Edit, F2/Enter) -------------------------------
    // Starts inline editing of `id` through the controller's LabelEditor.
    // Returns false when there is no editor or the cell is not editable.
    bool editLabel(const std::string& id);

    // --- keyboard navigation between cells (Tab, cell_navigation.h) ----------
    // The steps of Tab (Next), Shift+Tab (Previous), Alt+Tab (FirstChild) and Alt+Shift+Tab (Container).
    enum class CellStep { Next, Previous, FirstChild, Container };
    // Selects the cell `step` leads to from the first selected cell (from nothing: the first or the last
    // cell) and scrolls it into view. Next and Previous wrap around. Returns false, and changes nothing,
    // when there is no such cell.
    bool navigateCells(CellStep step);

    // --- drag and drop --------------------------------------------------------
    void addDropHandler(CanvasDropHandler* handler);
    void removeDropHandler(CanvasDropHandler* handler);

signals:
    // The Freehand tool turned on or off (also by Escape on the canvas).
    void freehandActiveChanged(bool active);
    void zoomChanged(double zoom);
    // Mouse position in page units, for the status bar.
    void mouseScenePosChanged(QPointF scenePos);
    // Right button released without dragging (draw.io popup menu trigger).
    // The selection was already updated as by a left click on that point
    // when the point hit an unselected cell. `globalPos` is for QMenu::popup.
    void contextMenuRequested(QPoint globalPos, QPointF scenePos);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    // The cell tooltip (QEvent::ToolTip on the viewport).
    bool viewportEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    // Tab and Shift+Tab move the selection from cell to cell instead of the focus (keyboard navigation);
    // Ctrl+Tab and Ctrl+Shift+Tab leave the canvas (keyPressEvent).
    bool focusNextPrevChild(bool next) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    // The content layer: presents the tiles of the scene's snapshot at integer
    // device positions through the CpuCompositor.
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    // The view as the tile scheduler needs it.
    [[nodiscard]] TileViewport tileViewport() const;
    [[nodiscard]] CanvasEvent toCanvasEvent(const QMouseEvent& event) const;
    // A rotated label is edited in a QGraphicsProxyWidget in the scene: mouse
    // events on it (or while it holds the mouse) go to QGraphicsView.
    [[nodiscard]] bool sceneEditorWantsMouse(const QMouseEvent& event) const;
    void updateSceneRect();
    void updateCursor();
    void onContentDamaged(const render::Damage& damage);
    // Repaints a rectangle of scene coordinates (view transform, `padPx` logical pixels of slack).
    void updateSceneArea(const QRectF& sceneRect, double padPx);

    DocumentSession& session_;
    std::unique_ptr<DiagramScene> scene_;
    ActiveLayer* activeLayer_ = nullptr; // child object, created after the tile scheduler
    std::unique_ptr<InteractionController> interaction_;
    std::vector<CanvasDropHandler*> dropHandlers_;
    CanvasDropHandler* activeDrop_ = nullptr;
    double zoom_ = 1;
    bool guidesEnabled_ = true;
    bool spaceHeld_ = false;
    bool panning_ = false;
    QPoint panStart_;
    std::optional<QPointF> lastMouseScenePos_;
    std::optional<QPointF> lastMouseViewPos_; // viewport pixels; empty after the pointer left
    TileScheduler* tiles_ = nullptr; // child object: tile cache, zoom buckets, worker pool
    QRegion exposed_;           // the region of the paint event in progress (logical pixels)
    // What the overlay occupied when it was last painted (or scheduled for
    // painting); nullopt when that was unknown. A null rectangle: nothing.
    std::optional<QRectF> overlayPainted_ = QRectF();
};

} // namespace qraw::ui
