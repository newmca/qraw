// Polyline helpers (src/render/geometry/polyline.cpp).

#include "qraw/geometry/polyline.h"

#include <QTest>

#include <cmath>
#include <limits>
#include <vector>

using namespace qraw::geometry;
using Kind = PathElement::Kind;

namespace {
bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) <= eps; }
bool near(Point a, Point b, double eps = 1e-6) { return near(a.x, b.x, eps) && near(a.y, b.y, eps); }
} // namespace

class PolylineTest : public QObject {
    Q_OBJECT

private slots:
    void unrounded()
    {
        const std::vector<Point> pts{{0, 0}, {100, 0}, {100, 100}};
        const Path p = polylinePath(pts, false);
        QCOMPARE(p.elements().size(), std::size_t(3));
        QCOMPARE(p.elements()[0].kind, Kind::MoveTo);
        QCOMPARE(p.elements()[1].p[0], (Point{100, 0}));
        QCOMPARE(p.elements()[2].p[0], (Point{100, 100}));
        QVERIFY(polylinePath({}, true).empty());
    }

    void roundedCorner()
    {
        // L shape, arcSize 10: the corner (100,0) becomes lineTo (90,0), a quad
        // with the corner as control point ending at (100,10), then the end.
        const std::vector<Point> pts{{0, 0}, {100, 0}, {100, 100}};
        const Path p = polylinePath(pts, true, 10);
        const auto& e = p.elements();
        QCOMPARE(e.size(), std::size_t(4));
        QCOMPARE(e[1].kind, Kind::LineTo);
        QCOMPARE(e[1].p[0], (Point{90, 0}));
        QCOMPARE(e[2].kind, Kind::QuadTo);
        QCOMPARE(e[2].p[0], (Point{100, 0}));
        QCOMPARE(e[2].p[1], (Point{100, 10}));
        QCOMPARE(e[3].p[0], (Point{100, 100}));
    }

    void roundedCornerCutIsLimitedByHalfSegment()
    {
        // The first segment is 10 long, so the cut is min(10, 10 / 2) = 5.
        const std::vector<Point> pts{{0, 0}, {10, 0}, {10, 100}};
        const Path p = polylinePath(pts, true, 10);
        QCOMPARE(p.elements()[1].p[0], (Point{5, 0}));
        QCOMPARE(p.elements()[2].p[1], (Point{10, 10}));
    }

    void excludedCornerStaysSharp()
    {
        const std::vector<Point> pts{{0, 0}, {100, 0}, {100, 100}};
        const int exclude[] = {0}; // corner i = 1 is excluded as index i - 1
        const Path p = polylinePath(pts, true, 10, false, exclude);
        QCOMPARE(p.elements().size(), std::size_t(3));
        QCOMPARE(p.elements()[1].p[0], (Point{100, 0}));
    }

    void closedRoundedTriangle()
    {
        // Upstream inserts a virtual start in the middle of the closing segment
        // (25,50); each of the three corners is rounded; the path closes.
        const std::vector<Point> pts{{0, 0}, {100, 0}, {50, 100}};
        const Path p = polylinePath(pts, true, 10, true);
        const auto& e = p.elements();
        const Kind kinds[] = {Kind::MoveTo, Kind::LineTo, Kind::QuadTo, Kind::LineTo,
                              Kind::QuadTo, Kind::LineTo, Kind::QuadTo, Kind::Close};
        QCOMPARE(e.size(), std::size(kinds));
        for (std::size_t i = 0; i < e.size(); ++i)
            QCOMPARE(e[i].kind, kinds[i]);
        QCOMPARE(e[0].p[0], (Point{25, 50}));
        QCOMPARE(e[2].p[0], (Point{0, 0}));
        QCOMPARE(e[4].p[0], (Point{100, 0}));
        QCOMPARE(e[6].p[0], (Point{50, 100}));
        QVERIFY(near(e[2].p[1], {10, 0}));
        // First cut: from (25,50) towards (0,0), 10 short of the corner:
        // (0,0) + (25,50) * 10 / |(25,50)|.
        QVERIFY(near(e[1].p[0], {4.472136, 8.944272}, 1e-5));
    }

    void curved()
    {
        const std::vector<Point> pts{{0, 0}, {10, 0}, {10, 10}, {20, 10}};
        const Path p = curvedPath(pts);
        const auto& e = p.elements();
        QCOMPARE(e.size(), std::size_t(3));
        QCOMPARE(e[0].kind, Kind::MoveTo);
        QCOMPARE(e[1].kind, Kind::QuadTo);
        QCOMPARE(e[1].p[0], (Point{10, 0}));
        QCOMPARE(e[1].p[1], (Point{10, 5})); // midpoint of the inner segment
        QCOMPARE(e[2].p[0], (Point{10, 10}));
        QCOMPARE(e[2].p[1], (Point{20, 10}));

        QCOMPARE(curvedPath(std::vector<Point>{{0, 0}, {5, 5}}).elements()[1].kind, Kind::LineTo);
        QVERIFY(curvedPath(std::vector<Point>{{0, 0}}).empty());
    }

    void lengthAndBounds()
    {
        const std::vector<Point> pts{{0, 0}, {30, 40}, {30, 140}};
        QVERIFY(near(polylineLength(pts), 150));
        QCOMPARE(pointsBounds(pts), (Rect{0, 0, 30, 140}));
        QCOMPARE(pointsBounds({}), (Rect{}));
    }

    void relativePoint()
    {
        // Total length 200: segment 0 is (0,0)-(100,0), segment 1 (100,0)-(100,100).
        const std::vector<Point> pts{{0, 0}, {100, 0}, {100, 100}};
        QVERIFY(near(relativePointOnPolyline(pts, -1, 0, std::nullopt), {0, 0}));
        QVERIFY(near(relativePointOnPolyline(pts, 0, 0, std::nullopt), {100, 0}));
        QVERIFY(near(relativePointOnPolyline(pts, 1, 0, std::nullopt), {100, 100}));
        QVERIFY(near(relativePointOnPolyline(pts, 0.5, 0, std::nullopt), {100, 50}));

        // y is orthogonal: on the horizontal segment, positive y is up (-y).
        QVERIFY(near(relativePointOnPolyline(pts, -0.5, 10, std::nullopt), {50, -10}));
        // On the vertical segment, positive y is to the right.
        QVERIFY(near(relativePointOnPolyline(pts, 0.5, 10, std::nullopt), {110, 50}));
        // The offset is added as is (upstream: y - (ny * gy - offset.y)).
        QVERIFY(near(relativePointOnPolyline(pts, 0, 10, Point{5, 7}), {115, 7}));

        QVERIFY(near(relativePointOnPolyline(std::vector<Point>{{3, 4}}, 0, 0, Point{1, 1}), {4, 5}));
        QVERIFY(near(relativePointOnPolyline({}, 0, 0, Point{1, 1}), {1, 1}));
        QVERIFY(near(relativePointOnPolyline({}, 0, 0, std::nullopt), {0, 0}));
    }

    void labelAnchor()
    {
        const std::vector<Point> pts{{0, 0}, {100, 40}};
        // Absolute geometry: midpoint of the end points plus the offset.
        QVERIFY(near(edgeLabelAnchor(pts, false, 0, 0, Point{3, 4}, {-1, -1}), {53, 24}));
        QVERIFY(near(edgeLabelAnchor(pts, true, 0, 0, std::nullopt, {-1, -1}),
                     relativePointOnPolyline(pts, 0, 0, std::nullopt)));
        QVERIFY(near(edgeLabelAnchor({}, true, 0, 0, std::nullopt, {-1, -2}), {-1, -2}));
    }

    void relativeCoordinatesInvertRelativePoint()
    {
        const std::vector<Point> pts{{0, 0}, {100, 0}, {100, 100}};
        for (double x : {-0.8, -0.3, 0.4, 0.9}) {
            for (double y : {-12.0, 0.0, 15.0}) {
                const Point p = relativePointOnPolyline(pts, x, y, std::nullopt);
                const Point rc = relativeCoordinates(pts, p);
                const Point back = relativePointOnPolyline(pts, rc.x, rc.y, std::nullopt);
                QVERIFY2(near(back, p, 1.0), qPrintable(QStringLiteral("x=%1 y=%2").arg(x).arg(y)));
                QVERIFY2(near(rc.y, y, 0.5), qPrintable(QStringLiteral("x=%1 y=%2").arg(x).arg(y)));
            }
        }
        QCOMPARE(relativeCoordinates(std::vector<Point>{{1, 1}}, {5, 5}), (Point{0, 0}));
    }

    void distanceSquared()
    {
        const std::vector<Point> pts{{0, 0}, {100, 0}, {100, 100}};
        QVERIFY(near(distanceSquaredToPolyline(pts, {50, 10}), 100));
        QVERIFY(near(distanceSquaredToPolyline(pts, {110, 50}), 100));
        QVERIFY(near(distanceSquaredToPolyline(pts, {-3, 4}), 25)); // closest to the first point
        QVERIFY(near(distanceSquaredToPolyline(std::vector<Point>{{0, 0}}, {3, 4}), 25));
        QVERIFY(std::isinf(distanceSquaredToPolyline({}, {0, 0})));
    }
};

QTEST_APPLESS_MAIN(PolylineTest)
#include "tst_polyline.moc"
