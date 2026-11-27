// Port of mxgraph/src/shape/mxShape.js (addPoints), mxgraph/src/shape/mxPolyline.js
// (paintCurvedLine), mxgraph/src/view/mxGraphView.js (getPoint, getRelativePoint,
// updateEdgeLabelOffset) and mxgraph/src/util/mxUtils.js (ptSegDistSq,
// relativeCcw) (draw.io, Apache-2.0), modified. All at view scale 1.

#include "qraw/geometry/polyline.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace qraw::geometry {

namespace {

// JavaScript Math.round: halves round towards +infinity.
double jsRound(double v) { return std::floor(v + 0.5); }

// mxUtils.mod
std::size_t wrapIndex(int i, std::size_t n)
{
    const int m = static_cast<int>(n);
    return static_cast<std::size_t>(((i % m) + m) % m);
}

double distance(Point a, Point b) { return std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)); }

// mxCellState.segments: the length of every segment.
std::vector<double> segmentLengths(std::span<const Point> points)
{
    std::vector<double> segments;
    if (points.size() > 1)
        segments.reserve(points.size() - 1);
    for (std::size_t i = 1; i < points.size(); ++i)
        segments.push_back(distance(points[i - 1], points[i]));
    return segments;
}

// mxGraphView.getSegmentLengths: lengths[i] is the distance along the
// polyline from the first point to point i.
std::vector<double> cumulativeLengths(const std::vector<double>& segments)
{
    std::vector<double> lengths{0};
    lengths.reserve(segments.size() + 1);
    for (double s : segments)
        lengths.push_back(lengths.back() + s);
    return lengths;
}

// mxUtils.ptSegDistSq
double ptSegDistSq(double x1, double y1, double x2, double y2, double px, double py)
{
    x2 -= x1;
    y2 -= y1;
    px -= x1;
    py -= y1;

    double dotprod = px * x2 + py * y2;
    double projlenSq = 0;

    if (dotprod > 0) {
        px = x2 - px;
        py = y2 - py;
        dotprod = px * x2 + py * y2;
        if (dotprod > 0)
            projlenSq = dotprod * dotprod / (x2 * x2 + y2 * y2);
    }

    return std::max(0.0, px * px + py * py - projlenSq);
}

// mxUtils.relativeCcw
int relativeCcw(double x1, double y1, double x2, double y2, double px, double py)
{
    x2 -= x1;
    y2 -= y1;
    px -= x1;
    py -= y1;
    double ccw = px * y2 - py * x2;

    if (ccw == 0) {
        ccw = px * x2 + py * y2;
        if (ccw > 0) {
            px -= x2;
            py -= y2;
            ccw = px * x2 + py * y2;
            if (ccw < 0)
                ccw = 0;
        }
    }
    return ccw < 0 ? -1 : (ccw > 0 ? 1 : 0);
}

} // namespace

Path polylinePath(std::span<const Point> points, bool rounded, double arcSize, bool close,
                  std::span<const int> exclude, bool initialMove)
{
    Path path;
    if (points.empty())
        return path;

    std::vector<Point> pts(points.begin(), points.end());
    const Point pe = pts.back();

    // Adds a virtual waypoint in the center between start and end point.
    if (close && rounded) {
        const Point p0 = pts.front();
        pts.insert(pts.begin(), Point{pe.x + (p0.x - pe.x) / 2, pe.y + (p0.y - pe.y) / 2});
    }

    const int count = static_cast<int>(pts.size());
    Point pt = pts[0];
    int i = 1;

    if (initialMove)
        path.moveTo(pt);
    else
        path.lineTo(pt);

    while (i < (close ? count : count - 1)) {
        Point tmp = pts[wrapIndex(i, pts.size())];
        double dx = pt.x - tmp.x;
        double dy = pt.y - tmp.y;
        const bool excluded = std::find(exclude.begin(), exclude.end(), i - 1) != exclude.end();

        if (rounded && (dx != 0 || dy != 0) && !excluded) {
            // A line from the last point to the current point, stopping
            // `arcSize` short of the current point.
            double dist = std::sqrt(dx * dx + dy * dy);
            const double nx1 = dx * std::min(arcSize, dist / 2) / dist;
            const double ny1 = dy * std::min(arcSize, dist / 2) / dist;
            path.lineTo({tmp.x + nx1, tmp.y + ny1});

            // A curve through the current point (control point) to a point
            // `arcSize` away from it towards the next point.
            Point next = pts[wrapIndex(i + 1, pts.size())];

            // Uses the next non-overlapping point.
            while (i < count - 2 && jsRound(next.x - tmp.x) == 0 && jsRound(next.y - tmp.y) == 0) {
                next = pts[wrapIndex(i + 2, pts.size())];
                ++i;
            }

            dx = next.x - tmp.x;
            dy = next.y - tmp.y;
            dist = std::max(1.0, std::sqrt(dx * dx + dy * dy));
            const double nx2 = dx * std::min(arcSize, dist / 2) / dist;
            const double ny2 = dy * std::min(arcSize, dist / 2) / dist;

            const Point end{tmp.x + nx2, tmp.y + ny2};
            path.quadTo(tmp, end);
            tmp = end;
        } else {
            path.lineTo(tmp);
        }

        pt = tmp;
        ++i;
    }

    if (close)
        path.close();
    else
        path.lineTo(pe);
    return path;
}

Path curvedPath(std::span<const Point> points)
{
    Path path;
    const std::size_t n = points.size();
    if (n < 2)
        return path;

    path.moveTo(points[0]);
    if (n == 2) {
        path.lineTo(points[1]);
        return path;
    }

    for (std::size_t i = 1; i + 2 < n; ++i) {
        const Point p0 = points[i];
        const Point p1 = points[i + 1];
        path.quadTo(p0, {(p0.x + p1.x) / 2, (p0.y + p1.y) / 2});
    }
    path.quadTo(points[n - 2], points[n - 1]);
    return path;
}

double polylineLength(std::span<const Point> points)
{
    double length = 0;
    for (double s : segmentLengths(points))
        length += s;
    return length;
}

Point relativePointOnPolyline(std::span<const Point> points, double x, double y, std::optional<Point> offset)
{
    const Point off = offset.value_or(Point{});
    if (points.empty())
        return off;
    if (points.size() == 1)
        return {points[0].x + off.x, points[0].y + off.y};

    const std::vector<double> segments = segmentLengths(points);
    const std::vector<double> lengths = cumulativeLengths(segments);
    const double total = lengths.back();

    const double gx = x / 2;
    const double dist = jsRound((gx + 0.5) * total);

    // The first point whose rounded length from the first point is greater
    // than the distance (binary search as upstream).
    std::size_t index = 1;
    std::size_t hi = points.size() - 1;
    while (index < hi) {
        const std::size_t mid = (index + hi) >> 1;
        if (dist >= jsRound(lengths[mid]))
            index = mid + 1;
        else
            hi = mid;
    }

    const double segment = segments[index - 1];
    const double lengthBefore = lengths[index - 1];
    const double factor = segment == 0 ? 0 : (dist - lengthBefore) / segment;
    const Point p0 = points[index - 1];
    const Point pe = points[index];

    const double dx = pe.x - p0.x;
    const double dy = pe.y - p0.y;
    const double nx = segment == 0 ? 0 : dy / segment;
    const double ny = segment == 0 ? 0 : dx / segment;

    return {p0.x + dx * factor + (nx * y + off.x), p0.y + dy * factor - (ny * y - off.y)};
}

Point edgeLabelAnchor(std::span<const Point> points, bool relative, double x, double y, std::optional<Point> offset,
                      Point fallbackCenter)
{
    if (points.empty())
        return fallbackCenter;
    if (relative)
        return relativePointOnPolyline(points, x, y, offset);

    const Point p0 = points.front();
    const Point pe = points.back();
    const Point off = offset.value_or(Point{});
    return {p0.x + (pe.x - p0.x) / 2 + off.x, p0.y + (pe.y - p0.y) / 2 + off.y};
}

Point relativeCoordinates(std::span<const Point> points, Point p)
{
    if (points.size() < 2)
        return {};

    const std::vector<double> segments = segmentLengths(points);
    const double totalLength = polylineLength(points);

    // Finds the segment closest to the point (later segments win ties).
    Point p0 = points[0];
    Point pe = points[1];
    double minDist = ptSegDistSq(p0.x, p0.y, pe.x, pe.y, p.x, p.y);
    double length = 0;
    std::size_t index = 0;
    double tmp = 0;

    for (std::size_t i = 2; i < points.size(); ++i) {
        p0 = pe;
        pe = points[i];
        const double dist = ptSegDistSq(p0.x, p0.y, pe.x, pe.y, p.x, p.y);
        tmp += segments[i - 2];

        if (dist <= minDist) {
            minDist = dist;
            index = i - 1;
            length = tmp;
        }
    }

    const double seg = segments[index];
    p0 = points[index];
    pe = points[index + 1];

    const double xSegment = p0.x - pe.x;
    const double ySegment = p0.y - pe.y;
    const double px = xSegment - (p.x - pe.x);
    const double py = ySegment - (p.y - pe.y);
    const double dotprod = px * xSegment + py * ySegment;
    const double projlenSq = dotprod <= 0 ? 0 : dotprod * dotprod / (xSegment * xSegment + ySegment * ySegment);

    const double projlen = std::min(std::sqrt(projlenSq), seg);
    double yDistance = std::sqrt(ptSegDistSq(p0.x, p0.y, pe.x, pe.y, p.x, p.y));
    if (relativeCcw(p0.x, p0.y, pe.x, pe.y, p.x, p.y) == -1)
        yDistance = -yDistance;

    return {totalLength > 0 ? ((totalLength / 2 - length - projlen) / totalLength) * -2 : 0, yDistance};
}

double distanceSquaredToPolyline(std::span<const Point> points, Point p)
{
    // Nothing to be close to: infinitely far, so hit tests never match.
    if (points.empty())
        return std::numeric_limits<double>::infinity();
    if (points.size() == 1)
        return (p.x - points[0].x) * (p.x - points[0].x) + (p.y - points[0].y) * (p.y - points[0].y);

    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 1; i < points.size(); ++i)
        best = std::min(best, ptSegDistSq(points[i - 1].x, points[i - 1].y, points[i].x, points[i].y, p.x, p.y));
    return best;
}

Rect pointsBounds(std::span<const Point> points)
{
    if (points.empty())
        return {};
    double minX = points[0].x, maxX = minX, minY = points[0].y, maxY = minY;
    for (const Point& p : points) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    return {minX, minY, maxX - minX, maxY - minY};
}

} // namespace qraw::geometry
