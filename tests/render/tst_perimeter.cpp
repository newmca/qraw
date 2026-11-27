// Perimeter points (src/render/geometry/perimeter.cpp). Every expected value
// is worked out by hand from the upstream algorithm; the bounds are
// (0,0,100,50) with the center (50,25) unless a test says otherwise.

#include "qraw/geometry/perimeter.h"

#include <QTest>

#include <cmath>

using namespace qraw::geometry;

namespace {

const Rect kBounds{0, 0, 100, 50};

PerimeterShape shapeOf(PerimeterKind kind, Direction direction = Direction::East, double size = -1,
                       bool fixedSize = false)
{
    PerimeterShape s;
    s.kind = kind;
    s.direction = direction;
    s.size = size;
    s.fixedSize = fixedSize;
    return s;
}

Point at(PerimeterKind kind, Point next, bool orthogonal = false, Direction direction = Direction::East,
         double size = -1, bool fixedSize = false)
{
    return perimeterPoint(shapeOf(kind, direction, size, fixedSize), kBounds, next, orthogonal);
}

bool near(Point p, double x, double y, double eps = 1e-6)
{
    return std::abs(p.x - x) < eps && std::abs(p.y - y) < eps;
}

} // namespace

#define VERIFY_NEAR(actual, ex, ey)                                                                               \
    do {                                                                                                         \
        const Point p_ = (actual);                                                                               \
        if (!near(p_, (ex), (ey)))                                                                               \
            QFAIL(qPrintable(QString("got (%1, %2), expected (%3, %4)").arg(p_.x).arg(p_.y).arg(ex).arg(ey))); \
    } while (false)

class PerimeterTest : public QObject {
    Q_OBJECT

private slots:
    void parseNames()
    {
        QCOMPARE(parsePerimeter("rectanglePerimeter"), PerimeterKind::Rectangle);
        QCOMPARE(parsePerimeter("ellipsePerimeter"), PerimeterKind::Ellipse);
        QCOMPARE(parsePerimeter("rhombusPerimeter"), PerimeterKind::Rhombus);
        QCOMPARE(parsePerimeter("trianglePerimeter"), PerimeterKind::Triangle);
        QCOMPARE(parsePerimeter("hexagonPerimeter"), PerimeterKind::Hexagon);
        QCOMPARE(parsePerimeter("hexagonPerimeter2"), PerimeterKind::Hexagon2);
        QCOMPARE(parsePerimeter("parallelogramPerimeter"), PerimeterKind::Parallelogram);
        QCOMPARE(parsePerimeter("trapezoidPerimeter"), PerimeterKind::Trapezoid);
        QCOMPARE(parsePerimeter("stepPerimeter"), PerimeterKind::Step);
        QCOMPARE(parsePerimeter("centerPerimeter"), PerimeterKind::Center);
        // Bare names registered in mxStyleRegistry.
        QCOMPARE(parsePerimeter("ellipse"), PerimeterKind::Ellipse);
        QCOMPARE(parsePerimeter("rhombus"), PerimeterKind::Rhombus);
        // No perimeter, and the documented fallback.
        QVERIFY(!parsePerimeter("").has_value());
        QVERIFY(!parsePerimeter("none").has_value());
        QCOMPARE(parsePerimeter("calloutPerimeter"), PerimeterKind::Rectangle);
        QCOMPARE(parsePerimeter("somethingElse"), PerimeterKind::Rectangle);
    }

    void rectangleSides()
    {
        // The ray from the center (50,25) leaves through the side it faces.
        VERIFY_NEAR(at(PerimeterKind::Rectangle, {200, 25}), 100, 25);
        VERIFY_NEAR(at(PerimeterKind::Rectangle, {-100, 25}), 0, 25);
        VERIFY_NEAR(at(PerimeterKind::Rectangle, {50, -100}), 50, 0);
        VERIFY_NEAR(at(PerimeterKind::Rectangle, {50, 200}), 50, 50);
    }

    void rectangleCorners()
    {
        // Direction (150,100) from the center: steeper than the diagonal
        // (100:50), so it leaves through the bottom at dy = 25 -> dx = 37.5.
        VERIFY_NEAR(at(PerimeterKind::Rectangle, {200, 125}), 87.5, 50);
        // Direction (-150,-100): top at dy = -25 -> dx = -37.5.
        VERIFY_NEAR(at(PerimeterKind::Rectangle, {-100, -75}), 12.5, 0);
        // Exactly on the diagonal (100,50) hits the corner.
        VERIFY_NEAR(at(PerimeterKind::Rectangle, {150, 75}), 100, 50);
    }

    void orthogonalFlagProjects()
    {
        // next.x inside the x extent: straight down/up.
        VERIFY_NEAR(at(PerimeterKind::Rectangle, {70, 200}, true), 70, 50);
        // next.y inside the y extent: straight across.
        VERIFY_NEAR(at(PerimeterKind::Rectangle, {300, 30}, true), 100, 30);
        // Ellipse at y = 10: ty = -15, tx = sqrt(50^2 * (1 - 15^2/25^2)) = 40.
        VERIFY_NEAR(at(PerimeterKind::Ellipse, {150, 10}, true), 90, 10);
        // Rhombus: the vertical x = 70 meets the lower right edge
        // (50,50)-(100,25) at y = 50 - 20/2 = 40.
        VERIFY_NEAR(at(PerimeterKind::Rhombus, {70, 200}, true), 70, 40);
    }

    void ellipseOnTheEquation()
    {
        const Point p = at(PerimeterKind::Ellipse, {150, 75});
        const double nx = (p.x - 50) / 50;
        const double ny = (p.y - 25) / 25;
        QVERIFY(std::abs(nx * nx + ny * ny - 1) < 1e-9);
        // On the ray towards (150,75): same direction as (100,50).
        QVERIFY(std::abs((p.x - 50) * 50 - (p.y - 25) * 100) < 1e-9);

        VERIFY_NEAR(at(PerimeterKind::Ellipse, {150, 25}), 100, 25);
        VERIFY_NEAR(at(PerimeterKind::Ellipse, {50, -100}), 50, 0); // dx == 0 branch
        VERIFY_NEAR(at(PerimeterKind::Ellipse, {50, 25}), 50, 25);  // next == center
    }

    void rhombusEdges()
    {
        // Ray towards (150,75): 25 + (x-50)/2 == 50 - (x-50)/2 -> x = 75, y = 37.5.
        const Point p = at(PerimeterKind::Rhombus, {150, 75});
        VERIFY_NEAR(p, 75, 37.5);
        QVERIFY(std::abs(std::abs(p.x - 50) / 50 + std::abs(p.y - 25) / 25 - 1) < 1e-9);
        // The special cases return the corners.
        VERIFY_NEAR(at(PerimeterKind::Rhombus, {50, -100}), 50, 0);
        VERIFY_NEAR(at(PerimeterKind::Rhombus, {-100, 25}), 0, 25);
    }

    void triangleEastAndNorth()
    {
        // East (default): base on the left, apex (100,25).
        VERIFY_NEAR(at(PerimeterKind::Triangle, {-100, 25}), 0, 25);   // base
        VERIFY_NEAR(at(PerimeterKind::Triangle, {200, 25}), 100, 25);  // apex
        // Towards (100,100): meets the lower slanted side (100,25)-(0,50) at
        // s = 1/7 along (50,75): (57.142857, 35.714286), i.e. y = 25 + (100-x)/4.
        const Point p = at(PerimeterKind::Triangle, {100, 100});
        VERIFY_NEAR(p, 400.0 / 7, 250.0 / 7);
        QVERIFY(std::abs(p.y - (25 + (100 - p.x) / 4)) < 1e-9);

        // North: apex (50,0), base along the bottom.
        VERIFY_NEAR(at(PerimeterKind::Triangle, {50, 200}, false, Direction::North), 50, 50);
        VERIFY_NEAR(at(PerimeterKind::Triangle, {50, -100}, false, Direction::North), 50, 0);
    }

    void hexagonAndHexagon2OnSlantedEdges()
    {
        // Hexagon (25,0),(75,0),(100,25),(75,50),(25,50),(0,25). The ray from
        // the center (50,25) towards (150,-25) leaves through the upper right
        // edge (75,0)-(100,25) (y = x - 75) at s = 1/3: (83.33.., 8.33..).
        const Point classic = at(PerimeterKind::Hexagon, {150, -25});
        VERIFY_NEAR(classic, 250.0 / 3, 25.0 / 3);
        QVERIFY(std::abs(classic.y - (classic.x - 75)) < 1e-9);

        // hexagonPerimeter2 with size 0.25 (dx = 25) is the same outline.
        const Point two = at(PerimeterKind::Hexagon2, {150, -25}, false, Direction::East, 0.25);
        VERIFY_NEAR(two, 250.0 / 3, 25.0 / 3);
        // The default size is 0.25 too.
        VERIFY_NEAR(at(PerimeterKind::Hexagon2, {150, -25}), 250.0 / 3, 25.0 / 3);

        VERIFY_NEAR(at(PerimeterKind::Hexagon, {200, 25}), 100, 25);
        VERIFY_NEAR(at(PerimeterKind::Hexagon2, {200, 25}), 100, 25);
        VERIFY_NEAR(at(PerimeterKind::Hexagon2, {-100, 25}), 0, 25);

        // Orthogonal: the vertical x = 90 from the center row meets the edge
        // (75,0)-(100,25) at y = 90 - 75 = 15.
        VERIFY_NEAR(at(PerimeterKind::Hexagon2, {90, -100}, true), 90, 15);
    }

    void parallelogramTrapezoidStep()
    {
        // Parallelogram, size 0.2: dx = 20; right edge (100,0)-(80,50) -> x = 90 at y = 25.
        VERIFY_NEAR(at(PerimeterKind::Parallelogram, {200, 25}), 90, 25);
        // fixedSize with size = 10: right edge (100,0)-(90,50) -> x = 95.
        VERIFY_NEAR(at(PerimeterKind::Parallelogram, {200, 25}, false, Direction::East, 10, true), 95, 25);
        // fixedSize without size uses 20 (same as the relative 0.2 here).
        VERIFY_NEAR(at(PerimeterKind::Parallelogram, {200, 25}, false, Direction::East, -1, true), 90, 25);

        // Trapezoid, size 0.2: right edge (80,0)-(100,50) -> x = 90 at y = 25.
        VERIFY_NEAR(at(PerimeterKind::Trapezoid, {200, 25}), 90, 25);
        VERIFY_NEAR(at(PerimeterKind::Trapezoid, {200, 25}, false, Direction::East, 10, true), 95, 25);
        VERIFY_NEAR(at(PerimeterKind::Trapezoid, {50, -100}), 50, 0);

        // Step, size 0.2 (dx = 20): tip (100,25) and the notch (20,25).
        VERIFY_NEAR(at(PerimeterKind::Step, {200, 25}), 100, 25);
        VERIFY_NEAR(at(PerimeterKind::Step, {-100, 25}), 20, 25);
        VERIFY_NEAR(at(PerimeterKind::Step, {-100, 25}, false, Direction::East, 10, true), 10, 25);
    }

    void centerAndDegenerateBounds()
    {
        VERIFY_NEAR(at(PerimeterKind::Center, {500, 500}), 50, 25);
        const Point p = perimeterPoint(shapeOf(PerimeterKind::Ellipse), {10, 10, 0, 0}, {50, 50}, false);
        VERIFY_NEAR(p, 10, 10);
        const double nan = std::nan("");
        VERIFY_NEAR(at(PerimeterKind::Rectangle, {nan, 3}), 50, 25);
    }
};

QTEST_APPLESS_MAIN(PerimeterTest)
#include "tst_perimeter.moc"
