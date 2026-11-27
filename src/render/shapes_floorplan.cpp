// Floorplan shapes that draw.io implements as JavaScript classes:
// mxgraph.floorplan.wall, wallCorner, wallU, window, dimension, doorLeft,
// doorRight and doorDouble (the rest of the floorplan palette is stencils).
//
// Port of shapes/mxFloorplan.js (draw.io, Apache-2.0), modified. All of them
// draw in coordinates relative to the shape origin.

#include "shapes_common.h"

namespace qraw::render::detail {

namespace {

// mxFloorplanWall (:11): a horizontal bar of `wallThickness` through the middle.
class WallShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const double wallTh = context.style.number("wallThickness", 10);
        c.translate(x, y);
        c.rect(0, h * 0.5 - wallTh * 0.5, w, wallTh);
        c.fillAndStroke();
    }
};

// mxFloorplanWallCorner (:60)
class WallCornerShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const double wallTh = context.style.number("wallThickness", 10);
        c.translate(x, y);
        c.begin();
        c.moveTo(0, h);
        c.lineTo(0, 0);
        c.lineTo(w, 0);
        c.lineTo(w, wallTh);
        c.lineTo(wallTh, wallTh);
        c.lineTo(wallTh, h);
        c.close();
        c.fillAndStroke();
    }
};

// mxFloorplanWallU (:117): a U of `wallThickness` open at the bottom.
class WallUShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const double wallTh = context.style.number("wallThickness", 10);
        c.translate(x, y);
        c.begin();
        c.moveTo(0, h);
        c.lineTo(0, 0);
        c.lineTo(w, 0);
        c.lineTo(w, h);
        c.lineTo(w - wallTh, h);
        c.lineTo(w - wallTh, wallTh);
        c.lineTo(wallTh, wallTh);
        c.lineTo(wallTh, h);
        c.close();
        c.fillAndStroke();
    }
};

// mxFloorplanWindow (:236): the wall bar with a center line.
class WindowShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const double wallTh = context.style.number("wallThickness", 10);
        c.translate(x, y);
        c.rect(0, h * 0.5 - wallTh * 0.5, w, wallTh);
        c.fillAndStroke();
        c.begin();
        c.moveTo(0, h * 0.5);
        c.lineTo(w, h * 0.5);
        c.stroke();
    }
};

// mxFloorplanDimension (:290): a dimension line 20 units from the top.
class DimensionShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext&, double x, double y, double w, double h) const override
    {
        c.translate(x, y);
        c.begin();
        c.moveTo(0, 20);
        c.lineTo(w, 20);
        c.moveTo(10, 15);
        c.lineTo(0, 20);
        c.lineTo(10, 25);
        c.moveTo(w - 10, 15);
        c.lineTo(w, 20);
        c.lineTo(w - 10, 25);
        c.moveTo(0, 15);
        c.lineTo(0, h);
        c.moveTo(w, 15);
        c.lineTo(w, h);
        c.stroke();
    }
};

// mxFloorplanDoorLeft (:631) and mxFloorplanDoorRight (:680): a 5 unit wall
// strip and a quarter-circle swing.
class DoorShape final : public VertexShape {
public:
    explicit DoorShape(bool hingeLeft) : hingeLeft_(hingeLeft) {}

    void paint(Canvas& c, const ShapePaintContext&, double x, double y, double w, double) const override
    {
        c.translate(x, y);
        c.rect(0, 0, w, 5);
        c.fillAndStroke();
        c.begin();
        if (hingeLeft_) {
            c.moveTo(w, 5);
            c.arcTo(w, w, 0, false, true, 0, 5 + w);
            c.lineTo(0, 5);
        } else {
            c.moveTo(0, 5);
            c.arcTo(w, w, 0, false, false, w, 5 + w);
            c.lineTo(w, 5);
        }
        c.stroke();
    }

private:
    bool hingeLeft_;
};

// mxFloorplanDoorDouble (:729): two leaves swinging from the middle.
class DoubleDoorShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext&, double x, double y, double w, double) const override
    {
        const double halfW = w * 0.5;
        c.translate(x, y);
        c.rect(0, 0, w, 5);
        c.fillAndStroke();
        c.begin();
        c.moveTo(halfW, 0);
        c.lineTo(halfW, 5);
        c.moveTo(halfW, 5);
        c.arcTo(halfW, halfW, 0, false, true, 0, 5 + halfW);
        c.lineTo(0, 5);
        c.moveTo(halfW, 5);
        c.arcTo(halfW, halfW, 0, false, false, w, 5 + halfW);
        c.lineTo(w, 5);
        c.stroke();
    }
};

} // namespace

void registerFloorplanShapes(ShapeRegistry& registry)
{
    registry.add("mxgraph.floorplan.wall", std::make_unique<WallShape>());
    registry.add("mxgraph.floorplan.wallCorner", std::make_unique<WallCornerShape>());
    registry.add("mxgraph.floorplan.wallU", std::make_unique<WallUShape>());
    registry.add("mxgraph.floorplan.window", std::make_unique<WindowShape>());
    registry.add("mxgraph.floorplan.dimension", std::make_unique<DimensionShape>());
    registry.add("mxgraph.floorplan.doorLeft", std::make_unique<DoorShape>(true));
    registry.add("mxgraph.floorplan.doorRight", std::make_unique<DoorShape>(false));
    registry.add("mxgraph.floorplan.doorDouble", std::make_unique<DoubleDoorShape>());
}

} // namespace qraw::render::detail
