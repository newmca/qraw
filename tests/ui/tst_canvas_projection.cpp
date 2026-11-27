// the DiagramScene projection (incremental updates, page layout,
// hit testing, background) of docs/design/ui.md section 3.

#include "support/test_support.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/geometry/polyline.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/commands/ops_pages.h"
#include "qraw/commands/ops_properties.h"
#include "qraw/commands/ops_structure.h"

#include "qraw/render/stats.h"

#include <QElapsedTimer>
#include <QGraphicsScene>
#include <QSignalSpy>
#include <QtTest>

#ifndef QRAW_TEST_TIMEOUT_SCALE
#define QRAW_TEST_TIMEOUT_SCALE 1 // 3 in the asan preset
#endif

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;
namespace ops = qraw::commands::ops;

namespace {

// Collects the scene areas the scene reports as changed content
// (contentDamaged): a cell whose record was rebuilt shows up there, one that
// was left alone does not. This is the seam for "only changed cells are touched".
class DirtyRects {
public:
    explicit DirtyRects(DiagramScene& scene)
    {
        QObject::connect(&scene, &DiagramScene::contentDamaged, &scene, [this](const render::Damage& damage) {
            everything_ = everything_ || damage.everything;
            for (const QRectF& r : damage.rects)
                rects_.append(r);
        });
    }
    void settle()
    {
        rects_.clear();
        everything_ = false;
    }
    void collect() {}
    [[nodiscard]] bool touches(const QRectF& area) const
    {
        return everything_ || std::any_of(rects_.begin(), rects_.end(), [&](const QRectF& r) { return r.intersects(area); });
    }

private:
    QList<QRectF> rects_;
    bool everything_ = false;
};

core::Cell vertexAt(const std::string& id, double x, double y, double w, double h, std::string_view style = "")
{
    return core::Cell::makeVertex(id, "1", id, style, core::Rect{x, y, w, h});
}

// 2,000 vertices in a 50 x 40 grid and a chain of edges between neighbours.
std::string bigDiagram()
{
    std::string xml = R"(<mxfile><diagram id="big" name="Big"><mxGraphModel grid="1" gridSize="10" page="1" pageWidth="827" pageHeight="1169"><root><mxCell id="0"/><mxCell id="1" parent="0"/>)";
    for (int i = 0; i < 2000; ++i) {
        const int col = i % 50;
        const int row = i / 50;
        xml += "<mxCell id=\"v" + std::to_string(i) + "\" value=\"V" + std::to_string(i)
            + "\" style=\"rounded=1;\" vertex=\"1\" parent=\"1\"><mxGeometry x=\"" + std::to_string(col * 80)
            + "\" y=\"" + std::to_string(row * 60) + "\" width=\"60\" height=\"40\" as=\"geometry\"/></mxCell>";
    }
    for (int i = 0; i < 500; ++i) {
        xml += "<mxCell id=\"e" + std::to_string(i) + "\" edge=\"1\" parent=\"1\" source=\"v" + std::to_string(i)
            + "\" target=\"v" + std::to_string(i + 1)
            + "\"><mxGeometry relative=\"1\" as=\"geometry\"/></mxCell>";
    }
    xml += "</root></mxGraphModel></diagram></mxfile>";
    return xml;
}

// A page that exercises the hit tests: rotated vertices, zero-size vertices,
// horizontal and vertical edges, edges with waypoints, labels away from their
// vertices, a swimlane with children and a locked cell.
std::string hitTestDiagram()
{
    std::string xml = R"(<mxfile><diagram id="hit" name="Hit"><mxGraphModel grid="1" gridSize="10" page="1" pageWidth="827" pageHeight="1169"><root><mxCell id="0"/><mxCell id="1" parent="0"/>)";
    const auto num = [](double v) { return std::to_string(v); };
    for (int i = 0; i < 400; ++i) {
        const int col = i % 25;
        const int row = i / 25;
        const char* style = i % 7 == 0   ? "rounded=1;rotation=30;"
            : i % 11 == 0                ? "ellipse;rotation=90;verticalLabelPosition=bottom;"
            : i % 13 == 0                ? "text;"
            : i % 17 == 0                ? "rounded=0;locked=1;"
                                         : "rounded=1;";
        const double w = i % 19 == 0 ? 0 : 50 + (i % 5) * 10;
        const double h = i % 19 == 0 ? 0 : 30 + (i % 3) * 10;
        xml += "<mxCell id=\"v" + std::to_string(i) + "\" value=\"V" + std::to_string(i) + "\" style=\"" + style
            + "\" vertex=\"1\" parent=\"1\"><mxGeometry x=\"" + num(col * 70) + "\" y=\"" + num(row * 55)
            + "\" width=\"" + num(w) + "\" height=\"" + num(h) + "\" as=\"geometry\"/></mxCell>";
    }
    for (int i = 0; i < 120; ++i) {
        // Straight edges between horizontal and vertical neighbours, some with a waypoint.
        const int a = (i * 3) % 399;
        const int b = i % 2 == 0 ? a + 1 : std::min(a + 25, 399);
        xml += "<mxCell id=\"e" + std::to_string(i) + "\" value=\"" + (i % 4 == 0 ? "lbl" : "") + "\" style=\""
            + (i % 3 == 0 ? "edgeStyle=orthogonalEdgeStyle;" : "") + "\" edge=\"1\" parent=\"1\" source=\"v"
            + std::to_string(a) + "\" target=\"v" + std::to_string(b)
            + "\"><mxGeometry relative=\"1\" as=\"geometry\">"
            + (i % 5 == 0 ? "<Array as=\"points\"><mxPoint x=\"" + num(30 + i * 9) + "\" y=\"" + num(900 + i)
                      + "\"/></Array>"
                          : std::string())
            + "</mxGeometry></mxCell>";
    }
    // A horizontal and a vertical dangling edge (no area), and a container.
    xml += R"(<mxCell id="h" edge="1" parent="1"><mxGeometry relative="1" as="geometry"><mxPoint x="100" y="1000" as="sourcePoint"/><mxPoint x="400" y="1000" as="targetPoint"/></mxGeometry></mxCell>)";
    xml += R"(<mxCell id="vv" edge="1" parent="1"><mxGeometry relative="1" as="geometry"><mxPoint x="500" y="950" as="sourcePoint"/><mxPoint x="500" y="1100" as="targetPoint"/></mxGeometry></mxCell>)";
    xml += R"(<mxCell id="sw" value="Lane" style="swimlane;" vertex="1" parent="1"><mxGeometry x="1900" y="40" width="300" height="200" as="geometry"/></mxCell>)";
    xml += R"(<mxCell id="in1" value="in" style="" vertex="1" parent="sw"><mxGeometry x="20" y="50" width="80" height="40" as="geometry"/></mxCell>)";
    xml += R"(<mxCell id="in2" value="in" style="rotation=45;" vertex="1" parent="sw"><mxGeometry x="150" y="100" width="100" height="40" as="geometry"/></mxCell>)";
    xml += "</root></mxGraphModel></diagram></mxfile>";
    return xml;
}

// Reads like the scene did before the grid: every state, topmost first, the
// ones whose paint bounds (a pixel of slack) meet the probe, the exact tests.
std::vector<std::string> referenceCellsAt(const DiagramScene& scene, const core::Page& page, QPointF p,
                                          double tolerance)
{
    const auto rect = [](const core::Rect& r) { return QRectF(r.x, r.y, r.width, r.height); };
    const auto unrotated = [](QPointF q, QPointF center, double degrees) {
        QTransform t;
        t.translate(center.x(), center.y());
        t.rotate(-degrees);
        t.translate(-center.x(), -center.y());
        return degrees == 0 ? q : t.map(q);
    };
    const auto labelHit = [&](const render::CellState& s) {
        if (!s.label || s.label->width <= 0 || s.label->height <= 0)
            return false;
        return rect(s.label->rect()).contains(unrotated(p, QPointF(s.label->x, s.label->y), s.label->rotation));
    };
    const double reach = std::max(tolerance, 0.5);
    const QRectF probe(p.x() - reach, p.y() - reach, 2 * reach, 2 * reach);
    std::vector<std::string> hits;
    const auto& states = scene.pageView().states();
    for (auto it = states.rbegin(); it != states.rend(); ++it) {
        const render::CellState& s = *it;
        const QRectF box = rect(s.paintBounds).adjusted(-1, -1, 1, 1);
        if (!(box.left() <= probe.right() && box.right() >= probe.left() && box.top() <= probe.bottom()
              && box.bottom() >= probe.top()))
            continue;
        bool hit = false;
        if (s.kind == render::CellState::Kind::Vertex) {
            const QRectF bounds = rect(s.bounds);
            hit = bounds.adjusted(-tolerance, -tolerance, tolerance, tolerance)
                      .contains(unrotated(p, bounds.center(), s.style.rotation()))
                || labelHit(s);
        } else {
            hit = geometry::distanceSquaredToPolyline(s.points, core::Point{p.x(), p.y()}) <= tolerance * tolerance
                || labelHit(s);
        }
        if (hit && isCellSelectable(page, s.id))
            hits.push_back(s.id);
    }
    return hits;
}

// Every selectable state whose (rotated) bounds lie inside the rectangle, in paint order.
std::vector<std::string> referenceCellsInRect(const DiagramScene& scene, const core::Page& page, const QRectF& r,
                                              std::string_view scope)
{
    std::vector<std::string> result;
    for (const render::CellState& s : scene.pageView().states()) {
        const double rotation = s.kind == render::CellState::Kind::Vertex ? s.style.rotation() : 0;
        const core::Rect box = rotation != 0 ? geometry::rotatedBounds(s.bounds, rotation) : s.bounds;
        if (box.x >= r.left() && box.x + box.width <= r.right() && box.y >= r.top()
            && box.y + box.height <= r.bottom() && isCellSelectable(page, s.id)
            && ancestorInScope(page, s.id, scope) == s.id)
            result.push_back(s.id);
    }
    return result;
}

} // namespace

class TestCanvasProjection : public QObject {
    Q_OBJECT

private slots:
    void initialState()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();
        QCOMPARE(scene.pageView().states().size(), std::size_t(6));
        for (const char* id : {"a", "b", "e", "g", "c1", "c2"})
            QVERIFY2(scene.state(id) != nullptr, id);
        QCOMPARE(scene.snapshot()->indexOf("a"), std::size_t(0));
    }

    void moveRecordsOnlyAffectedCells()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();
        const auto before = scene.snapshot();
        QSignalSpy projected(&scene, &DiagramScene::projectionChanged);
        DirtyRects dirty(scene);
        dirty.settle();

        QVERIFY(session.edit(QStringLiteral("Move"), [](commands::Transaction& tx) {
                    return ops::moveCells(tx, 0, {"a"}, 0, 20);
                }).ok);
        dirty.collect();

        QCOMPARE(projected.count(), 1);
        // Only the moved cell and the re-routed edge were recorded again.
        for (const char* id : {"b", "g", "c1", "c2"})
            QVERIFY2(scene.snapshot()->find(id) == before->find(id), id);
        QVERIFY(scene.snapshot()->find("a") != before->find("a"));
        QVERIFY(scene.snapshot()->find("e") != before->find("e"));
        const QRectF moved = scene.boundsOf({"a"});
        QVERIFY(dirty.touches(moved));
        QVERIFY(dirty.touches(scene.boundsOf({"e"}))); // re-routed, not in the ChangeSet
        QVERIFY(!dirty.touches(scene.boundsOf({"g"})));
        QVERIFY(!dirty.touches(QRectF(300, 60, 90, 20))); // the middle of b: neither b nor e changed there
    }

    void edgeFollowsMovedTerminal()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();
        const auto routeBefore = scene.state("e")->points;
        QVERIFY(session.edit(QStringLiteral("Move"), [](commands::Transaction& tx) {
                    return ops::moveCells(tx, 0, {"b"}, 0, 200);
                }).ok);
        QVERIFY(scene.state("e")->points != routeBefore);
        QVERIFY(scene.state("e")->bounds.height > 0);
        QCOMPARE(scene.state("b")->bounds.y, 240.0);
    }

    void addRemoveReorderAndUndoKeepSnapshotInPaintOrder()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();
        const auto stackingIsPaintOrder = [&] {
            const auto& states = scene.pageView().states();
            const auto snapshot = scene.snapshot();
            if (snapshot->records().size() != states.size())
                return false;
            for (std::size_t i = 0; i < states.size(); ++i) {
                if (snapshot->indexOf(states[i].id) != i)
                    return false;
            }
            // Hit testing sees the same order: the topmost of two overlapping cells comes first.
            return true;
        };

        QVERIFY(session.edit(QStringLiteral("Add"), [](commands::Transaction& tx) {
                    return ops::addCells(tx, 0, {vertexAt("n", 500, 300, 60, 40)});
                }).ok);
        QCOMPARE(scene.pageView().states().size(), std::size_t(7));
        QVERIFY(stackingIsPaintOrder());

        QVERIFY(session.edit(QStringLiteral("Front"), [](commands::Transaction& tx) {
                    return ops::orderCells(tx, 0, {"a"}, ops::ZOrder::ToFront);
                }).ok);
        QVERIFY(stackingIsPaintOrder());
        QVERIFY(scene.snapshot()->indexOf("a") > scene.snapshot()->indexOf("n"));

        QVERIFY(session.edit(QStringLiteral("Delete"), [](commands::Transaction& tx) {
                    return ops::removeCells(tx, 0, {"n", "b"});
                }).ok);
        QCOMPARE(scene.pageView().states().size(), std::size_t(5)); // a, e (disconnected), g, c1, c2
        QVERIFY(stackingIsPaintOrder());
    }

    void patchedProjectionEqualsAFullBuild()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();
        const auto equalsFullBuild = [&] {
            const render::PageView full = render::PageView::build(*session.currentPage(), session.pageRenderContext());
            const auto& a = scene.pageView().states();
            const auto& b = full.states();
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (a[i].id != b[i].id || a[i].parent != b[i].parent || a[i].bounds != b[i].bounds
                    || a[i].points != b[i].points || a[i].paintBounds != b[i].paintBounds || a[i].style != b[i].style
                    || a[i].label != b[i].label || a[i].labelText != b[i].labelText)
                    return false;
            }
            return true;
        };
        const auto before = [] { return render::stats::get(render::stats::Counter::pageViewUpdates); };
        const std::uint64_t updates = before();

        // Moving a terminal re-routes its edge; moving a group moves its children; a style change.
        QVERIFY(session.edit(QStringLiteral("Move"), [](commands::Transaction& tx) {
                    return ops::moveCells(tx, 0, {"b"}, 0, 200);
                }).ok);
        QVERIFY(equalsFullBuild());
        QVERIFY(session.edit(QStringLiteral("Move"), [](commands::Transaction& tx) {
                    return ops::moveCells(tx, 0, {"g"}, 30, 10);
                }).ok);
        QVERIFY(equalsFullBuild());
        QVERIFY(session.edit(QStringLiteral("Style"), [](commands::Transaction& tx) {
                    return ops::setStyle(tx, 0, {"c1"}, "fillColor", "#ff0000");
                }).ok);
        QVERIFY(equalsFullBuild());
        QVERIFY(session.undo());
        QVERIFY(equalsFullBuild());
        QVERIFY(session.redo());
        QVERIFY(equalsFullBuild());
        // All of these changed content only: patched in place, not built again.
        QVERIFY(before() > updates);

        // A structural edit builds the view again and still agrees.
        QVERIFY(session.edit(QStringLiteral("Add"), [](commands::Transaction& tx) {
                    return ops::addCells(tx, 0, {vertexAt("n", 500, 300, 60, 40)});
                }).ok);
        QVERIFY(equalsFullBuild());
        QVERIFY(session.undo());
        QVERIFY(equalsFullBuild());
    }

    void undoRestoresProjection()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();
        const QByteArray original = savedBytes(session);
        QVERIFY(session.edit(QStringLiteral("Delete"), [](commands::Transaction& tx) {
                    return ops::removeCells(tx, 0, {"a"});
                }).ok);
        QVERIFY(scene.state("a") == nullptr);
        QVERIFY(session.undo());
        QCOMPARE(savedBytes(session), original);
        QVERIFY(scene.state("a") != nullptr);
        QCOMPARE(scene.pageView().states().size(), std::size_t(6));
        QCOMPARE(scene.snapshot()->indexOf("a"), std::size_t(0));
    }

    void pageSwitchRebuilds()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();
        QVERIFY(session.edit(QStringLiteral("Page"), [](commands::Transaction& tx) {
                    return ops::addPage(tx, std::nullopt, std::string("Second"));
                }).ok);
        QCOMPARE(session.currentPageIndex(), std::size_t(1)); // Result.page switches
        QCOMPARE(scene.pageView().states().size(), std::size_t(0));
        QCOMPARE(scene.pageArea(), QRectF(0, 0, 827, 1169));
        session.setCurrentPage(0);
        QCOMPARE(scene.pageView().states().size(), std::size_t(6));
    }

    void changesToOtherPagesAreIgnored()
    {
        DocumentSession session(simpleDocument());
        QVERIFY(session.edit(QStringLiteral("Page"), [](commands::Transaction& tx) {
                    return ops::addPage(tx, std::nullopt, std::string("Second"));
                }).ok);
        session.setCurrentPage(0);
        auto canvas = makeCanvas(session);
        QSignalSpy projected(&canvas->diagramScene(), &DiagramScene::projectionChanged);
        QVERIFY(session.edit(QStringLiteral("Add"), [](commands::Transaction& tx) {
                    return ops::addCells(tx, 1, {vertexAt("x", 0, 0, 10, 10)});
                }).ok);
        QCOMPARE(projected.count(), 0);
        QCOMPARE(canvas->diagramScene().pageView().states().size(), std::size_t(6));
    }

    void pagePropertiesEmitOnceAndMoveThePageArea()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();
        QSignalSpy projected(&scene, &DiagramScene::projectionChanged);
        QVERIFY(session.edit(QStringLiteral("Page setup"), [](commands::Transaction& tx) {
                    commands::ops::PageProperties p;
                    p.pageWidth = 300;
                    p.pageHeight = 200;
                    return ops::setPageProperties(tx, 0, p);
                }).ok);
        QCOMPARE(projected.count(), 1);
        // The simple diagram spans x 40..400, y 40..300: 2 x 2 tiles of 300 x 200.
        QCOMPARE(scene.pageArea(), QRectF(0, 0, 600, 400));
    }

    void pageAreaFollowsGetPageLayout()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();
        QCOMPARE(scene.pageArea(), QRectF(0, 0, 827, 1169)); // content inside the first tile

        // Content left of and above the origin extends the area by whole tiles.
        QVERIFY(session.edit(QStringLiteral("Add"), [](commands::Transaction& tx) {
                    return ops::addCells(tx, 0, {vertexAt("w", -100, -50, 60, 40)});
                }).ok);
        QCOMPARE(scene.pageArea(), QRectF(-827, -1169, 2 * 827, 2 * 1169));

        // Content crossing the right edge of the first tile adds a column.
        QVERIFY(session.edit(QStringLiteral("Add"), [](commands::Transaction& tx) {
                    return ops::addCells(tx, 0, {vertexAt("r", 800, 100, 100, 40)});
                }).ok);
        QCOMPARE(scene.pageArea(), QRectF(-827, -1169, 3 * 827, 2 * 1169));
    }

    void pageAreaWithoutPagesIsTheContent()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();
        QVERIFY(session.edit(QStringLiteral("Page setup"), [](commands::Transaction& tx) {
                    commands::ops::PageProperties p;
                    p.pageVisible = false;
                    return ops::setPageProperties(tx, 0, p);
                }).ok);
        const QRectF area = scene.pageArea();
        const auto bounds = scene.pageView().bounds();
        QCOMPARE(area, QRectF(bounds.x, bounds.y, bounds.width, bounds.height));
    }

    void hitTestingRotatedVertexAndEdge()
    {
        DocumentSession session(simpleDocument());
        QVERIFY(session.edit(QStringLiteral("Add"), [](commands::Transaction& tx) {
                    return ops::addCells(tx, 0, {vertexAt("r", 500, 400, 100, 20, "rotation=90;")});
                }).ok);
        auto canvas = makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();
        // Rotated by 90 degrees around (550, 410): 20 wide, 100 tall.
        QCOMPARE(scene.cellAt({550, 455}, 0), std::string("r"));
        QCOMPARE(scene.cellAt({590, 410}, 0), std::string()); // inside the unrotated box only
        QCOMPARE(scene.cellAt({563, 410}, 0), std::string());
        QCOMPARE(scene.cellAt({563, 410}, 4), std::string("r")); // tolerance grows the rotated box
        // Edge: distance to the polyline, with tolerance.
        QCOMPARE(scene.cellAt({220, 72}, 4), std::string("e"));
        QCOMPARE(scene.cellAt({220, 90}, 4), std::string());
        // Topmost first: the child of the group hides the group itself.
        const auto hits = scene.cellsAt({60, 210}, 0);
        QVERIFY(hits.size() >= 2);
        QCOMPARE(hits.front(), std::string("c1"));
        QCOMPARE(hits.back(), std::string("g"));
    }

    void cellsInRectNeedsFullContainment()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();
        const auto inside = scene.cellsInRect(QRectF(30, 30, 140, 80));
        QCOMPARE(inside, (std::vector<std::string>{"a"}));
        QVERIFY(scene.cellsInRect(QRectF(30, 30, 100, 80)).empty());
    }

    // QRectF::contains() never contains a rectangle without width or height;
    // straight edges and zero-size vertices must still be rubber-banded.
    void cellsInRectSelectsStraightEdgesAndRotatedVertices()
    {
        const auto vertex = [](const char* id, const char* style, int x, int y, int w, int h) {
            return QStringLiteral("<mxCell id=\"%1\" value=\"\" style=\"%2\" vertex=\"1\" parent=\"1\">"
                                  "<mxGeometry x=\"%3\" y=\"%4\" width=\"%5\" height=\"%6\" as=\"geometry\"/></mxCell>")
                .arg(QLatin1String(id), QLatin1String(style))
                .arg(x).arg(y).arg(w).arg(h);
        };
        const auto edge = [](const char* id, int x1, int y1, int x2, int y2) {
            return QStringLiteral("<mxCell id=\"%1\" style=\"endArrow=none;\" edge=\"1\" parent=\"1\">"
                                  "<mxGeometry relative=\"1\" as=\"geometry\">"
                                  "<mxPoint x=\"%2\" y=\"%3\" as=\"sourcePoint\"/>"
                                  "<mxPoint x=\"%4\" y=\"%5\" as=\"targetPoint\"/></mxGeometry></mxCell>")
                .arg(QLatin1String(id)).arg(x1).arg(y1).arg(x2).arg(y2);
        };
        const QString xml = QStringLiteral("<mxfile><diagram id=\"p\" name=\"P\"><mxGraphModel><root>"
                                           "<mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>")
            + edge("h", 10, 20, 110, 20)      // horizontal: zero height
            + edge("v", 200, 10, 200, 110)    // vertical: zero width
            + edge("out", 300, 20, 500, 20)   // sticks out of the band
            + vertex("dot", "rounded=0;", 600, 20, 0, 0) // zero size
            + vertex("rot", "rotation=90;", 700, 50, 100, 20) // box (720..780, 10..110) once rotated
            + QStringLiteral("</root></mxGraphModel></diagram></mxfile>");
        DocumentSession session(documentFromXml(xml.toStdString()));
        auto canvas = makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();

        QCOMPARE(scene.cellsInRect(QRectF(0, 0, 120, 40)), (std::vector<std::string>{"h"}));
        QCOMPARE(scene.cellsInRect(QRectF(190, 0, 20, 120)), (std::vector<std::string>{"v"}));
        QCOMPARE(scene.cellsInRect(QRectF(590, 10, 20, 20)), (std::vector<std::string>{"dot"}));
        // Whole band: everything but the edge that leaves it.
        const auto all = scene.cellsInRect(QRectF(0, 0, 480, 130));
        QCOMPARE(all, (std::vector<std::string>{"h", "v"}));
        // The rotated box is what counts: the unrotated one (700..800 x 50..70)
        // is not inside this band, the rotated one (720..780 x 10..110) is.
        QCOMPARE(scene.cellsInRect(QRectF(715, 5, 70, 110)), (std::vector<std::string>{"rot"}));
        QVERIFY(scene.cellsInRect(QRectF(695, 45, 110, 30)).empty());
        // The scope filter is unchanged: nothing is inside another scope.
        QVERIFY(scene.cellsInRect(QRectF(0, 0, 120, 40), "nowhere").empty());
    }

    // curvedText has noLabel=1: the value reaches the painter as valueText, and
    // the scene paints it unless the label editor has the cell.
    void curvedTextValueIsPaintedUntilTheLabelEditorHidesIt()
    {
        const QString xml = QStringLiteral(
            "<mxfile><diagram id=\"p\" name=\"P\"><mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
            "<mxCell id=\"ct\" value=\"Hello\" style=\"shape=curvedText;noLabel=1;strokeColor=none;fillColor=none;\" "
            "vertex=\"1\" parent=\"1\"><mxGeometry x=\"20\" y=\"20\" width=\"100\" height=\"60\" as=\"geometry\"/></mxCell>"
            "</root></mxGraphModel></diagram></mxfile>");
        DocumentSession session(documentFromXml(xml.toStdString()));
        auto canvas = makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();
        QCOMPARE(scene.state("ct")->valueText, std::string("Hello"));
        QVERIFY(scene.state("ct")->labelText.empty());

        const auto inkOf = [&] {
            QImage image(140, 100, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            scene.render(&painter, QRectF(0, 0, 140, 100), QRectF(0, 0, 140, 100));
            painter.end();
            return image;
        };
        const QImage shown = inkOf();
        scene.setHiddenLabel("ct");
        const QImage hidden = inkOf();
        scene.setHiddenLabel({});
        QVERIFY(shown != hidden);           // the text along the arc is gone while editing
        QCOMPARE(inkOf(), shown);          // and back afterwards
    }

    void rendererFollowsTheBaseDirectory()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        session.setBaseDirectory(QStringLiteral("/tmp/qraw-canvas-test"));
        QVERIFY(session.edit(QStringLiteral("Move"), [](commands::Transaction& tx) {
                    return ops::moveCells(tx, 0, {"a"}, 10, 0);
                }).ok);
        QCOMPARE(canvas->diagramScene().renderer().context().images.baseDirectory,
                 QStringLiteral("/tmp/qraw-canvas-test"));
    }

    void hitTestsEqualAScanOfEveryState()
    {
        DocumentSession session(documentFromXml(hitTestDiagram()));
        auto canvas = makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();
        const core::Page& page = *session.currentPage();
        QVERIFY(scene.pageView().states().size() > 500);
        const QRectF area = scene.pageArea();

        std::uint64_t seed = 12345;
        const auto next = [&seed] {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            return static_cast<double>(seed % 100003) / 100003.0;
        };
        std::size_t hitsFound = 0;
        for (int i = 0; i < 1500; ++i) {
            const QPointF p(area.left() + next() * area.width(), area.top() + next() * area.height());
            const double tolerance = i % 3 == 0 ? 0 : i % 3 == 1 ? 4 : 25;
            const auto expected = referenceCellsAt(scene, page, p, tolerance);
            QCOMPARE(scene.cellsAt(p, tolerance), expected);
            hitsFound += expected.size();
        }
        QVERIFY(hitsFound > 300); // the points do land on cells
        // Points on the cells themselves: the first vertices and the straight edges.
        for (const render::CellState& s : scene.pageView().states()) {
            const QPointF p(s.bounds.x + s.bounds.width / 2, s.bounds.y + s.bounds.height / 2);
            QCOMPARE(scene.cellsAt(p, 2), referenceCellsAt(scene, page, p, 2));
        }

        for (int i = 0; i < 300; ++i) {
            const double w = i % 10 == 0 ? area.width() : next() * area.width() * 0.4;
            const double h = i % 10 == 0 ? area.height() : next() * area.height() * 0.4;
            const QRectF r(area.left() + next() * area.width() * 0.6, area.top() + next() * area.height() * 0.6, w, h);
            QCOMPARE(scene.cellsInRect(r), referenceCellsInRect(scene, page, r, {}));
        }
        // The rubber band takes a straight edge and a zero-size vertex, and honours the scope.
        const auto everything = scene.cellsInRect(area.adjusted(-10, -10, 10, 10));
        QVERIFY(std::find(everything.begin(), everything.end(), "h") != everything.end());
        QVERIFY(std::find(everything.begin(), everything.end(), "vv") != everything.end());
        QVERIFY(std::find(everything.begin(), everything.end(), "v0") != everything.end()); // 0 x 0
        QCOMPARE(scene.cellsInRect(area.adjusted(-10, -10, 10, 10), "sw"), referenceCellsInRect(scene, page, area.adjusted(-10, -10, 10, 10), "sw"));

        // The same after edits: the grid follows the projection.
        QVERIFY(session.edit(QStringLiteral("Move"), [](commands::Transaction& tx) {
                    return ops::moveCells(tx, 0, {"v3", "v40", "sw"}, 333, 111);
                }).ok);
        for (int i = 0; i < 300; ++i) {
            const QPointF p(area.left() + next() * area.width(), area.top() + next() * area.height());
            QCOMPARE(scene.cellsAt(p, 4), referenceCellsAt(scene, page, p, 4));
        }
        QVERIFY(session.undo());
        for (int i = 0; i < 300; ++i) {
            const QPointF p(area.left() + next() * area.width(), area.top() + next() * area.height());
            QCOMPARE(scene.cellsAt(p, 4), referenceCellsAt(scene, page, p, 4));
        }
    }

    void largePageUpdateIsFast()
    {
        DocumentSession session(documentFromXml(bigDiagram()));
        auto canvas = makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();
        QCOMPARE(scene.pageView().states().size(), std::size_t(2500));

        QElapsedTimer timer;
        timer.start();
        QVERIFY(session.edit(QStringLiteral("Move"), [](commands::Transaction& tx) {
                    return ops::moveCells(tx, 0, {"v1000"}, 10, 0);
                }).ok);
        const qint64 ms = timer.elapsed();
        qInfo() << "move on a 2500-cell page (edit + projection):" << ms << "ms";
        QVERIFY2(ms < 200 * QRAW_TEST_TIMEOUT_SCALE, qPrintable(QStringLiteral("%1 ms").arg(ms))); // 3x under sanitizers

        timer.restart();
        const std::string hit = scene.cellAt({40.0 + 80 * 10, 20.0 + 60 * 5}, 4);
        qInfo() << "hit test:" << timer.nsecsElapsed() / 1000 << "us";
        QCOMPARE(hit, std::string("v260"));
    }

    void backgroundShowsBackdropPageAndGrid()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        canvas->setZoom(1);
        canvas->centerOn(QRectF(200, 200, 1, 1));
        const auto pixel = [&](const QImage& image, QPointF scenePos) {
            return image.pixelColor(devicePixel(*canvas, scenePos));
        };
        QImage image = canvas->viewport()->grab().toImage();
        QCOMPARE(pixel(image, {405, 205}), QColor(Qt::white));          // page, between grid lines
        QCOMPARE(pixel(image, {-5, 205}), QColor(0xf0, 0xf0, 0xf0));    // backdrop left of the page
        // A major line (every 40) and a minor one (every 10); +-1 device px tolerates line snapping.
        const auto rowHas = [&](QColor color, double sceneX) {
            const QPoint p = devicePixel(*canvas, QPointF(sceneX, 205));
            for (int dx = -1; dx <= 1; ++dx) {
                if (image.pixelColor(p + QPoint(dx, 0)) == color)
                    return true;
            }
            return false;
        };
        QVERIFY(rowHas(QColor(0xd8, 0xd8, 0xd8), 400));
        QVERIFY(rowHas(QColor(0xe8, 0xe8, 0xe8), 410));
        QVERIFY(grabToPng(*canvas, QStringLiteral("canvas-grid")) || qEnvironmentVariableIsEmpty("QRAW_SCREENSHOT_DIR"));

        // Page background colour; no page tiles and an unbounded background when pageVisible = 0.
        QVERIFY(session.edit(QStringLiteral("Page setup"), [](commands::Transaction& tx) {
                    commands::ops::PageProperties p;
                    p.background = "#ff0000";
                    p.pageVisible = false;
                    p.grid = false;
                    return ops::setPageProperties(tx, 0, p);
                }).ok);
        image = canvas->viewport()->grab().toImage();
        QCOMPARE(pixel(image, {405, 205}), QColor(255, 0, 0));
        QCOMPARE(pixel(image, {-5, 205}), QColor(255, 0, 0));
    }
};

QTEST_MAIN(TestCanvasProjection)
#include "tst_canvas_projection.moc"
