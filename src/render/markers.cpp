// Port of mxgraph/src/shape/mxMarker.js, js/grapheditor/Shapes.js (markers
// registered at 8280-8534) and shapes/er/mxER.js (markers at 1247-1357)
// (draw.io, Apache-2.0), modified.
//
// Every marker is a factory with upstream's signature that moves the line end
// point back and returns the function painting the marker. The painters
// issue the same canvas calls as upstream, in the same order. Deviations:
//  - `end` (upstream: the shared mxPoint `pe`) is copied after the factory has
//    adjusted it wherever the painter reads it, so the painter does not depend
//    on the caller keeping that point alive or unchanged.
//  - oval and circle call begin() before ellipse(): upstream's SVG canvas
//    creates an ellipse node that needs no path, ours adds a closed subpath to
//    the current path.

#include "qraw/render/markers.h"

#include <algorithm>
#include <array>

namespace qraw::render {

namespace {

// The arguments of a marker factory (mxMarker.createMarker).
struct Args {
    Canvas& canvas;
    const ResolvedStyle& style;
    std::string_view type;
    Point& end; // pe, adjusted in place
    double unitX;
    double unitY;
    double size;
    bool source;
    double sw; // stroke width
    bool filled;
};

using Factory = MarkerPainter (*)(const Args&);

void paintFilledOrStroked(Canvas& c, bool filled)
{
    if (filled)
        c.fillAndStroke();
    else
        c.stroke();
}

// --- mxMarker.js -----------------------------------------------------------

// createArrow(widthFactor): classic, classicThin, block, blockThin.
MarkerPainter createArrow(const Args& a, double widthFactor)
{
    // The angle of the forward facing arrow sides against the x axis is
    // 26.565 degrees, 1/sin(26.565) = 2.236 / 2 = 1.118 (/ 2 allows for only
    // half the strokewidth being processed).
    const double endOffsetX = a.unitX * a.sw * 1.118;
    const double endOffsetY = a.unitY * a.sw * 1.118;

    const double unitX = a.unitX * (a.size + a.sw);
    const double unitY = a.unitY * (a.size + a.sw);

    Point pt = a.end;
    pt.x -= endOffsetX;
    pt.y -= endOffsetY;

    const bool classic = a.type == "classic" || a.type == "classicThin";
    const double f = classic ? 3.0 / 4 : 1;
    a.end.x += -unitX * f - endOffsetX;
    a.end.y += -unitY * f - endOffsetY;

    Canvas* c = &a.canvas;
    const bool filled = a.filled;
    return [=] {
        c->begin();
        c->moveTo(pt.x, pt.y);
        c->lineTo(pt.x - unitX - unitY / widthFactor, pt.y - unitY + unitX / widthFactor);
        if (classic)
            c->lineTo(pt.x - unitX * 3 / 4, pt.y - unitY * 3 / 4);
        c->lineTo(pt.x + unitY / widthFactor - unitX, pt.y - unitY - unitX / widthFactor);
        c->close();
        paintFilledOrStroked(*c, filled);
    };
}

// createOpenArrow(widthFactor): open, openThin.
MarkerPainter createOpenArrow(const Args& a, double widthFactor)
{
    const double endOffsetX = a.unitX * a.sw * 1.118;
    const double endOffsetY = a.unitY * a.sw * 1.118;

    const double unitX = a.unitX * (a.size + a.sw);
    const double unitY = a.unitY * (a.size + a.sw);

    Point pt = a.end;
    pt.x -= endOffsetX;
    pt.y -= endOffsetY;

    a.end.x += -endOffsetX * 2;
    a.end.y += -endOffsetY * 2;

    Canvas* c = &a.canvas;
    return [=] {
        c->begin();
        c->moveTo(pt.x - unitX - unitY / widthFactor, pt.y - unitY + unitX / widthFactor);
        c->lineTo(pt.x, pt.y);
        c->lineTo(pt.x + unitY / widthFactor - unitX, pt.y - unitY - unitX / widthFactor);
        c->stroke();
    };
}

MarkerPainter oval(const Args& a)
{
    const double r = a.size / 2;
    const Point pt = a.end;
    a.end.x -= a.unitX * r;
    a.end.y -= a.unitY * r;

    Canvas* c = &a.canvas;
    const double size = a.size;
    const bool filled = a.filled;
    return [=] {
        c->begin();
        c->ellipse(pt.x - r, pt.y - r, size, size);
        paintFilledOrStroked(*c, filled);
    };
}

MarkerPainter baseDash(const Args& a)
{
    const double nx = a.unitX * (a.size + a.sw + 1);
    const double ny = a.unitY * (a.size + a.sw + 1);
    const Point pe = a.end;

    Canvas* c = &a.canvas;
    return [=] {
        c->begin();
        c->moveTo(pe.x - ny / 2, pe.y + nx / 2);
        c->lineTo(pe.x + ny / 2, pe.y - nx / 2);
        c->stroke();
    };
}

MarkerPainter doubleBlock(const Args& a)
{
    const double widthFactor = 2;

    const double endOffsetX = a.unitX * a.sw * 1.118;
    const double endOffsetY = a.unitY * a.sw * 1.118;

    const double unitX = a.unitX * (a.size + a.sw);
    const double unitY = a.unitY * (a.size + a.sw);

    Point pt = a.end;
    pt.x -= endOffsetX;
    pt.y -= endOffsetY;

    // Upstream's `type != classic` test is always true for this marker.
    const double f = 1;
    a.end.x += -unitX * f * 2 - endOffsetX;
    a.end.y += -unitY * f * 2 - endOffsetY;

    Canvas* c = &a.canvas;
    const bool filled = a.filled;
    return [=] {
        c->begin();
        c->moveTo(pt.x, pt.y);
        c->lineTo(pt.x - unitX - unitY / widthFactor, pt.y - unitY + unitX / widthFactor);
        c->lineTo(pt.x + unitY / widthFactor - unitX, pt.y - unitY - unitX / widthFactor);
        c->close();
        c->moveTo(pt.x - unitX, pt.y - unitY);
        c->lineTo(pt.x - 2 * unitX - 0.5 * unitY, pt.y + 0.5 * unitX - 2 * unitY);
        c->lineTo(pt.x - 2 * unitX + 0.5 * unitY, pt.y - 0.5 * unitX - 2 * unitY);
        c->close();
        paintFilledOrStroked(*c, filled);
    };
}

// diamond and diamondThin.
MarkerPainter diamond(const Args& a)
{
    // The angle of the forward facing arrow sides against the x axis is
    // 45 degrees, 1/sin(45) = 1.4142 / 2 = 0.7071 (/ 2 allows for only half
    // the strokewidth being processed), or 0.9862 for the thin diamond.
    const bool regular = a.type == "diamond";
    const double swFactor = regular ? 0.7071 : 0.9862;
    const double endOffsetX = a.unitX * a.sw * swFactor;
    const double endOffsetY = a.unitY * a.sw * swFactor;

    const double unitX = a.unitX * (a.size + a.sw);
    const double unitY = a.unitY * (a.size + a.sw);

    Point pt = a.end;
    pt.x -= endOffsetX;
    pt.y -= endOffsetY;

    a.end.x += -unitX - endOffsetX;
    a.end.y += -unitY - endOffsetY;

    // Thickness factor of the diamond.
    const double tk = regular ? 2 : 3.4;

    Canvas* c = &a.canvas;
    const bool filled = a.filled;
    return [=] {
        c->begin();
        c->moveTo(pt.x, pt.y);
        c->lineTo(pt.x - unitX / 2 - unitY / tk, pt.y + unitX / tk - unitY / 2);
        c->lineTo(pt.x - unitX, pt.y - unitY);
        c->lineTo(pt.x - unitX / 2 + unitY / tk, pt.y - unitY / 2 - unitX / tk);
        c->close();
        paintFilledOrStroked(*c, filled);
    };
}

MarkerPainter manyOptional(const Args& a)
{
    const double nx = a.unitX * (a.size + a.sw + 1);
    const double ny = a.unitY * (a.size + a.sw + 1);
    const double r = a.size / 2;
    const double px = a.end.x;
    const double py = a.end.y;

    a.end.x -= 2 * nx - a.unitX * a.sw / 2;
    a.end.y -= 2 * ny - a.unitY * a.sw / 2;

    Canvas* c = &a.canvas;
    const bool filled = a.filled;
    return [=] {
        c->begin();
        c->ellipse(px - 1.5 * nx - r, py - 1.5 * ny - r, 2 * r, 2 * r);
        paintFilledOrStroked(*c, filled);

        c->begin();
        c->moveTo(px, py);
        c->lineTo(px - nx, py - ny);

        c->moveTo(px + ny / 2, py - nx / 2);
        c->lineTo(px - nx, py - ny);
        c->lineTo(px - ny / 2, py + nx / 2);

        c->stroke();
    };
}

// --- Shapes.js ---------------------------------------------------------------

MarkerPainter dash(const Args& a)
{
    const double nx = a.unitX * (a.size + a.sw + 1);
    const double ny = a.unitY * (a.size + a.sw + 1);
    const Point pe = a.end;

    Canvas* c = &a.canvas;
    return [=] {
        c->begin();
        c->moveTo(pe.x - nx / 2 - ny / 2, pe.y - ny / 2 + nx / 2);
        c->lineTo(pe.x + ny / 2 - 3 * nx / 2, pe.y - 3 * ny / 2 - nx / 2);
        c->stroke();
    };
}

MarkerPainter box(const Args& a)
{
    const double nx = a.unitX * (a.size + a.sw + 1);
    const double ny = a.unitY * (a.size + a.sw + 1);
    const double px = a.end.x + nx / 2;
    const double py = a.end.y + ny / 2;

    a.end.x -= nx;
    a.end.y -= ny;

    Canvas* c = &a.canvas;
    const bool filled = a.filled;
    return [=] {
        c->begin();
        c->moveTo(px - nx / 2 - ny / 2, py - ny / 2 + nx / 2);
        c->lineTo(px - nx / 2 + ny / 2, py - ny / 2 - nx / 2);
        c->lineTo(px + ny / 2 - 3 * nx / 2, py - 3 * ny / 2 - nx / 2);
        c->lineTo(px - ny / 2 - 3 * nx / 2, py - 3 * ny / 2 + nx / 2);
        c->close();
        paintFilledOrStroked(*c, filled);
    };
}

MarkerPainter cross(const Args& a)
{
    const double nx = a.unitX * (a.size + a.sw + 1);
    const double ny = a.unitY * (a.size + a.sw + 1);
    const Point pe = a.end;

    Canvas* c = &a.canvas;
    return [=] {
        c->begin();
        c->moveTo(pe.x - nx / 2 - ny / 2, pe.y - ny / 2 + nx / 2);
        c->lineTo(pe.x + ny / 2 - 3 * nx / 2, pe.y - 3 * ny / 2 - nx / 2);
        c->moveTo(pe.x - nx / 2 + ny / 2, pe.y - ny / 2 - nx / 2);
        c->lineTo(pe.x - ny / 2 - 3 * nx / 2, pe.y - 3 * ny / 2 + nx / 2);
        c->stroke();
    };
}

MarkerPainter circle(const Args& a)
{
    const double size = a.size + a.sw;
    const Point pt = a.end;

    a.end.x -= a.unitX * (2 * size + a.sw);
    a.end.y -= a.unitY * (2 * size + a.sw);

    const double unitX = a.unitX * (size + a.sw);
    const double unitY = a.unitY * (size + a.sw);

    Canvas* c = &a.canvas;
    const bool filled = a.filled;
    return [=] {
        c->begin();
        c->ellipse(pt.x - unitX - size, pt.y - unitY - size, 2 * size, 2 * size);
        paintFilledOrStroked(*c, filled);
    };
}

MarkerPainter circlePlus(const Args& a)
{
    const Point pt = a.end;
    const MarkerPainter circlePainter = circle(a);
    const double unitX = a.unitX;
    const double unitY = a.unitY;
    const double sw = a.sw;
    const double nx = unitX * (a.size + 2 * sw);
    const double ny = unitY * (a.size + 2 * sw);

    Canvas* c = &a.canvas;
    return [=] {
        circlePainter();

        c->begin();
        c->moveTo(pt.x - unitX * sw, pt.y - unitY * sw);
        c->lineTo(pt.x - 2 * nx + unitX * sw, pt.y - 2 * ny + unitY * sw);
        c->moveTo(pt.x - nx - ny + unitY * sw, pt.y - ny + nx - unitX * sw);
        c->lineTo(pt.x + ny - nx - unitY * sw, pt.y - ny - nx + unitX * sw);
        c->stroke();
    };
}

MarkerPainter halfCircle(const Args& a)
{
    const double nx = a.unitX * (a.size + a.sw + 1);
    const double ny = a.unitY * (a.size + a.sw + 1);
    const Point pt = a.end;

    a.end.x -= nx;
    a.end.y -= ny;
    const Point pe = a.end; // the painter reads the adjusted end point

    Canvas* c = &a.canvas;
    return [=] {
        c->begin();
        c->moveTo(pt.x - ny, pt.y + nx);
        c->quadTo(pe.x - ny, pe.y + nx, pe.x, pe.y);
        c->quadTo(pe.x + ny, pe.y - nx, pt.x + ny, pt.y - nx);
        c->stroke();
    };
}

MarkerPainter async(const Args& a)
{
    const double endOffsetX = a.unitX * a.sw * 1.118;
    const double endOffsetY = a.unitY * a.sw * 1.118;

    const double unitX = a.unitX * (a.size + a.sw);
    const double unitY = a.unitY * (a.size + a.sw);

    Point pt = a.end;
    pt.x -= endOffsetX;
    pt.y -= endOffsetY;

    const double f = 1;
    a.end.x += -unitX * f - endOffsetX;
    a.end.y += -unitY * f - endOffsetY;

    Canvas* c = &a.canvas;
    const bool source = a.source;
    const bool filled = a.filled;
    return [=] {
        c->begin();
        c->moveTo(pt.x, pt.y);

        if (source)
            c->lineTo(pt.x - unitX - unitY / 2, pt.y - unitY + unitX / 2);
        else
            c->lineTo(pt.x + unitY / 2 - unitX, pt.y - unitY - unitX / 2);

        c->lineTo(pt.x - unitX, pt.y - unitY);
        c->close();
        paintFilledOrStroked(*c, filled);
    };
}

MarkerPainter openAsync(const Args& a)
{
    const double widthFactor = 2;
    const double unitX = a.unitX * (a.size + a.sw);
    const double unitY = a.unitY * (a.size + a.sw);
    const Point pt = a.end;

    Canvas* c = &a.canvas;
    const bool source = a.source;
    return [=] {
        c->begin();
        c->moveTo(pt.x, pt.y);

        if (source)
            c->lineTo(pt.x - unitX - unitY / widthFactor, pt.y - unitY + unitX / widthFactor);
        else
            c->lineTo(pt.x + unitY / widthFactor - unitX, pt.y - unitY - unitX / widthFactor);

        c->stroke();
    };
}

// --- mxER.js -------------------------------------------------------------------

MarkerPainter erOne(const Args& a)
{
    const double nx = a.unitX * (a.size + a.sw + 1);
    const double ny = a.unitY * (a.size + a.sw + 1);
    const Point pe = a.end;

    Canvas* c = &a.canvas;
    return [=] {
        c->begin();
        c->moveTo(pe.x - nx / 2 - ny / 2, pe.y - ny / 2 + nx / 2);
        c->lineTo(pe.x - nx / 2 + ny / 2, pe.y - ny / 2 - nx / 2);
        c->stroke();
    };
}

MarkerPainter erMandOne(const Args& a)
{
    const double nx = a.unitX * (a.size + a.sw + 1);
    const double ny = a.unitY * (a.size + a.sw + 1);
    const Point pe = a.end;

    Canvas* c = &a.canvas;
    return [=] {
        c->begin();
        c->moveTo(pe.x - nx / 2 - ny / 2, pe.y - ny / 2 + nx / 2);
        c->lineTo(pe.x - nx / 2 + ny / 2, pe.y - ny / 2 - nx / 2);
        c->moveTo(pe.x - nx - ny / 2, pe.y - ny + nx / 2);
        c->lineTo(pe.x - nx + ny / 2, pe.y - ny - nx / 2);
        c->stroke();
    };
}

MarkerPainter erMany(const Args& a)
{
    const double nx = a.unitX * (a.size + a.sw + 1);
    const double ny = a.unitY * (a.size + a.sw + 1);
    const Point pe = a.end;

    Canvas* c = &a.canvas;
    return [=] {
        c->begin();
        c->moveTo(pe.x + ny / 2, pe.y - nx / 2);
        c->lineTo(pe.x - nx, pe.y - ny);
        c->lineTo(pe.x - ny / 2, pe.y + nx / 2);
        c->stroke();
    };
}

MarkerPainter erOneToMany(const Args& a)
{
    const double nx = a.unitX * (a.size + a.sw + 1);
    const double ny = a.unitY * (a.size + a.sw + 1);
    const Point pe = a.end;

    Canvas* c = &a.canvas;
    return [=] {
        c->begin();
        c->moveTo(pe.x - nx - ny / 2, pe.y - ny + nx / 2);
        c->lineTo(pe.x - nx + ny / 2, pe.y - ny - nx / 2);
        c->moveTo(pe.x + ny / 2, pe.y - nx / 2);
        c->lineTo(pe.x - nx, pe.y - ny);
        c->lineTo(pe.x - ny / 2, pe.y + nx / 2);
        c->stroke();
    };
}

// The circle of the ER "zero or ..." markers. Filled circles are painted
// white; the fill color is then restored to the stroke color (`oldColor`).
// Upstream's TODO notes that markers normally fill with the stroke color.
void paintErCircle(Canvas& c, bool filled, const std::optional<QColor>& oldColor)
{
    if (filled) {
        c.setFillColor(QColor(0xff, 0xff, 0xff));
        c.fillAndStroke();
        c.setFillColor(oldColor);
    } else {
        c.stroke();
    }
}

// mxUtils.getValue(shape.style, 'strokeColor', '#666666')
std::optional<QColor> strokeColorOrDefault(const ResolvedStyle& style)
{
    return style.has("strokeColor") ? style.strokeColor() : std::optional<QColor>(QColor(0x66, 0x66, 0x66));
}

MarkerPainter erZeroToMany(const Args& a)
{
    const double nx = a.unitX * (a.size + a.sw + 1);
    const double ny = a.unitY * (a.size + a.sw + 1);
    const double r = a.size / 2;
    const double px = a.end.x;
    const double py = a.end.y;

    if (!a.filled) {
        a.end.x -= 2 * nx - a.unitX * a.sw / 2;
        a.end.y -= 2 * ny - a.unitY * a.sw / 2;
    }

    Canvas* c = &a.canvas;
    const bool filled = a.filled;
    const std::optional<QColor> oldColor = strokeColorOrDefault(a.style);
    return [=] {
        c->begin();
        c->ellipse(px - 1.5 * nx - r, py - 1.5 * ny - r, 2 * r, 2 * r);
        paintErCircle(*c, filled, oldColor);

        c->begin();
        c->moveTo(px + ny / 2, py - nx / 2);
        c->lineTo(px - nx, py - ny);
        c->lineTo(px - ny / 2, py + nx / 2);

        if (!filled) {
            c->moveTo(px - nx, py - ny);
            c->lineTo(px, py);
        }

        c->stroke();
    };
}

MarkerPainter erZeroToOne(const Args& a)
{
    const double nx = a.unitX * (a.size + a.sw + 1);
    const double ny = a.unitY * (a.size + a.sw + 1);
    const double r = a.size / 2;
    const double px = a.end.x;
    const double py = a.end.y;

    if (!a.filled) {
        a.end.x -= 2 * nx - a.unitX * a.sw / 2;
        a.end.y -= 2 * ny - a.unitY * a.sw / 2;
    }

    Canvas* c = &a.canvas;
    const bool filled = a.filled;
    const double unitX = a.unitX;
    const double unitY = a.unitY;
    const double sw = a.sw;
    const std::optional<QColor> oldColor = strokeColorOrDefault(a.style);
    return [=] {
        c->begin();
        c->ellipse(px - 1.5 * nx - r, py - 1.5 * ny - r, 2 * r, 2 * r);
        paintErCircle(*c, filled, oldColor);

        c->begin();
        c->moveTo(px - nx / 2 - ny / 2, py - ny / 2 + nx / 2);
        c->lineTo(px - nx / 2 + ny / 2, py - ny / 2 - nx / 2);

        if (!filled) {
            c->moveTo(px - nx - unitX * sw / 2, py - ny - unitY * sw / 2);
            c->lineTo(px, py);
        }

        c->stroke();
    };
}

// --- registry ------------------------------------------------------------------

struct Entry {
    std::string_view name;
    Factory factory;
};

// Sorted by name (byte order), so lookup is a binary search and markerNames()
// needs no extra sorting.
constexpr std::array<Entry, 26> kMarkers{{
    {"ERmandOne", erMandOne},
    {"ERmany", erMany},
    {"ERone", erOne},
    {"ERoneToMany", erOneToMany},
    {"ERzeroToMany", erZeroToMany},
    {"ERzeroToOne", erZeroToOne},
    {"async", async},
    {"baseDash", baseDash},
    {"block", [](const Args& a) { return createArrow(a, 2); }},
    {"blockThin", [](const Args& a) { return createArrow(a, 3); }},
    {"box", box},
    {"circle", circle},
    {"circlePlus", circlePlus},
    {"classic", [](const Args& a) { return createArrow(a, 2); }},
    {"classicThin", [](const Args& a) { return createArrow(a, 3); }},
    {"cross", cross},
    {"dash", dash},
    {"diamond", diamond},
    {"diamondThin", diamond},
    {"doubleBlock", doubleBlock},
    {"halfCircle", halfCircle},
    {"manyOptional", manyOptional},
    {"open", [](const Args& a) { return createOpenArrow(a, 2); }},
    {"openAsync", openAsync},
    {"openThin", [](const Args& a) { return createOpenArrow(a, 3); }},
    {"oval", oval},
}};

static_assert(std::is_sorted(kMarkers.begin(), kMarkers.end(),
                             [](const Entry& a, const Entry& b) { return a.name < b.name; }),
              "kMarkers must stay sorted by name");

const Entry* findMarker(std::string_view type)
{
    const auto it = std::lower_bound(kMarkers.begin(), kMarkers.end(), type,
                                     [](const Entry& e, std::string_view name) { return e.name < name; });
    return it != kMarkers.end() && it->name == type ? &*it : nullptr;
}

} // namespace

MarkerPainter createMarker(Canvas& canvas, const ResolvedStyle& style, std::string_view type, Point& end,
                           double unitX, double unitY, double size, bool source, double strokeWidth, bool filled)
{
    const Entry* entry = findMarker(type);
    if (!entry)
        return {};
    return entry->factory({canvas, style, type, end, unitX, unitY, size, source, strokeWidth, filled});
}

bool isKnownMarker(std::string_view type) { return findMarker(type) != nullptr; }

std::vector<std::string_view> markerNames()
{
    std::vector<std::string_view> names;
    names.reserve(kMarkers.size());
    for (const Entry& e : kMarkers)
        names.push_back(e.name);
    return names;
}

} // namespace qraw::render
