#pragma once

// Test double: a Canvas that records every call as one text line, so tests
// can assert the exact drawing sequence a painter issues (the same calls an
// upstream mxAbstractCanvas2D would receive). Header-only on purpose: it
// must not depend on any package's implementation.
//
// Line format: the method name followed by its arguments separated by
// single spaces; numbers via fmt() (up to 3 decimals, no trailing zeros),
// colors as #rrggbb or #rrggbbaa (alpha < 255), nullopt as "none", bools as
// 0/1. Examples:
//   "begin", "moveTo 10 20", "fillAndStroke", "setFillColor #ff0000",
//   "rect 0 0 120 60", "text 60 30 120 60 center middle Hello"

#include "qraw/render/canvas.h"

#include <QColor>

#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::test {

class RecordingCanvas final : public render::Canvas {
public:
    std::vector<std::string> ops;
    std::vector<render::PathText> pathTexts; // every textOnPath call, in full
    // Path built from moveTo/lineTo/quadTo/curveTo/close since the last
    // begin(); rect/roundrect/ellipse/arcTo/addPath are only logged.
    geometry::Path path;
    // Number of fill/stroke/fillAndStroke calls.
    int paints = 0;

    static std::string fmt(double v)
    {
        if (std::abs(v) < 0.0005)
            v = 0;
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.3f", v);
        std::string s(buf);
        while (!s.empty() && s.back() == '0')
            s.pop_back();
        if (!s.empty() && s.back() == '.')
            s.pop_back();
        return s;
    }
    static std::string color(const std::optional<QColor>& c)
    {
        if (!c)
            return "none";
        return c->alpha() == 255 ? c->name(QColor::HexRgb).toStdString()
                                 : c->name(QColor::HexArgb).toStdString();
    }
    static const char* b(bool v) { return v ? "1" : "0"; }

    // All ops joined with '\n'.
    [[nodiscard]] std::string text() const
    {
        std::string out;
        for (const auto& op : ops) {
            out += op;
            out += '\n';
        }
        return out;
    }
    // Ops that start with `prefix` (e.g. "fill", "text ").
    [[nodiscard]] std::vector<std::string> opsStartingWith(std::string_view prefix) const
    {
        std::vector<std::string> out;
        for (const auto& op : ops)
            if (std::string_view(op).substr(0, prefix.size()) == prefix)
                out.push_back(op);
        return out;
    }

    void save() override { log("save"); }
    void restore() override { log("restore"); }
    void scale(double s) override { log("scale " + fmt(s)); }
    void translate(double dx, double dy) override { log("translate " + fmt(dx) + ' ' + fmt(dy)); }
    void rotate(double degrees, bool flipH, bool flipV, double cx, double cy) override
    {
        log("rotate " + fmt(degrees) + ' ' + b(flipH) + ' ' + b(flipV) + ' ' + fmt(cx) + ' ' + fmt(cy));
    }
    void setAlpha(double a) override { log("setAlpha " + fmt(a)); }
    void setFillAlpha(double a) override { log("setFillAlpha " + fmt(a)); }
    void setStrokeAlpha(double a) override { log("setStrokeAlpha " + fmt(a)); }
    void setFillColor(std::optional<QColor> c) override { log("setFillColor " + color(c)); }
    void setGradient(const QColor& c1, const QColor& c2, const render::Rect& r, render::GradientDirection d,
                     double a1, double a2) override
    {
        log("setGradient " + color(c1) + ' ' + color(c2) + ' ' + fmt(r.x) + ' ' + fmt(r.y) + ' ' + fmt(r.width) +
            ' ' + fmt(r.height) + ' ' + std::to_string(static_cast<int>(d)) + ' ' + fmt(a1) + ' ' + fmt(a2));
    }
    void setStrokeColor(std::optional<QColor> c) override { log("setStrokeColor " + color(c)); }
    void setStrokeWidth(double w) override { log("setStrokeWidth " + fmt(w)); }
    void setDashed(bool dashed, bool fixDash) override { log(std::string("setDashed ") + b(dashed) + ' ' + b(fixDash)); }
    void setDashPattern(std::string_view p) override { log("setDashPattern " + std::string(p)); }
    void setLineCap(render::LineCap c) override { log("setLineCap " + std::to_string(static_cast<int>(c))); }
    void setLineJoin(render::LineJoin j) override { log("setLineJoin " + std::to_string(static_cast<int>(j))); }
    void setMiterLimit(double l) override { log("setMiterLimit " + fmt(l)); }
    void setShadow(bool on) override { log(std::string("setShadow ") + b(on)); }

    void begin() override
    {
        path.clear();
        log("begin");
    }
    void moveTo(double x, double y) override
    {
        path.moveTo({x, y});
        log("moveTo " + fmt(x) + ' ' + fmt(y));
    }
    void lineTo(double x, double y) override
    {
        path.lineTo({x, y});
        log("lineTo " + fmt(x) + ' ' + fmt(y));
    }
    void quadTo(double x1, double y1, double x2, double y2) override
    {
        path.quadTo({x1, y1}, {x2, y2});
        log("quadTo " + fmt(x1) + ' ' + fmt(y1) + ' ' + fmt(x2) + ' ' + fmt(y2));
    }
    void curveTo(double x1, double y1, double x2, double y2, double x3, double y3) override
    {
        path.cubicTo({x1, y1}, {x2, y2}, {x3, y3});
        log("curveTo " + fmt(x1) + ' ' + fmt(y1) + ' ' + fmt(x2) + ' ' + fmt(y2) + ' ' + fmt(x3) + ' ' + fmt(y3));
    }
    void arcTo(double rx, double ry, double angle, bool largeArc, bool sweep, double x, double y) override
    {
        log("arcTo " + fmt(rx) + ' ' + fmt(ry) + ' ' + fmt(angle) + ' ' + b(largeArc) + ' ' + b(sweep) + ' ' +
            fmt(x) + ' ' + fmt(y));
    }
    void close() override
    {
        path.close();
        log("close");
    }
    void rect(double x, double y, double w, double h) override
    {
        log("rect " + fmt(x) + ' ' + fmt(y) + ' ' + fmt(w) + ' ' + fmt(h));
    }
    void roundrect(double x, double y, double w, double h, double dx, double dy) override
    {
        log("roundrect " + fmt(x) + ' ' + fmt(y) + ' ' + fmt(w) + ' ' + fmt(h) + ' ' + fmt(dx) + ' ' + fmt(dy));
    }
    void ellipse(double x, double y, double w, double h) override
    {
        log("ellipse " + fmt(x) + ' ' + fmt(y) + ' ' + fmt(w) + ' ' + fmt(h));
    }
    void addPath(const geometry::Path& p) override
    {
        log("addPath " + std::to_string(p.elements().size()));
    }
    void fill() override
    {
        ++paints;
        log("fill");
    }
    void stroke() override
    {
        ++paints;
        log("stroke");
    }
    void fillAndStroke() override
    {
        ++paints;
        log("fillAndStroke");
    }
    void image(double x, double y, double w, double h, const render::ImageData& img, bool aspect, bool flipH,
               bool flipV) override
    {
        log("image " + fmt(x) + ' ' + fmt(y) + ' ' + fmt(w) + ' ' + fmt(h) + ' ' + img.mimeType.toStdString() + ' ' +
            b(aspect) + ' ' + b(flipH) + ' ' + b(flipV));
    }
    void text(const render::TextBlock& t) override
    {
        static constexpr const char* h[] = {"left", "center", "right"};
        static constexpr const char* v[] = {"top", "middle", "bottom"};
        log("text " + fmt(t.x) + ' ' + fmt(t.y) + ' ' + fmt(t.width) + ' ' + fmt(t.height) + ' ' +
            h[static_cast<int>(t.align)] + ' ' + v[static_cast<int>(t.verticalAlign)] + ' ' + t.text.content);
    }
    // "textOnPath <align> <textLength> <path ops joined by ';'> <text>"; path
    // ops as M/L/Q/C/Z with their coordinates, e.g. "M 6 25;C 1 2 3 4 5 6".
    void textOnPath(const render::PathText& t) override
    {
        pathTexts.push_back(t);
        static constexpr const char* h[] = {"left", "center", "right"};
        using Kind = geometry::PathElement::Kind;
        std::string segments;
        for (const auto& e : t.path.elements()) {
            if (!segments.empty())
                segments += ';';
            switch (e.kind) {
            case Kind::MoveTo: segments += "M " + fmt(e.p[0].x) + ' ' + fmt(e.p[0].y); break;
            case Kind::LineTo: segments += "L " + fmt(e.p[0].x) + ' ' + fmt(e.p[0].y); break;
            case Kind::QuadTo:
                segments += "Q " + fmt(e.p[0].x) + ' ' + fmt(e.p[0].y) + ' ' + fmt(e.p[1].x) + ' ' + fmt(e.p[1].y);
                break;
            case Kind::CubicTo:
                segments += "C " + fmt(e.p[0].x) + ' ' + fmt(e.p[0].y) + ' ' + fmt(e.p[1].x) + ' ' + fmt(e.p[1].y) + ' ' +
                        fmt(e.p[2].x) + ' ' + fmt(e.p[2].y);
                break;
            case Kind::Close: segments += 'Z'; break;
            }
        }
        log(std::string("textOnPath ") + h[static_cast<int>(t.align)] + ' ' + fmt(t.textLength) + ' ' + segments + ' ' +
            t.text);
    }
    void beginGroup(std::string_view id) override { log("beginGroup " + std::string(id)); }
    void endGroup() override { log("endGroup"); }

private:
    void log(std::string line) { ops.push_back(std::move(line)); }
};

} // namespace qraw::test
