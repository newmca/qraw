#pragma once

// The render IR: a recorded sequence of Canvas calls (docs/research/
// fast-canvas-architectures.md 3.1; DECISIONS.md D-032).
//
// A cell is painted once into a RecordingCanvas, which keeps every call as a
// compact op (paths, text blocks and image handles by value) and tracks the
// area the calls can touch. The finished DisplayList is immutable: replay()
// re-issues the calls on any Canvas (PainterCanvas on a tile, SvgCanvas in a
// test) and, since the calls are the same ones, paints exactly what painting
// directly would have painted. Recording replaces the per-paint style
// lookups, stencil interpretation, sketch set-up and path building; replaying
// is a loop over plain data and holds no pointer into the live model, so it
// can run on any thread.
//
// Not QPicture: measured slower than direct drawing (qt-canvas-performance.md,
// section 3), it hides the ops, and it cannot give bounds.

#include "qraw/render/canvas.h"

#include <QColor>
#include <QRectF>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace qraw::render {

// One recorded Canvas call. Small numeric arguments are inline; paths, texts,
// images, strings, colours and gradients live in the list's side tables
// (`index`; for colours index 0 means "no colour").
enum class OpKind : std::uint8_t {
    Save,
    Restore,
    Scale,
    Translate,
    Rotate,
    SetAlpha,
    SetFillAlpha,
    SetStrokeAlpha,
    SetFillColor,
    SetGradient,
    SetStrokeColor,
    SetStrokeWidth,
    SetDashed,
    SetDashPattern,
    SetLineCap,
    SetLineJoin,
    SetMiterLimit,
    SetShadow,
    Begin,
    MoveTo,
    LineTo,
    QuadTo,
    CurveTo,
    ArcTo,
    Close,
    Rect,
    RoundRect,
    Ellipse,
    AddPath,
    Fill,
    Stroke,
    FillAndStroke,
    Image,
    Text,
    TextOnPath,
    BeginGroup,
    EndGroup,
};

struct Op {
    OpKind kind = OpKind::Save;
    std::uint8_t flags = 0;
    std::uint32_t index = 0;
    double v[6] = {0, 0, 0, 0, 0, 0};
};

// What replaying a list costs, from counts that are known while recording
// (the tile scheduler orders and budgets tile work with it).
struct DisplayListCost {
    std::size_t ops = 0;
    std::size_t glyphs = 0;      // characters of text blocks
    double imagePixels = 0;      // natural pixels of the images (0 when not decoded yet)
    // The largest font size (canvas units, scaled by the transform in force)
    // of the label's text blocks; 0 without a label text. Level of detail
    // (level_of_detail.h) skips a label whose size on the screen is below a
    // few pixels.
    double labelFontPx = 0;
};

class DisplayList {
public:
    // Re-issues the calls [from, to) (the whole list by default).
    void replay(Canvas& canvas) const { replay(canvas, 0, ops_.size()); }
    void replay(Canvas& canvas, std::size_t from, std::size_t to) const;
    // The calls of the shape / of the label; a cell is recorded as shape
    // first, label second, on one canvas, so that state a shape leaves behind
    // reaches its label exactly as it did when both were painted directly.
    void replayShape(Canvas& canvas) const { replay(canvas, 0, labelBegin_); }
    void replayLabel(Canvas& canvas) const { replay(canvas, labelBegin_, ops_.size()); }

    [[nodiscard]] std::size_t opCount() const { return ops_.size(); }
    [[nodiscard]] bool empty() const { return ops_.empty(); }
    [[nodiscard]] std::size_t labelBegin() const { return labelBegin_; }
    [[nodiscard]] const std::vector<Op>& ops() const { return ops_; }
    [[nodiscard]] const DisplayListCost& cost() const { return cost_; }

    // Everything the calls can paint, in canvas coordinates (a null rectangle
    // when nothing is painted): strokes at their full width and cap/join
    // extent, shadows, rotated and flipped content, images, and labels at the
    // exact size of their layout (labelMetrics). Grown, never exact to the
    // pixel: add the antialiasing slack of the target when culling.
    [[nodiscard]] const QRectF& shapeBounds() const { return shapeBounds_; }
    [[nodiscard]] const QRectF& labelBounds() const { return labelBounds_; }
    [[nodiscard]] QRectF bounds() const { return shapeBounds_.united(labelBounds_); }

    // Heap bytes held (the ops and the side tables), for cache budgets.
    [[nodiscard]] std::size_t memoryBytes() const;

private:
    friend class RecordingCanvas;

    struct Gradient {
        QColor c1;
        QColor c2;
        Rect bounds;
        GradientDirection direction = GradientDirection::South;
        double alpha1 = 1;
        double alpha2 = 1;
    };

    std::vector<Op> ops_;
    std::size_t labelBegin_ = 0;
    std::vector<QColor> colors_;
    std::vector<Gradient> gradients_;
    std::vector<geometry::Path> paths_;
    std::vector<ImageData> images_;
    std::vector<TextBlock> texts_;
    std::vector<PathText> pathTexts_;
    std::vector<std::string> strings_;
    QRectF shapeBounds_;
    QRectF labelBounds_;
    DisplayListCost cost_;
};

// A Canvas that records. Use: paint the cell's shape, markLabelStart(), paint
// its label, finish(). Not thread-safe; one per recording.
class RecordingCanvas final : public Canvas {
public:
    RecordingCanvas();
    ~RecordingCanvas() override;
    RecordingCanvas(const RecordingCanvas&) = delete;
    RecordingCanvas& operator=(const RecordingCanvas&) = delete;

    // Calls recorded from now on belong to the label (DisplayList::replayLabel).
    void markLabelStart();
    // The finished list; the canvas is empty afterwards.
    [[nodiscard]] DisplayList finish();

    void save() override;
    void restore() override;
    void scale(double s) override;
    void translate(double dx, double dy) override;
    void rotate(double degrees, bool flipH, bool flipV, double cx, double cy) override;

    void setAlpha(double alpha) override;
    void setFillAlpha(double alpha) override;
    void setStrokeAlpha(double alpha) override;
    void setFillColor(std::optional<QColor> color) override;
    void setGradient(const QColor& c1, const QColor& c2, const Rect& bounds, GradientDirection direction,
                     double alpha1, double alpha2) override;
    void setStrokeColor(std::optional<QColor> color) override;
    void setStrokeWidth(double width) override;
    void setDashed(bool dashed, bool fixDash) override;
    void setDashPattern(std::string_view pattern) override;
    void setLineCap(LineCap cap) override;
    void setLineJoin(LineJoin join) override;
    void setMiterLimit(double limit) override;
    void setShadow(bool enabled) override;

    void begin() override;
    void moveTo(double x, double y) override;
    void lineTo(double x, double y) override;
    void quadTo(double x1, double y1, double x2, double y2) override;
    void curveTo(double x1, double y1, double x2, double y2, double x3, double y3) override;
    void arcTo(double rx, double ry, double angle, bool largeArc, bool sweep, double x, double y) override;
    void close() override;
    void rect(double x, double y, double w, double h) override;
    void roundrect(double x, double y, double w, double h, double dx, double dy) override;
    void ellipse(double x, double y, double w, double h) override;
    void addPath(const geometry::Path& path) override;
    void fill() override;
    void stroke() override;
    void fillAndStroke() override;

    void image(double x, double y, double w, double h, const ImageData& image, bool aspect, bool flipH,
               bool flipV) override;
    void text(const TextBlock& block) override;
    void textOnPath(const PathText& text) override;

    void beginGroup(std::string_view cellId) override;
    void endGroup() override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::render
