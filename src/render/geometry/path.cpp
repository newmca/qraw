// Port of mxgraph/src/util/mxUtils.js (arcToCurves, getBoundingBox,
// getRotatedPoint) (draw.io, Apache-2.0), modified.

#include "qraw/geometry/path.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace qraw::geometry {

namespace {

constexpr double kKappa = 0.5522847498307936;

double toRadians(double degrees) { return degrees * std::numbers::pi / 180.0; }

// mxUtils.arcToCurves: the arc from (x0, y0) to (x, y) as cubic Béziers,
// 6 numbers per curve (x1 y1 x2 y2 x y). Upstream returns an empty list for
// identical end points and a degenerate curve for a zero radius; Path::arcTo
// draws a line in those cases instead, so only the general case remains here.
std::vector<double> arcToCurves(double x0, double y0, double r1, double r2, double angle, bool largeArcFlag,
                                bool sweepFlag, double x, double y)
{
    x -= x0;
    y -= y0;

    r1 = std::abs(r1);
    r2 = std::abs(r2);
    const double ctx = -x / 2;
    const double cty = -y / 2;
    const double cpsi = std::cos(toRadians(angle));
    const double spsi = std::sin(toRadians(angle));
    const double rxd = cpsi * ctx + spsi * cty;
    const double ryd = -1 * spsi * ctx + cpsi * cty;
    const double rxdd = rxd * rxd;
    const double rydd = ryd * ryd;
    const double r1x = r1 * r1;
    const double r2y = r2 * r2;
    const double lambda = rxdd / r1x + rydd / r2y;
    double sds = 0;

    if (lambda > 1) {
        r1 = std::sqrt(lambda) * r1;
        r2 = std::sqrt(lambda) * r2;
    } else {
        const double seif = (largeArcFlag == sweepFlag) ? -1 : 1;
        // Clamped like upstream: radii that only just span the chord can make
        // the radicand slightly negative through rounding.
        sds = seif * std::sqrt(std::max(0.0, (r1x * r2y - r1x * rydd - r2y * rxdd) / (r1x * rydd + r2y * rxdd)));
    }

    const double txd = sds * r1 * ryd / r2;
    const double tyd = -1 * sds * r2 * rxd / r1;
    const double tx = cpsi * txd - spsi * tyd + x / 2;
    const double ty = spsi * txd + cpsi * tyd + y / 2;
    double rad = std::atan2((ryd - tyd) / r2, (rxd - txd) / r1) - std::atan2(0.0, 1.0);
    double s1 = (rad >= 0) ? rad : 2 * std::numbers::pi + rad;
    rad = std::atan2((-ryd - tyd) / r2, (-rxd - txd) / r1) - std::atan2((ryd - tyd) / r2, (rxd - txd) / r1);
    double dr = (rad >= 0) ? rad : 2 * std::numbers::pi + rad;

    if (!sweepFlag && dr > 0)
        dr -= 2 * std::numbers::pi;
    else if (sweepFlag && dr < 0)
        dr += 2 * std::numbers::pi;

    const double sse = dr * 2 / std::numbers::pi;
    // Upstream uses a bare ceil. A quarter arc (sse == 1) then splits into 1 or
    // 2 curves depending on the platform's atan2 rounding (macOS libm landed one
    // ULP above 1), so outputs differed between platforms. The tolerance makes
    // segment counts platform-independent; it is far below any visible change.
    constexpr double kSegmentEpsilon = 1e-9;
    const double seg = std::max(0.0, std::ceil(std::abs(sse) - kSegmentEpsilon));
    const double segr = dr / seg;
    const double t = 8.0 / 3.0 * std::sin(segr / 4) * std::sin(segr / 4) / std::sin(segr / 2);
    const double cpsir1 = cpsi * r1;
    const double cpsir2 = cpsi * r2;
    const double spsir1 = spsi * r1;
    const double spsir2 = spsi * r2;
    double mc = std::cos(s1);
    double ms = std::sin(s1);
    double x2 = -t * (cpsir1 * ms + spsir2 * mc);
    double y2 = -t * (spsir1 * ms - cpsir2 * mc);

    std::vector<double> result;
    if (!std::isfinite(seg) || seg < 1)
        return result;
    result.reserve(static_cast<std::size_t>(seg) * 6);

    for (int n = 0; n < static_cast<int>(seg); ++n) {
        s1 += segr;
        mc = std::cos(s1);
        ms = std::sin(s1);

        const double x3 = cpsir1 * mc - spsir2 * ms + tx;
        const double y3 = spsir1 * mc + cpsir2 * ms + ty;
        const double dx = -t * (cpsir1 * ms + spsir2 * mc);
        const double dy = -t * (spsir1 * ms - cpsir2 * mc);

        result.push_back(x2 + x0);
        result.push_back(y2 + y0);
        result.push_back(x3 - dx + x0);
        result.push_back(y3 - dy + y0);
        result.push_back(x3 + x0);
        result.push_back(y3 + y0);

        x2 = x3 + dx;
        y2 = y3 + dy;
    }
    return result;
}

} // namespace

void Path::arcTo(double rx, double ry, double xAxisRotation, bool largeArc, bool sweep, Point end)
{
    const std::optional<Point> from = currentPoint();
    if (!from) {
        moveTo(end);
        return;
    }
    // Upstream omits an arc with identical end points and treats a zero
    // radius as a line; SVG draws a line in both cases, and so do we.
    if (rx == 0 || ry == 0 || end == *from) {
        lineTo(end);
        return;
    }

    const std::vector<double> c = arcToCurves(from->x, from->y, rx, ry, xAxisRotation, largeArc, sweep, end.x, end.y);
    if (c.empty()) {
        lineTo(end);
        return;
    }
    for (std::size_t i = 0; i + 5 < c.size(); i += 6)
        cubicTo({c[i], c[i + 1]}, {c[i + 2], c[i + 3]}, {c[i + 4], c[i + 5]});
}

void Path::addRect(const Rect& r)
{
    moveTo({r.x, r.y});
    lineTo({r.x + r.width, r.y});
    lineTo({r.x + r.width, r.y + r.height});
    lineTo({r.x, r.y + r.height});
    close();
}

void Path::addEllipse(const Rect& r)
{
    const double cx = r.x + r.width / 2;
    const double cy = r.y + r.height / 2;
    const double rx = r.width / 2;
    const double ry = r.height / 2;
    const double kx = rx * kKappa;
    const double ky = ry * kKappa;

    moveTo({cx + rx, cy});
    cubicTo({cx + rx, cy + ky}, {cx + kx, cy + ry}, {cx, cy + ry}); // right -> bottom
    cubicTo({cx - kx, cy + ry}, {cx - rx, cy + ky}, {cx - rx, cy}); // bottom -> left
    cubicTo({cx - rx, cy - ky}, {cx - kx, cy - ry}, {cx, cy - ry}); // left -> top
    cubicTo({cx + kx, cy - ry}, {cx + rx, cy - ky}, {cx + rx, cy}); // top -> right
    close();
}

void Path::addRoundedRect(const Rect& r, double dx, double dy)
{
    dx = std::clamp(dx, 0.0, r.width / 2);
    dy = std::clamp(dy, 0.0, r.height / 2);
    const double right = r.x + r.width;
    const double bottom = r.y + r.height;

    moveTo({r.x + dx, r.y});
    lineTo({right - dx, r.y});
    quadTo({right, r.y}, {right, r.y + dy});
    lineTo({right, bottom - dy});
    quadTo({right, bottom}, {right - dx, bottom});
    lineTo({r.x + dx, bottom});
    quadTo({r.x, bottom}, {r.x, bottom - dy});
    lineTo({r.x, r.y + dy});
    quadTo({r.x, r.y}, {r.x + dx, r.y});
    close();
}

Rect controlBounds(const Path& path)
{
    bool any = false;
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    const auto include = [&](Point p) {
        if (!any) {
            minX = maxX = p.x;
            minY = maxY = p.y;
            any = true;
            return;
        }
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    };

    for (const PathElement& e : path.elements()) {
        int count = 0;
        switch (e.kind) {
        case PathElement::Kind::MoveTo:
        case PathElement::Kind::LineTo: count = 1; break;
        case PathElement::Kind::QuadTo: count = 2; break;
        case PathElement::Kind::CubicTo: count = 3; break;
        case PathElement::Kind::Close: break;
        }
        for (int i = 0; i < count; ++i)
            include(e.p[static_cast<std::size_t>(i)]);
    }
    return any ? Rect{minX, minY, maxX - minX, maxY - minY} : Rect{};
}

Rect unite(const Rect& a, const Rect& b)
{
    const double left = std::min(a.x, b.x);
    const double top = std::min(a.y, b.y);
    const double right = std::max(a.x + a.width, b.x + b.width);
    const double bottom = std::max(a.y + a.height, b.y + b.height);
    return {left, top, right - left, bottom - top};
}

Point rotatePoint(Point p, double degrees, Point center)
{
    const double rad = toRadians(degrees);
    const double cos = std::cos(rad);
    const double sin = std::sin(rad);
    const double x = p.x - center.x;
    const double y = p.y - center.y;
    return {x * cos - y * sin + center.x, y * cos + x * sin + center.y};
}

Rect rotatedBounds(const Rect& r, double degrees)
{
    if (degrees == 0)
        return r;

    const Point c{r.x + r.width / 2, r.y + r.height / 2};
    const Point corners[4] = {
        rotatePoint({r.x, r.y}, degrees, c),
        rotatePoint({r.x + r.width, r.y}, degrees, c),
        rotatePoint({r.x + r.width, r.y + r.height}, degrees, c),
        rotatePoint({r.x, r.y + r.height}, degrees, c),
    };
    Rect result{corners[0].x, corners[0].y, 0, 0};
    for (int i = 1; i < 4; ++i)
        result = unite(result, {corners[i].x, corners[i].y, 0, 0});
    return result;
}

} // namespace qraw::geometry
