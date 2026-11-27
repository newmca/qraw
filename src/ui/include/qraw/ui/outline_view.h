#pragma once
// Outline / minimap (OutlineWindow, js/grapheditor/Dialogs.js:4632,
// mxOutline).

#include <QRectF>
#include <QWidget>

#include <memory>

namespace qraw::ui {

class CanvasView;

// Paints the canvas scene scaled to fit (QGraphicsScene::render into the
// widget, cached in a QPixmap rebuilt on DiagramScene::projectionChanged and
// on resize) and the visible viewport of the CanvasView as a blue rectangle
// (#3d9ef1-ish, draw.io mxOutline.border). Dragging inside the rectangle
// (or clicking elsewhere, which centres there) scrolls the canvas; the wheel
// zooms the canvas (CanvasView::setZoom). Updates when the canvas scrolls
// or zooms.
class OutlineView : public QWidget {
    Q_OBJECT

public:
    explicit OutlineView(CanvasView& canvas, QWidget* parent = nullptr);
    ~OutlineView() override;

    // The viewport rectangle in widget coordinates (for tests).
    [[nodiscard]] QRectF viewportRect() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::ui
