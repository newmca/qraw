#pragma once
// Synthetic mouse input on a CanvasView in scene coordinates, shared by the UI
// tests (test_support.h includes it) and qraw-bench (tests/bench). Needs no
// Qt Test.

#include "qraw/ui/canvas_view.h"

#include <QApplication>
#include <QMouseEvent>
#include <QWidget>

#include <cmath>

namespace qraw::ui::test {

// --- synthetic mouse input on a CanvasView (scene coordinates) --------------
inline QPoint viewPoint(const CanvasView& view, QPointF scenePos)
{
    return view.mapFromScene(scenePos);
}

// The pixel of a grabbed viewport (`QWidget::grab()` is as large as the widget times its device
// pixel ratio) that shows a scene position. Rectangles and search windows in device pixels are
// the caller's to scale by `view.viewport()->devicePixelRatioF()`.
inline QPoint devicePixel(const CanvasView& view, QPointF scenePos)
{
    const double dpr = view.viewport()->devicePixelRatioF();
    const QPoint logical = viewPoint(view, scenePos);
    return QPoint(int(std::lround(logical.x() * dpr)), int(std::lround(logical.y() * dpr)));
}

inline void sendMouse(CanvasView& view, QEvent::Type type, QPointF scenePos, Qt::MouseButton button,
                      Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    const QPointF local = viewPoint(view, scenePos);
    const QPointF global = view.viewport()->mapToGlobal(local);
    QMouseEvent event(type, local, global, button, buttons, modifiers);
    QApplication::sendEvent(view.viewport(), &event);
}

inline void press(CanvasView& view, QPointF p, Qt::KeyboardModifiers m = Qt::NoModifier,
                  Qt::MouseButton b = Qt::LeftButton)
{
    sendMouse(view, QEvent::MouseButtonPress, p, b, b, m);
}

inline void move(CanvasView& view, QPointF p, Qt::KeyboardModifiers m = Qt::NoModifier,
                 Qt::MouseButtons held = Qt::LeftButton)
{
    sendMouse(view, QEvent::MouseMove, p, Qt::NoButton, held, m);
}

inline void release(CanvasView& view, QPointF p, Qt::KeyboardModifiers m = Qt::NoModifier,
                    Qt::MouseButton b = Qt::LeftButton)
{
    sendMouse(view, QEvent::MouseButtonRelease, p, b, Qt::NoButton, m);
}

inline void click(CanvasView& view, QPointF p, Qt::KeyboardModifiers m = Qt::NoModifier)
{
    press(view, p, m);
    release(view, p, m);
}

inline void doubleClick(CanvasView& view, QPointF p, Qt::KeyboardModifiers m = Qt::NoModifier)
{
    press(view, p, m);
    release(view, p, m);
    sendMouse(view, QEvent::MouseButtonDblClick, p, Qt::LeftButton, Qt::LeftButton, m);
    release(view, p, m);
}

// Drags from `from` to `to` in `steps` moves.
inline void drag(CanvasView& view, QPointF from, QPointF to, Qt::KeyboardModifiers m = Qt::NoModifier, int steps = 5)
{
    press(view, from, m);
    for (int i = 1; i <= steps; ++i)
        move(view, from + (to - from) * (double(i) / steps), m);
    release(view, to, m);
}

} // namespace qraw::ui::test
