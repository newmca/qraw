// Edge markers (src/render/markers.cpp), checked through the recording canvas.
// Expected numbers follow the upstream factories for an edge ending at
// (100, 0) heading right (unit vector (1, 0)), marker size 6, stroke width 1.

#include "qraw/render/markers.h"

#include "support/recording_canvas.h"

#include <QTest>

#include <algorithm>
#include <cmath>

using namespace qraw;
using geometry::Point;

namespace {

bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }

struct Painted {
    std::string ops;
    Point end{100, 0};
    int paints = 0;
    bool created = false;
};

// Creates the marker at (100, 0) heading right and runs its painter.
Painted paint(std::string_view type, bool filled = true, const render::ResolvedStyle& style = {}, bool source = false)
{
    test::RecordingCanvas canvas;
    Painted out;
    const render::MarkerPainter painter =
        render::createMarker(canvas, style, type, out.end, 1, 0, 6, source, 1, filled);
    out.created = static_cast<bool>(painter);
    if (painter)
        painter();
    out.ops = canvas.text();
    out.paints = canvas.paints;
    return out;
}

QString q(const std::string& s) { return QString::fromStdString(s); }

} // namespace

class MarkersTest : public QObject {
    Q_OBJECT

private slots:
    void classicArrow()
    {
        // pt = end - 1.118 * sw = 98.882; the line stops at
        // 100 - (size + sw) * 3/4 - 1.118 = 93.632.
        const Painted p = paint("classic");
        QVERIFY(near(p.end.x, 100 - 7 * 0.75 - 1.118));
        QCOMPARE(p.end.y, 0.0);
        QCOMPARE(q(p.ops), QStringLiteral("begin\nmoveTo 98.882 0\nlineTo 91.882 3.5\nlineTo 93.632 0\n"
                                          "lineTo 91.882 -3.5\nclose\nfillAndStroke\n"));
    }

    void unfilledArrowOnlyStrokes()
    {
        const Painted p = paint("classic", false);
        QVERIFY(p.ops.ends_with("close\nstroke\n"));
        QCOMPARE(p.paints, 1);
        QCOMPARE(p.ops.find("fill"), std::string::npos);
    }

    void blockHasNoNotchAndMovesFurther()
    {
        const Painted p = paint("block");
        QVERIFY(near(p.end.x, 100 - 7 - 1.118));
        QCOMPARE(q(p.ops), QStringLiteral("begin\nmoveTo 98.882 0\nlineTo 91.882 3.5\nlineTo 91.882 -3.5\n"
                                          "close\nfillAndStroke\n"));
        // Thin arrows use width factor 3: 7 / 3 instead of 7 / 2.
        QVERIFY(paint("blockThin").ops.find("lineTo 91.882 2.333") != std::string::npos);
    }

    void openArrowIsNeverFilled()
    {
        const Painted p = paint("open");
        QVERIFY(near(p.end.x, 100 - 2 * 1.118));
        QCOMPARE(q(p.ops), QStringLiteral("begin\nmoveTo 91.882 3.5\nlineTo 98.882 0\nlineTo 91.882 -3.5\nstroke\n"));
        QCOMPARE(paint("open", true).ops.find("fill"), std::string::npos);
    }

    void ovalGeometry()
    {
        // Diameter = size; the line stops at the circle center (radius 3).
        const Painted p = paint("oval");
        QCOMPARE(p.end.x, 97.0);
        QCOMPARE(q(p.ops), QStringLiteral("begin\nellipse 97 -3 6 6\nfillAndStroke\n"));
        QVERIFY(paint("oval", false).ops.ends_with("stroke\n"));
    }

    void diamondGeometry()
    {
        // swFactor 0.7071, thickness factor 2: the tip sits 0.7071 before the end.
        const Painted p = paint("diamond");
        QVERIFY(near(p.end.x, 100 - 7 - 0.7071));
        QCOMPARE(q(p.ops), QStringLiteral("begin\nmoveTo 99.293 0\nlineTo 95.793 3.5\nlineTo 92.293 0\n"
                                          "lineTo 95.793 -3.5\nclose\nfillAndStroke\n"));
    }

    void sourceMarkersMirrorAsync()
    {
        // async draws one wing, on the other side for source markers.
        QVERIFY(paint("async", true, {}, false).ops.find("lineTo 91.882 -3.5") != std::string::npos);
        QVERIFY(paint("async", true, {}, true).ops.find("lineTo 91.882 3.5") != std::string::npos);
    }

    void erMany()
    {
        // nx = 8, ny = 0: a crow's foot at the end point; the line is not shortened.
        const Painted p = paint("ERmany");
        QCOMPARE(p.end.x, 100.0);
        QCOMPARE(q(p.ops), QStringLiteral("begin\nmoveTo 100 -4\nlineTo 92 0\nlineTo 100 4\nstroke\n"));
    }

    void erZeroToOne()
    {
        render::StyleMap map;
        map.set("strokeColor", "#112233");
        const render::ResolvedStyle style(map, true);

        // Filled: a white circle, then the fill color goes back to the stroke color.
        const Painted f = paint("ERzeroToOne", true, style);
        QCOMPARE(f.end.x, 100.0);
        QCOMPARE(q(f.ops), QStringLiteral("begin\nellipse 85 -3 6 6\nsetFillColor #ffffff\nfillAndStroke\n"
                                          "setFillColor #112233\nbegin\nmoveTo 96 4\nlineTo 96 -4\nstroke\n"));

        // Not filled: open circle, a longer line, and the line stops before the circle.
        const Painted o = paint("ERzeroToOne", false, style);
        QVERIFY(near(o.end.x, 100 - (2 * 8 - 0.5)));
        QCOMPARE(q(o.ops), QStringLiteral("begin\nellipse 85 -3 6 6\nstroke\nbegin\nmoveTo 96 4\nlineTo 96 -4\n"
                                          "moveTo 91.5 0\nlineTo 100 0\nstroke\n"));

        // No strokeColor in the style: upstream falls back to #666666.
        QVERIFY(paint("ERzeroToMany", true).ops.find("setFillColor #666666") != std::string::npos);
    }

    void circlePlusPaintsCircleAndCross()
    {
        const Painted p = paint("circlePlus");
        QVERIFY(p.ops.find("ellipse") != std::string::npos);
        QCOMPARE(p.paints, 2); // the circle and the cross
    }

    void everyMarkerPaints()
    {
        for (const std::string_view name : render::markerNames()) {
            const Painted p = paint(name);
            QVERIFY2(p.created, std::string(name).c_str());
            QVERIFY2(p.paints >= 1, std::string(name).c_str());
            QVERIFY2(std::isfinite(p.end.x) && std::isfinite(p.end.y), std::string(name).c_str());
        }
    }

    void unknownMarkers()
    {
        for (const std::string_view name : {"", "none", "bogus", "Classic"}) {
            const Painted p = paint(name);
            QVERIFY2(!p.created, std::string(name).c_str());
            QCOMPARE(p.end, (Point{100, 0}));
            QVERIFY(!render::isKnownMarker(name));
        }
    }

    void registry()
    {
        const auto names = render::markerNames();
        QCOMPARE(names.size(), std::size_t(26));
        QVERIFY(std::is_sorted(names.begin(), names.end()));
        QVERIFY(render::isKnownMarker("classic"));
        QVERIFY(render::isKnownMarker("ERzeroToMany"));
    }
};

QTEST_MAIN(MarkersTest)
#include "tst_markers.moc"
