// The freehand stroke: simplification, smoothing and the stencil it is stored as
// (commands/freehand.h).

#include "qraw/commands/freehand.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace qraw;
namespace fh = qraw::commands::freehand;
using core::Point;

namespace {

// The distance of `p` to the polyline `line`.
double distanceToLine(const Point& p, const std::vector<Point>& line)
{
    double best = 1e18;
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        const double dx = line[i + 1].x - line[i].x;
        const double dy = line[i + 1].y - line[i].y;
        const double length2 = dx * dx + dy * dy;
        const double t = length2 == 0 ? 0 : std::clamp(((p.x - line[i].x) * dx + (p.y - line[i].y) * dy) / length2, 0.0, 1.0);
        best = std::min(best, std::hypot(p.x - (line[i].x + t * dx), p.y - (line[i].y + t * dy)));
    }
    return best;
}

std::vector<Point> arc(int count)
{
    std::vector<Point> points;
    for (int i = 0; i <= count; ++i) {
        const double a = 3.14159265358979 * i / count;
        points.push_back({100 + 80 * std::cos(a), 200 - 80 * std::sin(a)});
    }
    return points;
}

int countOf(const std::string& text, const std::string& what)
{
    int count = 0;
    for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1))
        ++count;
    return count;
}

} // namespace

class TestFreehand : public QObject {
    Q_OBJECT

private slots:
    void simplifyDropsPointsOnALine()
    {
        const std::vector<Point> line{{0, 0}, {10, 0.2}, {20, -0.2}, {30, 0.1}, {40, 0}};
        const std::vector<Point> kept = fh::simplify(line, 1);
        QCOMPARE(kept.size(), std::size_t{2});
        QCOMPARE(kept.front(), (Point{0, 0}));
        QCOMPARE(kept.back(), (Point{40, 0}));
    }

    void simplifyKeepsACornerAndTheOrder()
    {
        const std::vector<Point> corner{{0, 0}, {20, 0}, {40, 0}, {40, 20}, {40, 40}};
        const std::vector<Point> kept = fh::simplify(corner, 1);
        QCOMPARE(kept, (std::vector<Point>{{0, 0}, {40, 0}, {40, 40}}));
    }

    void simplifyWithoutToleranceOnlyRemovesRepeats()
    {
        const std::vector<Point> points{{0, 0}, {0, 0}, {5, 1}, {5, 1}, {10, 0}};
        QCOMPARE(fh::simplify(points, 0), (std::vector<Point>{{0, 0}, {5, 1}, {10, 0}}));
        QVERIFY(fh::simplify({}, 1).empty());
    }

    void aLongStrokeDoesNotOverflowTheStack()
    {
        std::vector<Point> points;
        for (int i = 0; i < 200000; ++i)
            points.push_back({double(i), std::sin(i / 3000.0) * 100});
        QVERIFY(fh::simplify(points, 0.5).size() > 10);
    }

    void aClickOrAStandStillDrawsNothing()
    {
        QVERIFY(!fh::buildStroke({}, 2));
        const std::vector<Point> one{{5, 5}};
        QVERIFY(!fh::buildStroke(one, 2));
        const std::vector<Point> same{{5, 5}, {5, 5}, {5, 5}};
        QVERIFY(!fh::buildStroke(same, 2));
    }

    void boundsAreTheExtentOfTheKeptPoints()
    {
        const std::vector<Point> points{{30, 40}, {50, 90}, {80, 50}};
        const auto stroke = fh::buildStroke(points, 0);
        QVERIFY(stroke);
        QCOMPARE(stroke->bounds, (core::Rect{30, 40, 50, 50}));
        QVERIFY(stroke->stencilXml.find("w=\"50\" h=\"50\"") != std::string::npos);
        // Coordinates are relative to the bounds: the first point is the origin of the path.
        QVERIFY(stroke->stencilXml.find("<move x=\"0\" y=\"0\"/>") != std::string::npos);
    }

    void aStraightStrokeHasTheMinimumExtent()
    {
        const std::vector<Point> flat{{10, 20}, {60, 20}};
        const auto stroke = fh::buildStroke(flat, 1);
        QVERIFY(stroke);
        QCOMPARE(stroke->bounds, (core::Rect{10, 20, 50, fh::kMinExtent}));
    }

    void theStencilIsOneStrokedQuadraticPathThatScalesWithTheCell()
    {
        const auto stroke = fh::buildStroke(arc(40), 2);
        QVERIFY(stroke);
        const std::string& xml = stroke->stencilXml;
        QVERIFY(xml.starts_with("<shape name=\"Freehand\""));
        QVERIFY(xml.find("strokewidth=\"inherit\"") != std::string::npos);
        QVERIFY(xml.find("aspect=\"variable\"") != std::string::npos);
        QCOMPARE(countOf(xml, "<path>"), 1);
        QCOMPARE(countOf(xml, "<move"), 1);
        QVERIFY(countOf(xml, "<quad") >= 2);
        QCOMPARE(countOf(xml, "<stroke/>"), 1);
        QCOMPARE(countOf(xml, "<fill"), 0); // an open line is never filled
        QVERIFY(xml.ends_with("</path><stroke/></foreground></shape>"));
    }

    void smoothingGivesFewerSegments()
    {
        const auto fine = fh::buildStroke(arc(400), 0.1);
        const auto coarse = fh::buildStroke(arc(400), 5);
        QVERIFY(fine && coarse);
        QVERIFY(countOf(coarse->stencilXml, "<quad") < countOf(fine->stencilXml, "<quad"));
    }

    void theSmoothedLinePassesNearTheInputPoints()
    {
        const std::vector<Point> input = arc(60);
        for (const double smoothing : {0.0, 1.0, 3.0}) {
            const std::vector<Point> line = fh::flattenStroke(input, smoothing);
            QVERIFY(line.size() > 2);
            // The line starts and ends where the pointer did...
            QCOMPARE(line.front(), input.front());
            QVERIFY(std::hypot(line.back().x - input.back().x, line.back().y - input.back().y) < 1e-9);
            // ...and stays within the simplification tolerance of every sample (plus what the
            // rounding of corners adds, which is bounded by half the tolerance again).
            for (const Point& p : input)
                QVERIFY2(distanceToLine(p, line) <= smoothing * 1.5 + 0.5, qPrintable(QString::number(smoothing)));
        }
    }
};

QTEST_APPLESS_MAIN(TestFreehand)
#include "tst_freehand.moc"
