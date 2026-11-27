#pragma once
// Inline label editing (mxCellEditor).
// (src/ui/label_editor.cpp).

#include "qraw/ui/interaction.h"

#include <QObject>
#include <QPointF>

#include <memory>
#include <string>

namespace qraw::ui {

// Edits a cell label in place, in a text widget placed over the cell's label
// box (CellState::label, or the vertex bounds when it has none), scaled with
// the zoom and using the cell's font (fontFamily, fontSize * zoom,
// fontColor, fontStyle bits, align). The widget is transparent and the
// cell's own label is not painted meanwhile (DiagramScene::setHiddenLabel).
// A label that is rotated (cell rotation, horizontal=0) is edited rotated,
// in a QGraphicsProxyWidget in the scene (see sceneEditorAt).
//
//  * html=1 cells: rich text (QTextEdit with the HTML subset of
//    render::sanitizeHtml); bold/italic/underline via Ctrl+B/I/U while
//    editing. Others: plain text (newlines kept, mxCellEditor with
//    isHtmlLabel false).
//  * Commit (mxCellEditor.stopEditing(false), mxgraph/src/view/
//    mxCellEditor.js:982): click outside, Ctrl+Enter / Cmd+Enter, Tab,
//    focus loss, page switch. Writes the label with one
//    DocumentSession::edit(tr("Edit Label"), ops::setLabel) only when the
//    text changed; a vertex with autosize=1 is resized to the new text in
//    the same edit (label_autosize.h). Plain text is written as is; HTML is the editor's
//    sanitized inner HTML (no <html>/<body>/<p> wrappers: draw.io stores
//    the div's innerHTML).
//  * Cancel (stopEditing(true)): Escape. The model is unchanged.
//  * Enter alone inserts a newline (draw.io: Enter = newline, Ctrl+Enter or
//    a click outside = stop).
//  * While editing, the controller routes all events to this handler first
//    (PriorityLabelEditor); a press outside the editor commits and is then
//    passed on.
class LabelEditor : public QObject, public InteractionHandler {
    Q_OBJECT

public:
    explicit LabelEditor(const ToolContext& context, QObject* parent = nullptr);
    ~LabelEditor() override;

    // Starts editing the label of `id` on the current page. Fails (false)
    // when the cell is unknown, locked (cell_queries.h isCellLocked) or has
    // style editable=0. Commits a running edit first.
    bool startEditing(const std::string& id);
    // Double click on blank canvas (Graph.insertTextForEvent,
    // js/grapheditor/Graph.js:39167): opens the editor on a text vertex that
    // is not in the model yet (style "text;html=1;align=center;
    // verticalAlign=middle;resizable=0;points=[];autosize=1;strokeColor=none;
    // fillColor=none;", 40 x 20, text "Text" selected, centred at `scenePos`
    // snapped to the grid, parent = DocumentSession::insertionParent()).
    // Committing with a non-empty text inserts it, autosized, in one
    // "Insert Text" edit; Escape or an empty text inserts nothing.
    // editingCell() is empty meanwhile.
    bool insertTextAt(QPointF scenePos);
    // Commits (cancel = false) or discards the running edit. No-op when idle.
    void stopEditing(bool cancel);
    [[nodiscard]] bool isEditing() const;
    [[nodiscard]] const std::string& editingCell() const { return cellId_; }
    // True when a rotated label is being edited in the scene and `scenePos`
    // is on that editor: the canvas view hands such mouse events to the
    // scene (QGraphicsView) instead of the interaction handlers.
    [[nodiscard]] bool sceneEditorAt(QPointF scenePos) const;

    // InteractionHandler: double click on a cell starts editing it (edges
    // too: editing an edge label); on blank canvas calls insertTextAt.
    // The cell is the hit cell mapped with ancestorInScope(enteredGroup)
    // unless that ancestor is already selected (then the hit cell itself).
    // Declines (returns false) when that cell has children and its style has
    // the bare name `group`: the selection handler enters the group instead.
    // While editing, presses outside commit and return nullptr.
    std::unique_ptr<Gesture> press(const CanvasEvent& event) override;
    bool doubleClick(const CanvasEvent& event) override;
    bool keyPress(QKeyEvent& event) override;
    // The editor is a widget (or a scene item): nothing is painted in the overlay.
    [[nodiscard]] OverlayBounds overlayBounds(const OverlayContext&) const override { return QRectF(); }
    void reset() override;

signals:
    void editingStarted(const QString& cellId);
    void editingFinished(const QString& cellId, bool committed);

private:
    struct Impl;
    ToolContext context_;
    std::string cellId_;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::ui
