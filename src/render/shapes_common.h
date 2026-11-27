#pragma once

// Private helpers shared by the native shape painters (shapes_*.cpp). Not
// installed, not part of the public render API.
//
// The class hierarchy mirrors the upstream one where the paint order matters:
//   ActorShape       mxActor      translate, begin, redrawPath, fillAndStroke
//   BackgroundForegroundShape     mxShape.paintVertexShape: background, then
//                                 the foreground with the shadow off
//   RectangleShape   mxRectangleShape (process, plus, internalStorage, label)

#include "qraw/render/image_cache.h"
#include "qraw/render/shape.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace qraw::render::detail {

// Math.max(lo, Math.min(hi, v)), the clamp idiom of Shapes.js.
[[nodiscard]] inline double clampTo(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }

// JavaScript Math.round: halves round up (toward +infinity).
[[nodiscard]] inline double jsRound(double v) { return std::floor(v + 0.5); }

// mxUtils.getValue(style, 'arcSize', LINE_ARCSIZE) / 2: the arc size upstream
// hands to addPoints() for rounded polygons.
[[nodiscard]] inline double polygonArcSize(const ResolvedStyle& style)
{
    return style.number("arcSize", geometry::kLineArcSize) / 2;
}

// mxShape.getArcSize (mxgraph/src/shape/mxShape.js:1424).
[[nodiscard]] inline double rectangleArcSize(const ResolvedStyle& style, double w, double h)
{
    const double arc = style.number("arcSize", style.absoluteArcSize() ? geometry::kLineArcSize
                                                                         : kRectangleRoundingFactor * 100);
    if (style.absoluteArcSize())
        return std::min(w / 2, std::min(h / 2, arc / 2));
    const double f = arc / 100;
    return std::min(w * f, h * f);
}

// mxShape.addPoints(c, pts, this.isRounded, arcSize, close, exclude) on the
// current path of `c`.
inline void addPoints(Canvas& c, const ResolvedStyle& style, std::span<const Point> points, bool close = true,
                      std::span<const int> exclude = {})
{
    c.addPath(geometry::polylinePath(points, style.rounded(), polygonArcSize(style), close, exclude));
}

// Label margins as upstream builds them: mxRectangle(left, top, right,
// bottom) (x = left, y = top, width = right, height = bottom).
[[nodiscard]] inline geometry::Insets margins(double left, double top, double right, double bottom)
{
    return {top, left, bottom, right};
}

// The `boundedLbl` style: JS treats any present value as truthy, we read the
// usual 1/true flag so that boundedLbl=0 switches it off.
[[nodiscard]] inline bool boundedLabel(const ResolvedStyle& style) { return style.flag("boundedLbl", false); }

// mxActor: the path is built in coordinates relative to the shape origin.
class ActorShape : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        c.translate(x, y);
        c.begin();
        redrawPath(c, context.style, w, h);
        c.fillAndStroke();
    }

protected:
    // mxActor.redrawPath(c, x, y, w, h) with the translated origin (x = y = 0).
    virtual void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h) const = 0;
};

// mxShape.paintVertexShape (mxShape.js:1230): paintBackground(), then
// paintForeground() with the shadow switched off.
class BackgroundForegroundShape : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        paintBackground(c, context, x, y, w, h);
        c.setShadow(false);
        paintForeground(c, context, x, y, w, h);
    }

protected:
    virtual void paintBackground(Canvas& c, const ShapePaintContext& context, double x, double y, double w,
                                 double h) const = 0;
    virtual void paintForeground(Canvas&, const ShapePaintContext&, double, double, double, double) const {}
};

// mxRectangleShape.paintBackground (mxRectangleShape.js:62). Glass
// (`glass=1`) and footerSize are not painted (docs/design/render.md,
// section 11).
class RectangleShape : public BackgroundForegroundShape {
protected:
    void paintBackground(Canvas& c, const ShapePaintContext& context, double x, double y, double w,
                         double h) const override;
};

// mxCylinder.paintVertexShape (mxCylinder.js:54): the background path is
// filled and stroked, then the foreground path is stroked with the shadow
// off. Shapes only provide redrawPath(isForeground).
class CylinderBase : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        c.translate(x, y);
        c.begin();
        redrawPath(c, context.style, w, h, false);
        c.fillAndStroke();

        c.setShadow(false);
        c.begin();
        redrawPath(c, context.style, w, h, true);
        c.stroke();
    }

protected:
    // Coordinates are relative to the shape origin (translated).
    virtual void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h, bool isForeground) const = 0;
};

// Registration, one function per shapes_*.cpp.
void registerBasicShapes(ShapeRegistry& registry);
void registerFlowchartShapes(ShapeRegistry& registry);
void registerContainerShapes(ShapeRegistry& registry);
void registerImageShapes(ShapeRegistry& registry);
void registerUmlShapes(ShapeRegistry& registry);       // Shapes.js: cube, folder, umlLifeline, arrows, ...
void registerRect2Shapes(ShapeRegistry& registry);     // mxgraph.basic.rect
void registerBpmnShapes(ShapeRegistry& registry);      // mxgraph.bpmn.{event,gateway2,task2,shape}
void registerFloorplanShapes(ShapeRegistry& registry); // mxgraph.floorplan.*
void registerArrowShapes(ShapeRegistry& registry);     // mxgraph.arrows2.*
void registerCurvedTextShape(ShapeRegistry& registry); // curvedText

} // namespace qraw::render::detail
