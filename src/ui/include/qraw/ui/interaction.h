#pragma once
// The canvas interaction framework: events, gestures, handlers and the
// controller that dispatches between them. InteractionController
// and installDefaultHandlers are implemented in src/ui/interaction.cpp.
// Concrete handlers live in src/ui/select_*.cpp and src/ui/edge_*.cpp. See docs/design/ui.md §4.

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <Qt>

#include <memory>
#include <optional>
#include <vector>

class QKeyEvent;
class QPainter;

namespace qraw::ui {

class CanvasView;
class DiagramScene;
class DocumentSession;
class FreehandTool;
class LabelEditor;

// A pointer event in both coordinate systems.
struct CanvasEvent {
    QPointF scenePos;  // page units (model coordinates, absolute)
    QPointF viewPos;   // viewport pixels (logical, not device pixels)
    Qt::MouseButton button = Qt::NoButton; // the button that changed (press/release)
    Qt::MouseButtons buttons;              // buttons held
    Qt::KeyboardModifiers modifiers;
    double scale = 1;                      // view pixels per page unit (zoom)

    // Converts a tolerance in screen pixels to page units.
    [[nodiscard]] double pixels(double px) const { return px / scale; }
    [[nodiscard]] bool toggle() const { return modifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier); }
};

// Colors and sizes of the editing overlay, from draw.io's mxConstants /
// Graph.js overrides, so all handlers look alike.
namespace overlay {
// js/grapheditor/Graph.js:33903-33913 (mxConstants overrides).
inline const QColor kSelection(0x00, 0xa8, 0xff);     // VERTEX_/EDGE_SELECTION_COLOR '#00a8ff'
inline const QColor kHandleFill(0x29, 0xb6, 0xf2);    // HANDLE_FILLCOLOR '#29b6f2'
inline const QColor kHandleStroke(0x00, 0x88, 0xcf);  // HANDLE_STROKECOLOR '#0088cf'
inline const QColor kConnectHandle(0xce, 0xe7, 0xff); // CONNECT_HANDLE_FILLCOLOR '#cee7ff'
inline const QColor kGuide(0x00, 0x88, 0xcf);         // GUIDE_COLOR '#0088cf'
inline const QColor kValid(0x00, 0xa8, 0xff);         // DEFAULT_VALID_COLOR (connect target highlight)
inline const QColor kRubberband(0x99, 0xcc, 0xff);    // mxRubberband default fill, drawn at alpha 0.2
inline constexpr double kHandleSize = 9;              // px, mxConstants.HANDLE_SIZE
inline constexpr double kHitTolerance = 4;            // px, mxGraph.tolerance
// Rotation handle: 16 px right of and above the top-right corner
// (mxVertexHandler.rotationHandleVSpacing = -16, Graph.js:49969, :47388).
inline constexpr double kRotationHandleSpacing = 16;
} // namespace overlay

// What paintOverlay() receives. The painter is in scene coordinates (page
// units); sizes in pixels go through pixels(). Use cosmetic pens.
struct OverlayContext {
    double scale = 1;
    // The scene area being painted (the exposed rectangle of the view): what
    // lies outside it is clipped, so a handler may skip it. Null: unknown,
    // paint everything.
    QRectF exposed;
    [[nodiscard]] double pixels(double px) const { return px / scale; }
};

// The area a handler or gesture paints in the overlay, in scene coordinates:
//  * a rectangle that contains everything painted now (conservative: grow it
//    by the pixel sizes of handles, strokes and markers through
//    OverlayContext::pixels());
//  * a null rectangle (QRectF()): nothing is painted now;
//  * nullopt: unknown. The whole viewport is repainted when this one changes,
//    counted in render::stats viewportFullUpdates, which is how the paint
//    benchmark shows which handlers still fall back.
// CanvasView repaints the union of the bounds before and after every event, so
// the bounds must follow every change of what is painted.
using OverlayBounds = std::optional<QRectF>;

// Everything a handler may touch. Handlers keep a copy; the referenced
// objects outlive every handler (they are owned by the CanvasView or its
// creator).
struct ToolContext {
    DocumentSession* session = nullptr;
    DiagramScene* scene = nullptr;
    CanvasView* view = nullptr;
};

// One running pointer gesture (drag). Created by a handler on press; gets
// every move until release or cancel. Gestures draw a preview in the overlay
// and change the model exactly once, on release, through one
// DocumentSession::edit() (one undo entry). cancel() (Escape, focus loss,
// page switch, document reset) must leave the model untouched.
class Gesture {
public:
    virtual ~Gesture() = default;
    virtual void move(const CanvasEvent& event) = 0;
    virtual void release(const CanvasEvent& event) = 0;
    virtual void cancel() = 0;
    virtual void paint(QPainter& painter, const OverlayContext& context) const
    {
        (void)painter;
        (void)context;
    }
    // What paint() draws now (see OverlayBounds). Unknown unless overridden.
    [[nodiscard]] virtual OverlayBounds overlayBounds(const OverlayContext& context) const
    {
        (void)context;
        return std::nullopt;
    }
    [[nodiscard]] virtual Qt::CursorShape cursor() const { return Qt::ArrowCursor; }
};

// One feature of the canvas (vertex handles, edge handles, connect arrows,
// selection/move/rubber band, label editing). The controller asks handlers in
// priority order; the first that answers wins the event.
class InteractionHandler {
public:
    virtual ~InteractionHandler() = default;

    // Mouse over the canvas, no button held. Return a cursor to claim the
    // hover (lower-priority handlers then get hoverLeft()); nullopt passes.
    virtual std::optional<Qt::CursorShape> hover(const CanvasEvent& event)
    {
        (void)event;
        return std::nullopt;
    }
    virtual void hoverLeft() {}
    // A button was pressed. Return a gesture to own the drag, nullptr to
    // pass. A handler may also act immediately (a click that only changes the
    // selection) and return a gesture that does nothing on release.
    virtual std::unique_ptr<Gesture> press(const CanvasEvent& event)
    {
        (void)event;
        return nullptr;
    }
    virtual bool doubleClick(const CanvasEvent& event)
    {
        (void)event;
        return false;
    }
    // Keys that reach the canvas (window shortcuts already ran; see ui.md
    // §6). Return true to consume.
    virtual bool keyPress(QKeyEvent& event)
    {
        (void)event;
        return false;
    }
    // Handles and hover feedback, drawn above the cells.
    virtual void paintOverlay(QPainter& painter, const OverlayContext& context) const
    {
        (void)painter;
        (void)context;
    }
    // What paintOverlay() draws now (see OverlayBounds). A handler that paints
    // nothing returns QRectF(); the default is unknown, which is always right
    // and always the slowest.
    [[nodiscard]] virtual OverlayBounds overlayBounds(const OverlayContext& context) const
    {
        (void)context;
        return std::nullopt;
    }
    // The selection, the page, the projection or the document changed: drop
    // cached hover/handle state. Called by the controller, also while a
    // gesture runs (gestures own their state and keep going).
    virtual void reset() {}
};

// Handler order (lower first), matching draw.io's mxGraph event dispatch:
// the cell editor first, then the handles of selected cells
// (mxSelectionCellsHandler), then hover connect icons (HoverIcons), then the
// graph handler (click/move) and the rubber band last.
enum HandlerPriority : int {
    PriorityLabelEditor = 0,
    PriorityFreehand = 10,    // while the Freehand tool is on, a drag draws
    PriorityLinkBadge = 45,   // the link icon in the corner of a linked cell
    PriorityFoldControl = 50, // the +/- control sits on top of the cell it folds
    PriorityEdgeHandles = 100,
    PriorityTableHandles = 190, // row and column borders of a table, before its sizers
    PriorityVertexHandles = 200,
    PriorityConnect = 300,
    PrioritySelection = 400,
};

// Owned by CanvasView, which forwards its viewport events (after its own
// pan/zoom handling). Not a QObject; GUI thread only.
class InteractionController {
public:
    explicit InteractionController(ToolContext context);
    ~InteractionController();

    InteractionController(const InteractionController&) = delete;
    InteractionController& operator=(const InteractionController&) = delete;

    [[nodiscard]] const ToolContext& context() const { return context_; }

    // Handlers with equal priority keep insertion order.
    void addHandler(std::unique_ptr<InteractionHandler> handler, int priority);
    // The label editor is also a handler (PriorityLabelEditor); this keeps a
    // typed pointer for actions (F2/Enter, Edit > Edit) that start editing.
    void setLabelEditor(LabelEditor* editor) { labelEditor_ = editor; }
    [[nodiscard]] LabelEditor* labelEditor() const { return labelEditor_; }
    // The Freehand tool is a handler too (PriorityFreehand); this keeps a typed pointer for the
    // action that turns it on.
    void setFreehand(FreehandTool* tool) { freehand_ = tool; }
    [[nodiscard]] FreehandTool* freehand() const { return freehand_; }

    // Event entry points. Return true when consumed. The cursor to show
    // afterwards is cursor().
    bool mousePress(const CanvasEvent& event);
    bool mouseMove(const CanvasEvent& event);
    bool mouseRelease(const CanvasEvent& event);
    bool mouseDoubleClick(const CanvasEvent& event);
    bool keyPress(QKeyEvent& event);
    // Cancels the running gesture (Escape is routed here by keyPress when a
    // gesture runs; CanvasView calls it on focus loss).
    void cancelGesture();
    // Calls reset() on every handler (after selection or projection
    // changes). A running gesture continues; CanvasView cancels it on page
    // switches and focus loss.
    void reset();

    void paintOverlay(QPainter& painter, const OverlayContext& context) const;
    // The union of what every handler and the running gesture paint now;
    // nullopt when any of them cannot say.
    [[nodiscard]] OverlayBounds overlayBounds(const OverlayContext& context) const;
    [[nodiscard]] bool isGestureActive() const { return gesture_ != nullptr; }
    [[nodiscard]] Qt::CursorShape cursor() const { return cursor_; }

private:
    struct Entry {
        int priority;
        std::unique_ptr<InteractionHandler> handler;
    };

    ToolContext context_;
    std::vector<Entry> handlers_;
    std::unique_ptr<Gesture> gesture_;
    InteractionHandler* hovered_ = nullptr;
    LabelEditor* labelEditor_ = nullptr;
    FreehandTool* freehand_ = nullptr;
    Qt::CursorShape cursor_ = Qt::ArrowCursor;
};

// Creates and installs the standard handlers on `controller`:
// createSelectionHandlers, createEdgeHandlers
// and a LabelEditor (also set as labelEditor()). Called by
// the CanvasView constructor.
void installDefaultHandlers(InteractionController& controller);

} // namespace qraw::ui
