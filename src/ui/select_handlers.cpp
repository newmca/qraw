// the PrioritySelection handler (click selection, group
// drill-down, rubber band, move, keyboard nudge, enter/exit group) and the
// factory that also creates the vertex handles handler.
// Upstream: mxGraphHandler, mxRubberband, EditorUi.createKeyHandler,
// Graph.selectCellForEvent (see docs/design/ui.md section 4).

#include "qraw/ui/tool_factories.h"

#include "fold_controls.h"
#include "link_badge.h"
#include "select_gestures.h"
#include "select_support.h"
#include "select_table_handles.h"
#include "select_vertex_handles.h"
#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_tables.h"
#include "qraw/ui/canvas_view.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QTimer>

#include <algorithm>

namespace qraw::ui {

namespace {

// Consecutive arrow-key nudges are one undo step until this long a pause
// (or any other key or a mouse press) ends the sequence.
constexpr int kNudgeMergeTimeoutMs = 500;

class SelectionHandler : public InteractionHandler {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::SelectionHandler)

public:
    SelectionHandler(ToolContext context, std::shared_ptr<SelectionUiState> state)
        : ctx_(context), state_(std::move(state))
    {
        nudgeTimer_.setSingleShot(true);
        nudgeTimer_.setInterval(kNudgeMergeTimeoutMs);
        QObject::connect(&nudgeTimer_, &QTimer::timeout, [this] { endNudge(); });
    }

    std::optional<Qt::CursorShape> hover(const CanvasEvent& event) override;
    std::unique_ptr<Gesture> press(const CanvasEvent& event) override;
    bool doubleClick(const CanvasEvent& event) override;
    bool keyPress(QKeyEvent& event) override;
    [[nodiscard]] OverlayBounds overlayBounds(const OverlayContext&) const override { return QRectF(); }

private:
    [[nodiscard]] bool ready() const
    {
        return ctx_.session != nullptr && ctx_.scene != nullptr && ctx_.session->currentPage() != nullptr;
    }
    [[nodiscard]] std::string hitCell(const CanvasEvent& event) const;
    void applyPressSelection(const core::Page& page, const std::string& hit, const CanvasEvent& event,
                             ClickAction& click) const;
    bool nudge(int key, bool largeStep);
    void endNudge();

    ToolContext ctx_;
    std::shared_ptr<SelectionUiState> state_;
    QTimer nudgeTimer_;
    bool nudging_ = false;
};

std::string SelectionHandler::hitCell(const CanvasEvent& event) const
{
    return ctx_.scene->cellAt(event.scenePos, event.pixels(overlay::kHitTolerance));
}

// Graph.selectCellForEvent (simplified; a table cell is selected on the second click, and
// Shift+click selects the range of cells): the click selects the
// topmost ancestor below the entered group; a click on a cell that is
// already selected keeps the selection and decides on release, so a drag of
// a selected group by one of its children moves the group.
void SelectionHandler::applyPressSelection(const core::Page& page, const std::string& hit,
                                           const CanvasEvent& event, ClickAction& click) const
{
    SelectionModel& selection = ctx_.session->selection();
    // Shift+click on a cell of the table whose cell is selected selects the range of cells between them.
    if ((event.modifiers & Qt::ShiftModifier) && !(event.modifiers & (Qt::ControlModifier | Qt::MetaModifier)) &&
        !selection.isEmpty()) {
        const std::vector<std::string> range = commands::ops::tableCellRange(page, selection.first(), hit);
        if (!range.empty()) {
            selection.set(range);
            return;
        }
    }
    std::string cell = selection.contains(hit) ? hit : ancestorInScope(page, hit, ctx_.session->enteredGroup());
    // With a row or cell of a table selected, a click on another part of the
    // same table switches to that part instead of climbing back to the table.
    // Ctrl+click adds the part the same way, which is how the cells of a range are picked one by one.
    if ((event.toggle() || selection.size() == 1) && !selection.isEmpty() && !selection.contains(hit)) {
        const std::string table = commands::ops::tableOf(page, selection.first());
        if (!table.empty() && table != selection.first() && commands::ops::tableOf(page, hit) == table &&
            hit != table)
            cell = childToward(page, table, hit);
    }

    if (event.toggle()) {
        if (selection.contains(cell))
            click = {ClickAction::Kind::Deselect, cell};
        else
            selection.add({cell});
    } else if (!selection.contains(cell)) {
        selection.set({cell});
    } else if (selection.size() > 1) {
        click = {ClickAction::Kind::SelectOnly, cell};
    } else if (hit != cell) {
        // Second click into a selected group: drill down one level.
        const std::string child = childToward(page, cell, hit);
        if (!child.empty())
            click = {ClickAction::Kind::SelectOnly, child};
    }
}

std::unique_ptr<Gesture> SelectionHandler::press(const CanvasEvent& event)
{
    if (event.button != Qt::LeftButton || !ready())
        return nullptr;
    endNudge();
    const core::Page& page = *ctx_.session->currentPage();
    SelectionModel& selection = ctx_.session->selection();

    const std::string hit = hitCell(event);
    const bool inScope = !hit.empty() && (selection.contains(hit) ||
                                          !ancestorInScope(page, hit, ctx_.session->enteredGroup()).empty());
    if (!inScope) {
        if (!event.toggle())
            selection.clear();
        return std::make_unique<RubberbandGesture>(ctx_, event);
    }

    ClickAction click;
    applyPressSelection(page, hit, event, click);
    return std::make_unique<MoveGesture>(ctx_, state_, movableSelection(*ctx_.scene, page, selection.cells()),
                                         event, std::move(click));
}

// The move cursor over a selected, movable cell (mxGraphHandler.updateCursor).
std::optional<Qt::CursorShape> SelectionHandler::hover(const CanvasEvent& event)
{
    if (!ready() || ctx_.session->selection().isEmpty())
        return std::nullopt;
    const core::Page& page = *ctx_.session->currentPage();
    const SelectionModel& selection = ctx_.session->selection();
    const std::string hit = hitCell(event);
    if (hit.empty())
        return std::nullopt;
    for (const std::string& id : {hit, ancestorInScope(page, hit, ctx_.session->enteredGroup())}) {
        if (id.empty() || !selection.contains(id))
            continue;
        const render::CellState* state = ctx_.scene->state(id);
        if (state != nullptr && isMovable(page, *state))
            return Qt::SizeAllCursor;
    }
    return std::nullopt;
}

// Graph.dblClick / enterGroup (Actions.js:576): a double click on a group
// enters it; on blank canvas inside an entered group it exits again. The
// LabelEditor runs before and declines groups.
bool SelectionHandler::doubleClick(const CanvasEvent& event)
{
    if (event.button != Qt::LeftButton || !ready())
        return false;
    const core::Page& page = *ctx_.session->currentPage();
    const std::string scope = ctx_.session->enteredGroup();
    const std::string hit = hitCell(event);
    const std::string cell = hit.empty() ? std::string() : ancestorInScope(page, hit, scope);

    if (cell.empty()) {
        if (scope.empty())
            return false;
        ctx_.session->exitGroup();
        return true;
    }
    const core::Cell* group = page.findCell(cell);
    if (group == nullptr || !group->style().hasName("group") || !hasChildren(page, cell))
        return false;
    ctx_.session->enterGroup(cell);
    if (hit != cell) {
        const std::string child = childToward(page, cell, hit);
        if (!child.empty())
            ctx_.session->selection().set({child});
    }
    return true;
}

bool SelectionHandler::keyPress(QKeyEvent& event)
{
    if (!ready())
        return false;
    const Qt::KeyboardModifiers modifiers = event.modifiers() & ~Qt::KeypadModifier;
    const int key = event.key();
    if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down) {
        if (modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
            return false;
        return nudge(key, modifiers & Qt::ShiftModifier);
    }
    // Pressing Shift for a larger step must not end the sequence it joins.
    if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta)
        return false;
    endNudge();

    SelectionModel& selection = ctx_.session->selection();
    switch (key) {
    case Qt::Key_Escape:
        if (!selection.isEmpty()) {
            selection.clear();
            return true;
        }
        if (!ctx_.session->enteredGroup().empty()) {
            ctx_.session->exitGroup();
            return true;
        }
        return false;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return selection.size() == 1 && ctx_.view != nullptr && ctx_.view->editLabel(selection.first());
    default:
        return false;
    }
}

// EditorUi.createKeyHandler nudge (EditorUi.js:10925): 1 page unit, a grid
// step with Shift; the keys of one sequence share a merge key.
bool SelectionHandler::nudge(int key, bool largeStep)
{
    const core::Page& page = *ctx_.session->currentPage();
    const SelectionModel& selection = ctx_.session->selection();
    if (selection.isEmpty())
        return false;
    const std::vector<std::string> moving = movableSelection(*ctx_.scene, page, selection.cells());
    if (moving.empty())
        return true; // consumed: locked cells do not scroll the view either

    const double step = largeStep ? page.gridSize() : 1;
    const double dx = key == Qt::Key_Left ? -step : key == Qt::Key_Right ? step : 0;
    const double dy = key == Qt::Key_Up ? -step : key == Qt::Key_Down ? step : 0;

    std::string mergeKey = "nudge:";
    for (const std::string& id : moving)
        mergeKey += id + ',';
    const std::size_t pageIndex = ctx_.session->currentPageIndex();
    nudging_ = true;
    ctx_.session->edit(
        tr("Move"),
        [&](commands::Transaction& tx) { return commands::ops::moveCells(tx, pageIndex, moving, dx, dy); },
        mergeKey);
    nudgeTimer_.start();
    return true;
}

void SelectionHandler::endNudge()
{
    nudgeTimer_.stop();
    if (nudging_ && ctx_.session != nullptr)
        ctx_.session->endMerge();
    nudging_ = false;
}

} // namespace

HandlerList createSelectionHandlers(const ToolContext& context)
{
    auto state = std::make_shared<SelectionUiState>();
    HandlerList handlers;
    handlers.emplace_back(std::make_unique<LinkBadgeHandler>(context), PriorityLinkBadge);
    handlers.emplace_back(std::make_unique<FoldControlHandler>(context), PriorityFoldControl);
    handlers.emplace_back(std::make_unique<SelectionHandler>(context, state), PrioritySelection);
    handlers.emplace_back(std::make_unique<TableHandlesHandler>(context, state), PriorityTableHandles);
    handlers.emplace_back(std::make_unique<VertexHandlesHandler>(context, state), PriorityVertexHandles);
    return handlers;
}

} // namespace qraw::ui
