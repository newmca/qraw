// Edge routing (src/render/geometry/routing*.cpp). Expected values are worked
// out by hand from mxGraphView.updateEdgeState and mxEdgeStyle; the comment
// at each test shows the arithmetic. Box A is (0,0,100,50), centre (50,25).

#include "qraw/geometry/routing.h"

#include <QTest>

#include <cmath>
#include <limits>

using namespace qraw::geometry;

namespace {

constexpr double kEps = 1e-6;

Terminal box(double x, double y, double w = 100, double h = 50)
{
    Terminal t;
    t.bounds = Rect{x, y, w, h};
    return t;
}

ConnectionConstraint fixedPoint(double fx, double fy, bool perimeter, double dx = 0, double dy = 0)
{
    ConnectionConstraint c;
    c.point = Point{fx, fy};
    c.perimeter = perimeter;
    c.dx = dx;
    c.dy = dy;
    return c;
}

EdgeRouteRequest edge(Terminal source, Terminal target, EdgeStyleKind style = EdgeStyleKind::Straight)
{
    EdgeRouteRequest r;
    r.source = std::move(source);
    r.target = std::move(target);
    r.style = style;
    return r;
}

bool near(Point p, double x, double y) { return std::abs(p.x - x) < kEps && std::abs(p.y - y) < kEps; }

QString dump(const std::vector<Point>& pts)
{
    QString s;
    for (const Point& p : pts)
        s += QString("(%1,%2) ").arg(p.x).arg(p.y);
    return s;
}

// Compares a route with the expected polyline, point by point.
bool routeIs(const EdgeRoute& route, std::initializer_list<Point> expected)
{
    if (route.points.size() != expected.size()) {
        qWarning("route %s has %zu points, expected %zu", qPrintable(dump(route.points)), route.points.size(),
                 expected.size());
        return false;
    }
    std::size_t i = 0;
    for (const Point& e : expected) {
        if (!near(route.points[i], e.x, e.y)) {
            qWarning("route %s differs at %zu, expected (%g,%g)", qPrintable(dump(route.points)), i, e.x, e.y);
            return false;
        }
        ++i;
    }
    return true;
}

bool onBoundary(Point p, const Rect& r)
{
    const bool inX = p.x > r.x - kEps && p.x < r.x + r.width + kEps;
    const bool inY = p.y > r.y - kEps && p.y < r.y + r.height + kEps;
    const bool onX = std::abs(p.x - r.x) < kEps || std::abs(p.x - (r.x + r.width)) < kEps;
    const bool onY = std::abs(p.y - r.y) < kEps || std::abs(p.y - (r.y + r.height)) < kEps;
    return inX && inY && (onX || onY);
}

bool axisAligned(const std::vector<Point>& pts)
{
    for (std::size_t i = 0; i + 1 < pts.size(); ++i)
        if (std::abs(pts[i].x - pts[i + 1].x) > kEps && std::abs(pts[i].y - pts[i + 1].y) > kEps)
            return false;
    return true;
}

// True when an axis-aligned segment passes through the open interior of r.
bool crossesInterior(Point a, Point b, const Rect& r)
{
    const double x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
    const double y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
    return x1 > r.x + kEps && x0 < r.x + r.width - kEps && y1 > r.y + kEps && y0 < r.y + r.height - kEps;
}

bool avoids(const std::vector<Point>& pts, const Rect& r)
{
    for (std::size_t i = 0; i + 1 < pts.size(); ++i)
        if (crossesInterior(pts[i], pts[i + 1], r))
            return false;
    return true;
}

} // namespace

class RoutingTest : public QObject {
    Q_OBJECT

private slots:
    void parseStyles()
    {
        QCOMPARE(parseEdgeStyle("orthogonalEdgeStyle"), EdgeStyleKind::Orthogonal);
        QCOMPARE(parseEdgeStyle("elbowEdgeStyle"), EdgeStyleKind::Elbow);
        QCOMPARE(parseEdgeStyle("entityRelationEdgeStyle"), EdgeStyleKind::EntityRelation);
        QCOMPARE(parseEdgeStyle("sideToSideEdgeStyle"), EdgeStyleKind::SideToSide);
        QCOMPARE(parseEdgeStyle("topToBottomEdgeStyle"), EdgeStyleKind::TopToBottom);
        QCOMPARE(parseEdgeStyle("segmentEdgeStyle"), EdgeStyleKind::Segment);
        QCOMPARE(parseEdgeStyle("loopEdgeStyle"), EdgeStyleKind::Loop);
        QCOMPARE(parseEdgeStyle("isometricEdgeStyle"), EdgeStyleKind::Straight);
        QCOMPARE(parseEdgeStyle(""), EdgeStyleKind::Straight);
        QVERIFY(isOrthogonalStyle(EdgeStyleKind::Orthogonal));
        QVERIFY(isOrthogonalStyle(EdgeStyleKind::EntityRelation));
        QVERIFY(!isOrthogonalStyle(EdgeStyleKind::Straight));
        QVERIFY(!isOrthogonalStyle(EdgeStyleKind::Loop));
    }

    void straightSideBySide()
    {
        // Centres (50,25) and (250,25): the ray is horizontal, so the ends
        // are the facing side midpoints.
        const auto route = routeEdge(edge(box(0, 0), box(200, 0)));
        QVERIFY(routeIs(route, {{100, 25}, {200, 25}}));
    }

    void straightWithWaypoint()
    {
        // Waypoint (150,100). Source ray from (50,25): (100,75) -> bottom
        // side y=50 at t=1/3, x=50+33.33. Target ray from (250,25) towards
        // (150,100): bottom at y=50, x=250-33.33.
        auto r = edge(box(0, 0), box(200, 0));
        r.waypoints = {{150, 100}};
        const auto route = routeEdge(r);
        QVERIFY(routeIs(route, {{50 + 100.0 / 3, 50}, {150, 100}, {250 - 100.0 / 3, 50}}));
    }

    void constraintsRectangle()
    {
        // exitX=1;exitY=0.5 -> (100,25); entryX=0;entryY=0.5 -> (200,25).
        // Without the perimeter projection the point is the same.
        for (bool perimeter : {true, false}) {
            auto r = edge(box(0, 0), box(200, 0));
            r.source->constraint = fixedPoint(1, 0.5, perimeter);
            r.target->constraint = fixedPoint(0, 0.5, perimeter);
            QVERIFY(routeIs(routeEdge(r), {{100, 25}, {200, 25}}));
        }
    }

    void constraintPerimeterOnEllipse()
    {
        Terminal e = box(0, 0);
        e.perimeter = PerimeterShape{PerimeterKind::Ellipse};
        // Fixed (100,0) is outside the ellipse. exitPerimeter=0 keeps it.
        QVERIFY(near(constraintPoint(e, fixedPoint(1, 0, false)), 100, 0));
        // exitPerimeter=1: ray (50,-25) from the centre hits the ellipse where
        // 2t^2 = 1, t = 1/sqrt2 -> (50+35.355, 25-17.678).
        const double s = 50 / std::sqrt(2.0);
        QVERIFY(near(constraintPoint(e, fixedPoint(1, 0, true)), 50 + s, 25 - s / 2));
        // dx/dy are added after scaling.
        QVERIFY(near(constraintPoint(box(0, 0), fixedPoint(0.5, 0.5, false, 3, -4)), 53, 21));
    }

    void rotatedTerminal()
    {
        // Rotation 90 about (50,25): the outline spans x 25..75, y -25..75.
        // The target is on the right at y=25, so the source meets the
        // rotated right side: (75,25).
        auto r = edge(box(0, 0), box(200, 0));
        r.source->rotation = 90;
        QVERIFY(routeIs(routeEdge(r), {{75, 25}, {200, 25}}));
        // A constraint at the unrotated top midpoint (0.5,0) rotates to the
        // right side as well: (50,0) -> relative (0,-25) -> (25,0) + centre.
        QVERIFY(near(constraintPoint(*r.source, fixedPoint(0.5, 0, false)), 75, 25));
    }

    void ellipseTerminal()
    {
        // B centre (250,125) sees A's centre on its diagonal: corner (200,100).
        // The ellipse end then lies on the ellipse and on the line to it.
        auto r = edge(box(0, 0), box(200, 100));
        r.source->perimeter = PerimeterShape{PerimeterKind::Ellipse};
        const auto route = routeEdge(r);
        QVERIFY(route.valid());
        const Point p = route.points.front();
        QVERIFY(std::abs(std::pow((p.x - 50) / 50, 2) + std::pow((p.y - 25) / 25, 2) - 1) < 1e-9);
        QVERIFY(std::abs((p.x - 50) * 75 - (p.y - 25) * 150) < 1e-6); // collinear with (200,100)
    }

    void perimeterSpacingGrowsGap()
    {
        auto r = edge(box(0, 0), box(200, 0));
        r.source->perimeterSpacing = 10; // bounds become (-10,-10,120,70)
        QVERIFY(routeIs(routeEdge(r), {{110, 25}, {200, 25}}));
    }

    void elbowHorizontalAndVertical()
    {
        // Horizontal elbow (SideToSide): the vertical leg sits mid-gap,
        // x = (100+200)/2 = 150, between the terminal centre heights.
        auto r = edge(box(0, 0), box(200, 100), EdgeStyleKind::Elbow);
        QVERIFY(routeIs(routeEdge(r), {{100, 25}, {150, 25}, {150, 125}, {200, 125}}));
        // Vertical elbow (TopToBottom): y = (50+100)/2 = 75.
        r.elbow = ElbowAxis::Vertical;
        QVERIFY(routeIs(routeEdge(r), {{50, 50}, {50, 75}, {250, 75}, {250, 100}}));
    }

    void entityRelation()
    {
        // Source exits right (target is to the right), target is entered from
        // the left: stubs of `segment` = 30 at both ends.
        auto r = edge(box(0, 0), box(200, 100), EdgeStyleKind::EntityRelation);
        QVERIFY(routeIs(routeEdge(r), {{100, 25}, {130, 25}, {170, 125}, {200, 125}}));
        r.segment = 20;
        const auto route = routeEdge(r);
        QVERIFY(routeIs(route, {{100, 25}, {120, 25}, {180, 125}, {200, 125}}));
    }

    void orthogonalPlacements_data()
    {
        QTest::addColumn<double>("bx");
        QTest::addColumn<double>("by");
        QTest::newRow("right") << 250.0 << 0.0;
        QTest::newRow("below") << 0.0 << 150.0;
        QTest::newRow("overlapping-x") << 50.0 << 150.0;
        QTest::newRow("diagonal") << 250.0 << 150.0;
        QTest::newRow("above-left") << -250.0 << -150.0;
    }

    void orthogonalPlacements()
    {
        QFETCH(double, bx);
        QFETCH(double, by);
        auto r = edge(box(0, 0), box(bx, by), EdgeStyleKind::Orthogonal);
        const auto route = routeEdge(r);
        QVERIFY(route.valid());
        QVERIFY2(axisAligned(route.points), qPrintable(dump(route.points)));
        QVERIFY2(onBoundary(route.points.front(), r.source->bounds), qPrintable(dump(route.points)));
        QVERIFY2(onBoundary(route.points.back(), r.target->bounds), qPrintable(dump(route.points)));
        QVERIFY2(avoids(route.points, r.source->bounds), qPrintable(dump(route.points)));
        QVERIFY2(avoids(route.points, r.target->bounds), qPrintable(dump(route.points)));
    }

    void orthogonalExactRoutes()
    {
        // Diagonal: the L pattern. Source leaves east (100,25), runs to the
        // centre column of the target (300) and drops onto its top (150).
        auto r = edge(box(0, 0), box(250, 150), EdgeStyleKind::Orthogonal);
        QVERIFY(routeIs(routeEdge(r), {{100, 25}, {300, 25}, {300, 150}}));
        // Overlapping in x, target below: the Z pattern. Down from (50,50),
        // across at the middle of the gap (y = 100), down onto (100,150).
        r = edge(box(0, 0), box(50, 150), EdgeStyleKind::Orthogonal);
        QVERIFY(routeIs(routeEdge(r), {{50, 50}, {50, 100}, {100, 100}, {100, 150}}));
    }

    void orthogonalWithWaypoint()
    {
        // A waypoint hint far below both boxes: the route has to pass its
        // row or column and stay orthogonal.
        auto r = edge(box(0, 0), box(250, 0), EdgeStyleKind::Orthogonal);
        r.waypoints = {{175, 200}};
        const auto route = routeEdge(r);
        QVERIFY(route.valid());
        QVERIFY2(axisAligned(route.points), qPrintable(dump(route.points)));
        QVERIFY2(avoids(route.points, r.source->bounds), qPrintable(dump(route.points)));
        QVERIFY2(avoids(route.points, r.target->bounds), qPrintable(dump(route.points)));
        bool reachesHint = false;
        for (const Point& p : route.points)
            reachesHint = reachesHint || std::abs(p.y - 200) < kEps || std::abs(p.x - 175) < kEps;
        QVERIFY2(reachesHint, qPrintable(dump(route.points)));
    }

    void orthogonalPortConstraintNorth()
    {
        // Source must leave through its top side even though the target is
        // below it: the first segment goes up from y = 0.
        auto r = edge(box(0, 0), box(0, 150), EdgeStyleKind::Orthogonal);
        r.source->portConstraint = "north";
        const auto route = routeEdge(r);
        QVERIFY(route.valid());
        QVERIFY2(axisAligned(route.points), qPrintable(dump(route.points)));
        QVERIFY2(std::abs(route.points.front().y) < kEps, qPrintable(dump(route.points)));
        QVERIFY2(route.points[1].y < route.points[0].y, qPrintable(dump(route.points)));
        QVERIFY2(avoids(route.points, r.source->bounds), qPrintable(dump(route.points)));
        QVERIFY2(avoids(route.points, r.target->bounds), qPrintable(dump(route.points)));
    }

    void segmentStyle()
    {
        // Without hints SegmentConnector falls back to an L or Z route that
        // stays axis aligned.
        auto r = edge(box(0, 0), box(250, 150), EdgeStyleKind::Segment);
        const auto route = routeEdge(r);
        QVERIFY(route.valid());
        QVERIFY2(axisAligned(route.points), qPrintable(dump(route.points)));
    }

    void selfLoop()
    {
        auto r = edge(box(0, 0), box(0, 0));
        r.isSelfLoop = true;
        const auto route = routeEdge(r);
        QVERIFY2(route.points.size() >= 3, qPrintable(dump(route.points)));
        QVERIFY(onBoundary(route.points.front(), r.source->bounds));
        QVERIFY(onBoundary(route.points.back(), r.source->bounds));
    }

    void danglingEdge()
    {
        EdgeRouteRequest r;
        r.sourcePoint = Point{0, 0};
        r.targetPoint = Point{100, 100};
        QVERIFY(routeIs(routeEdge(r), {{0, 0}, {100, 100}}));
        r.waypoints = {{0, 100}};
        QVERIFY(routeIs(routeEdge(r), {{0, 0}, {0, 100}, {100, 100}}));
    }

    void danglingTarget()
    {
        // Source attached, target free: the source ray heads for the point.
        EdgeRouteRequest r;
        r.source = box(0, 0);
        r.targetPoint = Point{300, 25};
        QVERIFY(routeIs(routeEdge(r), {{100, 25}, {300, 25}}));
    }

    void edgeToEdgeTerminal()
    {
        // The target is a vertical edge x=200, y 0..200: the end is the
        // closest point of that polyline to the source centre (25,100).
        Terminal edgeTerminal;
        edgeTerminal.bounds = Rect{200, 0, 0, 200};
        edgeTerminal.edgePoints = {{200, 0}, {200, 200}};
        const auto route = routeEdge(edge(box(0, 80, 50, 40), edgeTerminal));
        QVERIFY(routeIs(route, {{50, 100}, {200, 100}}));
    }

    void invalidInputs()
    {
        QVERIFY(!routeEdge(EdgeRouteRequest{}).valid());
        auto r = edge(box(0, 0), box(200, 0));
        r.waypoints = {{std::numeric_limits<double>::quiet_NaN(), 0}};
        QVERIFY(!routeEdge(r).valid());
        r.waypoints.clear();
        r.source->bounds.width = std::numeric_limits<double>::infinity();
        QVERIFY(!routeEdge(r).valid());
    }
};

QTEST_APPLESS_MAIN(RoutingTest)
#include "tst_routing.moc"
