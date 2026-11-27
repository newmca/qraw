#include "fold_controls.h"

#include "select_support.h"

#include "qraw/commands/ops_structure.h"
#include "qraw/ui/canvas_view.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"

#include <QLinearGradient>
#include <QPainter>

#include <algorithm>
#include <numeric>

namespace qraw::ui {

namespace {

constexpr double kControlSize = 9;       // Graph.updateFoldingImages(9)
constexpr double kControlInset = 9;      // getControlBounds: centre at max(w / 2, 9)
// The control is scaled with the zoom; smaller than this on the screen it is
// not drawn (with every cell of a big page in view that would mean looking at
// each of them for nothing).
constexpr double kMinControlPixels = 2;

// Graph.createFoldingImage colours.
const QColor kControlStroke(0x8a, 0x94, 0xa5);
const QColor kControlTop(0xf0, 0xf0, 0xf0);
const QColor kControlBottom(0xaf, 0xb0, 0xb6);

bool isContainerState(const render::CellState& state)
{
    if (state.style.shape() == "swimlane")
        return state.style.string("container", "1") != "0";
    return state.style.string("container", "0") == "1";
}

} // namespace

bool isFoldable(const core::Page& page, const render::CellState& state)
{
    if (state.kind != render::CellState::Kind::Vertex || state.cell == nullptr)
        return false;
    const render::ResolvedStyle& style = state.style;
    if (style.string("transparentBounds") == "1" || style.string("resizable", "1") == "0")
        return false;
    const std::string_view collapsible = style.string("collapsible");
    const bool allowed = isContainerState(state) ? collapsible != "0" : collapsible == "1";
    return allowed && !isCellLocked(page, state.id);
}

QRectF foldControlRect(const render::CellState& state)
{
    const double cx = state.bounds.x + std::max(kControlSize / 2, kControlInset);
    const double cy = state.bounds.y + std::max(kControlSize / 2, kControlInset);
    return {cx - kControlSize / 2, cy - kControlSize / 2, kControlSize, kControlSize};
}

bool hitsFoldControl(const render::CellState& state, QPointF scenePos)
{
    const QPointF centre = toQRect(state.bounds).center();
    const double rotation = state.style.rotation();
    if (rotation != 0)
        scenePos = rotateAround(scenePos, centre, -rotation);
    return foldControlRect(state).contains(scenePos);
}

const render::CellState* FoldControlHandler::controlAt(QPointF scenePos) const
{
    if (ctx_.scene == nullptr || ctx_.session == nullptr || ctx_.session->currentPage() == nullptr)
        return nullptr;
    const core::Page& page = *ctx_.session->currentPage();
    const auto& states = ctx_.scene->pageView().states();
    // The control lies inside its state's bounds: only the states under the pointer count.
    const std::vector<std::uint32_t> candidates = ctx_.scene->statesIn(QRectF(scenePos, QSizeF(0, 0)));
    for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
        const render::CellState& state = states[*it];
        if (hitsFoldControl(state, scenePos) && isFoldable(page, state))
            return &state;
    }
    return nullptr;
}

std::optional<Qt::CursorShape> FoldControlHandler::hover(const CanvasEvent& event)
{
    return controlAt(event.scenePos) != nullptr ? std::optional<Qt::CursorShape>(Qt::PointingHandCursor) : std::nullopt;
}

// A click on the control: fold on release when the pointer is still on it.
class FoldClickGesture : public Gesture {
public:
    FoldClickGesture(FoldControlHandler& owner, std::string id) : owner_(owner), id_(std::move(id)) {}
    void move(const CanvasEvent& event) override { (void)event; }
    void release(const CanvasEvent& event) override
    {
        const render::CellState* state = owner_.ctx_.scene->state(id_);
        if (state != nullptr && hitsFoldControl(*state, event.scenePos))
            owner_.toggle(id_);
    }
    void cancel() override {}
    [[nodiscard]] Qt::CursorShape cursor() const override { return Qt::PointingHandCursor; }

private:
    FoldControlHandler& owner_;
    std::string id_;
};

std::unique_ptr<Gesture> FoldControlHandler::press(const CanvasEvent& event)
{
    if (event.button != Qt::LeftButton)
        return nullptr;
    const render::CellState* state = controlAt(event.scenePos);
    return state != nullptr ? std::make_unique<FoldClickGesture>(*this, state->id) : nullptr;
}

void FoldControlHandler::toggle(const std::string& id)
{
    DocumentSession& session = *ctx_.session;
    const core::Cell* cell = session.currentPage() != nullptr ? session.currentPage()->findCell(id) : nullptr;
    if (cell == nullptr)
        return;
    const bool collapse = !cell->isCollapsed();
    const std::size_t page = session.currentPageIndex();
    // The selection stays as it is (the control is not a selection click):
    // an empty Result.cells leaves it alone.
    session.edit(collapse ? tr("Collapse") : tr("Expand"), [&](commands::Transaction& tx) {
        commands::Result result = commands::ops::foldCells(tx, page, {id}, collapse);
        if (result)
            result.cells.clear();
        return result;
    });
}

// mxCellRenderer.createControl with Graph.createFoldingImage.
void FoldControlHandler::paintOverlay(QPainter& painter, const OverlayContext& context) const
{
    if (ctx_.scene == nullptr || ctx_.session == nullptr || ctx_.session->currentPage() == nullptr
        || kControlSize * context.scale < kMinControlPixels)
        return;
    const core::Page& page = *ctx_.session->currentPage();
    // Only the controls inside the exposed area are painted (the control lies
    // inside its state's bounds, plus a pixel of line).
    const auto& states = ctx_.scene->pageView().states();
    std::vector<std::uint32_t> candidates;
    if (context.exposed.isNull()) {
        candidates.resize(states.size());
        std::iota(candidates.begin(), candidates.end(), std::uint32_t(0));
    } else {
        const double pad = context.pixels(2);
        candidates = ctx_.scene->statesIn(context.exposed.adjusted(-pad, -pad, pad, pad));
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const std::uint32_t index : candidates) {
        const render::CellState& state = states[index];
        if (!isFoldable(page, state))
            continue;
        const QRectF rect = foldControlRect(state);
        painter.save();
        painter.translate(toQRect(state.bounds).center());
        painter.rotate(state.style.rotation());
        painter.translate(-toQRect(state.bounds).center());

        // The 9 x 9 image is a stroked box (2 px line, half of it outside the
        // image box, so the visible border is 1 px) with the sign inside.
        QLinearGradient gradient(rect.center().x(), rect.top(), rect.center().x(), rect.bottom());
        gradient.setColorAt(0.3, kControlTop);
        gradient.setColorAt(1, kControlBottom);
        QPen boxPen(kControlStroke, 1);
        boxPen.setCosmetic(true);
        painter.setPen(boxPen);
        painter.setBrush(gradient);
        painter.drawRect(rect);

        QPen signPen(Qt::black, 1);
        signPen.setCosmetic(true);
        painter.setPen(signPen);
        const QPointF c = rect.center();
        painter.drawLine(QPointF(rect.left() + 2, c.y()), QPointF(rect.right() - 2, c.y()));
        if (state.collapsed)
            painter.drawLine(QPointF(c.x(), rect.top() + 2), QPointF(c.x(), rect.bottom() - 2));
        painter.restore();
    }
    painter.restore();
}

} // namespace qraw::ui
