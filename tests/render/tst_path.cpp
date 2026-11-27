// Path builders and rectangle/rotation helpers (src/render/geometry/path.cpp).

#include "qraw/geometry/path.h"

#include <QTest>

#include <cmath>

using namespace qraw::geometry;
using Kind = PathElement::Kind;

namespace {
bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }
} // namespace

class PathTest : public QObject {
    Q_OBJECT

private slots:
    void arcQuarterCircle()
    {
        // Radius 10 from the top (0,0) clockwise to the right (10,10): the
        // circle is centered at (0,10), so the arc is the top-right quadrant.
        Path p;
        p.moveTo({0, 0});
        p.arcTo(10, 10, 0, false, true, {10, 10});

        QCOMPARE(p.elements().size(), std::size_t(2));
        const PathElement& c = p.elements()[1];
        QCOMPARE(c.kind, Kind::CubicTo);
        QVERIFY(near(c.p[2].x, 10, 1e-9) && near(c.p[2].y, 10, 1e-9));
        // Control points stay in the quadrant [0,10] x [0,10], hugging its edges.
        for (int i = 0; i < 2; ++i) {
            QVERIFY(c.p[i].x >= -1e-9 && c.p[i].x <= 10 + 1e-9);
            QVERIFY(c.p[i].y >= -1e-9 && c.p[i].y <= 10 + 1e-9);
        }
        QVERIFY(near(c.p[0].y, 0));  // tangent to the top at the start
        QVERIFY(near(c.p[1].x, 10)); // tangent to the right at the end
    }

    void arcSplitsIntoQuadrants()
    {
        Path p;
        p.moveTo({0, 0});
        p.arcTo(5, 5, 0, true, true, {10, 0}); // a semicircle: two curves
        QCOMPARE(p.elements().size(), std::size_t(3));
        QVERIFY(near(p.currentPoint()->x, 10) && near(p.currentPoint()->y, 0));
        // Radii that are too small are scaled up: still reaches the end point.
        Path q;
        q.moveTo({0, 0});
        q.arcTo(1, 1, 0, false, false, {10, 0});
        QVERIFY(near(q.currentPoint()->x, 10) && near(q.currentPoint()->y, 0));
    }

    void arcDegenerateCases()
    {
        Path zero;
        zero.moveTo({1, 1});
        zero.arcTo(0, 5, 0, false, true, {5, 5});
        QCOMPARE(zero.elements().back().kind, Kind::LineTo);
        QCOMPARE(zero.elements().back().p[0], (Point{5, 5}));

        Path same;
        same.moveTo({3, 3});
        same.arcTo(5, 5, 0, false, true, {3, 3});
        QCOMPARE(same.elements().back().kind, Kind::LineTo);

        Path none; // no current point
        none.arcTo(5, 5, 0, false, true, {7, 8});
        QCOMPARE(none.elements().size(), std::size_t(1));
        QCOMPARE(none.elements()[0].kind, Kind::MoveTo);
        QCOMPARE(none.elements()[0].p[0], (Point{7, 8}));
    }

    void ellipseElements()
    {
        Path p;
        p.addEllipse({0, 0, 20, 10});
        const auto& e = p.elements();
        QCOMPARE(e.size(), std::size_t(6));
        QCOMPARE(e[0].kind, Kind::MoveTo);
        QCOMPARE(e[0].p[0], (Point{20, 5})); // starts at the right
        const Point ends[] = {{10, 10}, {0, 5}, {10, 0}, {20, 5}}; // bottom, left, top, right
        for (std::size_t i = 0; i < 4; ++i) {
            QCOMPARE(e[i + 1].kind, Kind::CubicTo);
            QVERIFY(near(e[i + 1].p[2].x, ends[i].x) && near(e[i + 1].p[2].y, ends[i].y));
        }
        QCOMPARE(e[5].kind, Kind::Close);
        // First control point: kappa * ry below the start.
        QVERIFY(near(e[1].p[0].y, 5 + 5 * 0.5522847498307936));
    }

    void roundedRectElementsAndClamp()
    {
        Path p;
        p.addRoundedRect({0, 0, 100, 50}, 10, 5);
        const auto& e = p.elements();
        const Kind kinds[] = {Kind::MoveTo, Kind::LineTo, Kind::QuadTo, Kind::LineTo, Kind::QuadTo,
                              Kind::LineTo, Kind::QuadTo, Kind::LineTo, Kind::QuadTo, Kind::Close};
        QCOMPARE(e.size(), std::size(kinds));
        for (std::size_t i = 0; i < e.size(); ++i)
            QCOMPARE(e[i].kind, kinds[i]);
        QCOMPARE(e[0].p[0], (Point{10, 0}));
        QCOMPARE(e[1].p[0], (Point{90, 0}));
        QCOMPARE(e[2].p[0], (Point{100, 0}));  // control: the corner
        QCOMPARE(e[2].p[1], (Point{100, 5}));
        QCOMPARE(e[8].p[1], (Point{10, 0}));

        Path clamped;
        clamped.addRoundedRect({0, 0, 100, 50}, 500, -3);
        QCOMPARE(clamped.elements()[0].p[0], (Point{50, 0})); // dx -> w/2
        QCOMPARE(clamped.elements()[2].p[1], (Point{100, 0})); // dy -> 0
    }

    void rectAndBounds()
    {
        Path p;
        QCOMPARE(controlBounds(p), (Rect{0, 0, 0, 0}));
        p.addRect({10, 20, 30, 40});
        QCOMPARE(p.elements().size(), std::size_t(5));
        QCOMPARE(controlBounds(p), (Rect{10, 20, 30, 40}));

        Path q; // control points count
        q.moveTo({0, 0});
        q.quadTo({5, -10}, {10, 0});
        QCOMPARE(controlBounds(q), (Rect{0, -10, 10, 10}));
    }

    void uniteTreatsEmptyRectAsPoint()
    {
        QCOMPARE(unite({10, 10, 5, 5}, {0, 0, 0, 0}), (Rect{0, 0, 15, 15}));
        QCOMPARE(unite({0, 0, 4, 4}, {1, 1, 1, 1}), (Rect{0, 0, 4, 4}));
    }

    void rotation()
    {
        const Point p = rotatePoint({10, 0}, 90, {0, 0});
        QVERIFY(near(p.x, 0) && near(p.y, 10)); // clockwise on screen: right -> down

        const Rect r = rotatedBounds({0, 0, 20, 10}, 90);
        QVERIFY(near(r.x, 5) && near(r.y, -5) && near(r.width, 10) && near(r.height, 20));
        QCOMPARE(rotatedBounds({1, 2, 3, 4}, 0), (Rect{1, 2, 3, 4}));
    }
};

QTEST_APPLESS_MAIN(PathTest)
#include "tst_path.moc"
