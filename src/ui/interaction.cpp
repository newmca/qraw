#include "qraw/ui/interaction.h"

#include "qraw/ui/freehand_tool.h"
#include "qraw/ui/label_editor.h"
#include "qraw/ui/tool_factories.h"

#include <QKeyEvent>

#include <algorithm>

namespace qraw::ui {

InteractionController::InteractionController(ToolContext context) : context_(context) {}

InteractionController::~InteractionController()
{
    // The gesture may refer to handler state; drop it first.
    gesture_.reset();
}

void InteractionController::addHandler(std::unique_ptr<InteractionHandler> handler, int priority)
{
    if (!handler)
        return;
    const auto at = std::upper_bound(handlers_.begin(), handlers_.end(), priority,
                                     [](int p, const Entry& entry) { return p < entry.priority; });
    handlers_.insert(at, Entry{priority, std::move(handler)});
}

bool InteractionController::mousePress(const CanvasEvent& event)
{
    if (gesture_)
        return true; // a second button during a drag is ignored
    for (const Entry& entry : handlers_) {
        if (auto gesture = entry.handler->press(event)) {
            gesture_ = std::move(gesture);
            cursor_ = gesture_->cursor();
            return true;
        }
    }
    return false;
}

bool InteractionController::mouseMove(const CanvasEvent& event)
{
    if (gesture_) {
        gesture_->move(event);
        cursor_ = gesture_->cursor();
        return true;
    }
    InteractionHandler* claimer = nullptr;
    std::optional<Qt::CursorShape> cursor;
    for (const Entry& entry : handlers_) {
        cursor = entry.handler->hover(event);
        if (cursor) {
            claimer = entry.handler.get();
            break;
        }
    }
    if (hovered_ != nullptr && hovered_ != claimer)
        hovered_->hoverLeft();
    hovered_ = claimer;
    cursor_ = cursor.value_or(Qt::ArrowCursor);
    return claimer != nullptr;
}

bool InteractionController::mouseRelease(const CanvasEvent& event)
{
    if (!gesture_)
        return false;
    // Release may trigger edits whose notifications reset handlers; the
    // gesture leaves the controller before it runs.
    std::unique_ptr<Gesture> gesture = std::move(gesture_);
    gesture->release(event);
    cursor_ = Qt::ArrowCursor;
    return true;
}

bool InteractionController::mouseDoubleClick(const CanvasEvent& event)
{
    if (gesture_)
        return true;
    for (const Entry& entry : handlers_) {
        if (entry.handler->doubleClick(event))
            return true;
    }
    return false;
}

bool InteractionController::keyPress(QKeyEvent& event)
{
    if (gesture_) {
        if (event.key() == Qt::Key_Escape)
            cancelGesture();
        return true; // keys do not reach handlers during a drag
    }
    for (const Entry& entry : handlers_) {
        if (entry.handler->keyPress(event))
            return true;
    }
    return false;
}

void InteractionController::cancelGesture()
{
    if (!gesture_)
        return;
    std::unique_ptr<Gesture> gesture = std::move(gesture_);
    gesture->cancel();
    cursor_ = Qt::ArrowCursor;
}

void InteractionController::reset()
{
    if (hovered_ != nullptr)
        hovered_->hoverLeft();
    hovered_ = nullptr;
    for (const Entry& entry : handlers_)
        entry.handler->reset();
}

void InteractionController::paintOverlay(QPainter& painter, const OverlayContext& context) const
{
    // Lowest priority first, so the handles of higher-priority handlers end
    // up on top; the running gesture's preview last.
    for (auto it = handlers_.rbegin(); it != handlers_.rend(); ++it)
        it->handler->paintOverlay(painter, context);
    if (gesture_)
        gesture_->paint(painter, context);
}

OverlayBounds InteractionController::overlayBounds(const OverlayContext& context) const
{
    QRectF united;
    const auto add = [&united](const OverlayBounds& bounds) {
        if (!bounds)
            return false;
        if (!bounds->isNull())
            united = united.isNull() ? *bounds : united.united(*bounds);
        return true;
    };
    for (const Entry& entry : handlers_) {
        if (!add(entry.handler->overlayBounds(context)))
            return std::nullopt;
    }
    if (gesture_ && !add(gesture_->overlayBounds(context)))
        return std::nullopt;
    return united;
}

void installDefaultHandlers(InteractionController& controller)
{
    const ToolContext& context = controller.context();
    for (auto& [handler, priority] : createSelectionHandlers(context))
        controller.addHandler(std::move(handler), priority);
    for (auto& [handler, priority] : createEdgeHandlers(context))
        controller.addHandler(std::move(handler), priority);
    auto editor = std::make_unique<LabelEditor>(context);
    controller.setLabelEditor(editor.get());
    controller.addHandler(std::move(editor), PriorityLabelEditor);
    auto freehand = std::make_unique<FreehandTool>(context);
    controller.setFreehand(freehand.get());
    controller.addHandler(std::move(freehand), PriorityFreehand);
}

} // namespace qraw::ui
