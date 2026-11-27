// hover arrows, connection points and edge handles driven
// through the real CanvasView with synthetic mouse input.

#include "support/test_support.h"

#include "../../src/ui/edge_handle_model.h"
#include "../../src/ui/edge_handles.h"

#include "qraw/commands/ops_properties.h"
#include "qraw/ui/cell_queries.h"

#include <QtTest>

#include <set>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

namespace {

std::set<std::string> cellIds(const DocumentSession& session)
{
    std::set<std::string> ids;
    for (const core::Cell* cell : session.currentPage()->cells())
        ids.insert(cell->id());
    return ids;
}

// The one cell that appeared since `before`.
const core::Cell* addedCell(const DocumentSession& session, const std::set<std::string>& before)
{
    const core::Cell* found = nullptr;
    for (const core::Cell* cell : session.currentPage()->cells()) {
        if (before.count(cell->id()) == 0) {
            if (found != nullptr)
                return nullptr; // more than one
            found = cell;
        }
    }
    return found;
}

const core::Cell& cell(const DocumentSession& session, const char* id)
{
    return *session.currentPage()->findCell(id);
}

// simpleDocument with edge "e" as a plain straight connector (virtual handles).
core::Document straightEdgeDocument()
{
    std::string xml = kSimpleDiagram;
    const std::string from = "edgeStyle=orthogonalEdgeStyle;";
    xml.replace(xml.find(from), from.size(), "");
    return documentFromXml(xml);
}

// simpleDocument with "b" moved down, so that the orthogonal edge "e" is an S:
// (160,70) -> (220,70) -> (220,230) -> (280,230).
core::Document sShapedDocument()
{
    std::string xml = kSimpleDiagram;
    const std::string from = "<mxGeometry x=\"280\" y=\"40\"";
    xml.replace(xml.find(from), from.size(), "<mxGeometry x=\"280\" y=\"200\"");
    const std::string style = "edgeStyle=orthogonalEdgeStyle;";
    xml.replace(xml.find(style), style.size(), style + "exitX=1;exitY=0.5;entryX=0;entryY=0.5;");
    return documentFromXml(xml);
}

} // namespace

class TestConnectEdit : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QVERIFY(!createEdgeHandlers(ToolContext{}).empty()); }

    // --- hover arrows -----------------------------------------------------------

    void hoverShowsArrowsAndClaimsCursor()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        InteractionController& controller = view->interaction();

        move(*view, {100, 70}, Qt::NoModifier, Qt::NoButton);
        QCOMPARE(controller.cursor(), Qt::ArrowCursor);
        move(*view, {180, 70}, Qt::NoModifier, Qt::NoButton); // the east arrow
        QCOMPARE(controller.cursor(), Qt::PointingHandCursor);
        grabToPng(*view, QStringLiteral("connect-hover-arrows"));
        move(*view, {600, 500}, Qt::NoModifier, Qt::NoButton);
        QCOMPARE(controller.cursor(), Qt::ArrowCursor);
    }

    void groupIsNotConnectable()
    {
        // 'g' has connectable="0": no arrow zone around it.
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        const auto before = savedBytes(session);
        drag(*view, {140, 320}, {600, 500}); // below g, where its south arrow would be
        QCOMPARE(savedBytes(session), before);
    }

    // --- connect gestures ---------------------------------------------------------

    void dragArrowOntoVertexConnects()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        const auto before = savedBytes(session);
        const auto ids = cellIds(session);

        move(*view, {100, 70}, Qt::NoModifier, Qt::NoButton);
        drag(*view, {180, 70}, {340, 70}); // east arrow of 'a' onto 'b'

        const core::Cell* edge = addedCell(session, ids);
        QVERIFY(edge != nullptr);
        QVERIFY(edge->isEdge());
        QCOMPARE(QString::fromStdString(edge->source()), QStringLiteral("a"));
        QCOMPARE(QString::fromStdString(edge->target()), QStringLiteral("b"));
        QCOMPARE(QString::fromStdString(edge->styleText()),
                 QStringLiteral("edgeStyle=orthogonalEdgeStyle;rounded=0;orthogonalLoop=1;jettySize=auto;html=1;"));
        QCOMPARE(session.selection().cells(), std::vector<std::string>{edge->id()});

        QVERIFY(session.undo());
        QCOMPARE(savedBytes(session), before); // one undo entry
        QVERIFY(!session.canUndo());
    }

    void dragArrowToBlankClonesTheVertex()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        const auto before = savedBytes(session);
        const auto ids = cellIds(session);

        drag(*view, {180, 70}, {300, 300});

        int vertices = 0, edges = 0;
        const core::Cell* clone = nullptr;
        const core::Cell* edge = nullptr;
        for (const core::Cell* c : session.currentPage()->cells()) {
            if (ids.count(c->id()))
                continue;
            (c->isVertex() ? vertices : edges)++;
            (c->isVertex() ? clone : edge) = c;
        }
        QCOMPARE(vertices, 1);
        QCOMPARE(edges, 1);
        QCOMPARE(QString::fromStdString(clone->styleText()), QStringLiteral("rounded=0;whiteSpace=wrap;"));
        QCOMPARE(QString::fromStdString(clone->value()), QStringLiteral("A")); // draw.io keeps the label
        const core::Rect bounds = clone->geometry()->bounds();
        QCOMPARE(bounds.width, 120.0);
        QCOMPARE(bounds.height, 60.0);
        QVERIFY(std::abs(bounds.x + 60 - 300) <= 10); // centred under the pointer (grid snapped)
        QVERIFY(std::abs(bounds.y + 30 - 300) <= 10);
        QCOMPARE(QString::fromStdString(edge->source()), QStringLiteral("a"));
        QCOMPARE(QString::fromStdString(edge->target()), QString::fromStdString(clone->id()));
        QCOMPARE(session.selection().cells(), std::vector<std::string>{clone->id()});

        QVERIFY(session.undo());
        QCOMPARE(savedBytes(session), before);
    }

    void clickArrowClonesInThatDirection()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        const auto before = savedBytes(session);
        const auto ids = cellIds(session);

        click(*view, {100, 20}); // north arrow of 'a'

        const core::Cell* clone = nullptr;
        for (const core::Cell* c : session.currentPage()->cells())
            if (!ids.count(c->id()) && c->isVertex())
                clone = c;
        QVERIFY(clone != nullptr);
        const core::Rect bounds = clone->geometry()->bounds();
        QCOMPARE(bounds.x, 40.0);
        QCOMPARE(bounds.y, 40.0 - 80.0 - 60.0); // Graph.defaultEdgeLength = 80 above 'a'
        QCOMPARE(session.selection().cells(), std::vector<std::string>{clone->id()});

        QVERIFY(session.undo());
        QCOMPARE(savedBytes(session), before);
    }

    void dragConnectionPointToBlankCreatesDanglingEdge()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        const auto before = savedBytes(session);
        const auto ids = cellIds(session);

        drag(*view, {160, 70}, {300, 300}); // the east connection point of 'a'

        const core::Cell* edge = addedCell(session, ids);
        QVERIFY(edge != nullptr);
        QCOMPARE(QString::fromStdString(edge->source()), QStringLiteral("a"));
        QVERIFY(edge->target().empty());
        QVERIFY(edge->style().value("exitX") == std::optional<std::string>("1"));
        QVERIFY(edge->style().value("exitY") == std::optional<std::string>("0.5"));
        const auto target = edge->geometry()->targetPoint();
        QVERIFY(target.has_value());
        QCOMPARE(target->x, 300.0);
        QCOMPARE(target->y, 300.0);

        QVERIFY(session.undo());
        QCOMPARE(savedBytes(session), before);
    }

    void connectToConnectionPointWritesConstraint()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        const auto ids = cellIds(session);

        // From the arrow onto the south connection point of 'b' (340,100).
        drag(*view, {180, 70}, {340, 98});

        const core::Cell* edge = addedCell(session, ids);
        QVERIFY(edge != nullptr);
        QCOMPARE(QString::fromStdString(edge->target()), QStringLiteral("b"));
        QVERIFY(edge->style().value("entryX") == std::optional<std::string>("0.5"));
        QVERIFY(edge->style().value("entryY") == std::optional<std::string>("1"));
    }

    void escapeCancelsTheDrag()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        const auto before = savedBytes(session);
        press(*view, {180, 70});
        move(*view, {250, 150});
        view->interaction().cancelGesture();
        release(*view, {250, 150});
        QCOMPARE(savedBytes(session), before);
    }

    // --- edge handles ---------------------------------------------------------------

    void dragTargetEndToBlankDisconnects()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        session.selection().set({"e"});
        const auto before = savedBytes(session);

        grabToPng(*view, QStringLiteral("connect-edge-handles"));
        drag(*view, {280, 70}, {300, 300});

        const core::Cell& e = cell(session, "e");
        QCOMPARE(QString::fromStdString(e.source()), QStringLiteral("a"));
        QVERIFY(e.target().empty());
        const auto target = e.geometry()->targetPoint();
        QVERIFY(target.has_value());
        QCOMPARE(target->x, 300.0);
        QCOMPARE(target->y, 300.0);
        QCOMPARE(session.undoText(), QStringLiteral("Disconnect"));

        QVERIFY(session.undo());
        QCOMPARE(savedBytes(session), before);
        QVERIFY(!session.canUndo());
    }

    void dragTargetEndOntoVertexReconnects()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        session.selection().set({"e"});
        const auto before = savedBytes(session);

        drag(*view, {280, 70}, {100, 70}); // onto 'a'

        QCOMPARE(QString::fromStdString(cell(session, "e").target()), QStringLiteral("a"));
        QCOMPARE(session.undoText(), QStringLiteral("Connect"));
        QVERIFY(session.undo());
        QCOMPARE(savedBytes(session), before);
    }

    void virtualHandleAddsWaypointAndDoubleClickRemovesIt()
    {
        DocumentSession session(straightEdgeDocument());
        auto view = makeCanvas(session);
        session.selection().set({"e"});
        const auto before = savedBytes(session);

        drag(*view, {220, 70}, {220, 150}); // the virtual handle at the middle of 'e'
        auto points = cell(session, "e").geometry()->points();
        QCOMPARE(points.size(), std::size_t(1));
        QCOMPARE(points[0].x, 220.0);
        QCOMPARE(points[0].y, 150.0);
        QCOMPARE(session.undoText(), QStringLiteral("Change Points"));
        const auto withPoint = savedBytes(session);

        doubleClick(*view, {220, 150}); // the waypoint handle
        QVERIFY(cell(session, "e").geometry()->points().empty());
        QVERIFY(session.undo());
        QCOMPARE(savedBytes(session), withPoint);
        QVERIFY(session.undo());
        QCOMPARE(savedBytes(session), before);
    }

    void movingAWaypointKeepsItsIndex()
    {
        DocumentSession session(straightEdgeDocument());
        auto view = makeCanvas(session);
        session.selection().set({"e"});
        drag(*view, {220, 70}, {220, 150});
        const auto withOne = savedBytes(session);

        drag(*view, {220, 150}, {250, 170});
        auto points = cell(session, "e").geometry()->points();
        QCOMPARE(points.size(), std::size_t(1));
        QCOMPARE(points[0].x, 250.0);
        QCOMPARE(points[0].y, 170.0);
        QVERIFY(session.undo());
        QCOMPARE(savedBytes(session), withOne);
    }

    void shiftClickAddsAndRemovesWaypoints()
    {
        DocumentSession session(straightEdgeDocument());
        auto view = makeCanvas(session);
        session.selection().set({"e"});
        const auto before = savedBytes(session);

        click(*view, {200, 70}, Qt::ShiftModifier); // on the edge line
        auto points = cell(session, "e").geometry()->points();
        QCOMPARE(points.size(), std::size_t(1));
        QCOMPARE(points[0].x, 200.0);
        QCOMPARE(points[0].y, 70.0);

        click(*view, {200, 70}, Qt::ShiftModifier); // on the waypoint handle
        QVERIFY(cell(session, "e").geometry()->points().empty());
        QVERIFY(session.undo());
        QVERIFY(session.undo());
        QCOMPARE(savedBytes(session), before);
    }

    void pressWithoutMovementChangesNothing()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        session.selection().set({"e"});
        const auto before = savedBytes(session);
        click(*view, {220, 70}); // virtual handle
        click(*view, {280, 70}); // target handle
        QCOMPARE(savedBytes(session), before);
        QVERIFY(!session.canUndo());
    }

    void lockedEdgeHasNoHandles()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        QVERIFY(session.edit(QStringLiteral("lock"), [](commands::Transaction& tx) {
            return commands::ops::setStyle(tx, 0, {"e"}, "locked", "1");
        }));
        const ToolContext context{&session, &view->diagramScene(), view.get()};
        session.selection().set({"e"});
        QVERIFY(!edgeh::EdgeHandlesHandler::selectedEdgeHandles(context, 1).has_value());
        session.selection().set({"a"});
        QVERIFY(!edgeh::EdgeHandlesHandler::selectedEdgeHandles(context, 1).has_value());
    }

    // --- handle model ---------------------------------------------------------------

    void elbowStylesGetOneHandle()
    {
        std::string xml = kSimpleDiagram;
        const std::string from = "edgeStyle=orthogonalEdgeStyle;";
        xml.replace(xml.find(from), from.size(), "edgeStyle=elbowEdgeStyle;");
        DocumentSession session(documentFromXml(xml));
        auto view = makeCanvas(session);
        const render::CellState* state = view->diagramScene().state("e");
        QVERIFY(state != nullptr);
        const auto handles = edgeh::edgeHandlesFor(*session.currentPage(), *state, 1);
        QVERIFY(handles.has_value());
        QCOMPARE(handles->handles.size(), std::size_t(3));
        QCOMPARE(int(handles->handles[2].kind), int(edgeh::HandleKind::Elbow));

        session.selection().set({"e"});
        const QPointF at = handles->handles[2].position;
        drag(*view, at, at + QPointF(0, 80));
        QCOMPARE(cell(session, "e").geometry()->points().size(), std::size_t(1));
    }

    void orthogonalEdgesGetSegmentHandles()
    {
        DocumentSession session(sShapedDocument());
        auto view = makeCanvas(session);
        const render::CellState* state = view->diagramScene().state("e");
        QVERIFY(state != nullptr);
        const auto handles = edgeh::edgeHandlesFor(*session.currentPage(), *state, 1);
        QVERIFY(handles.has_value());
        QVERIFY(handles->segmentStyle);
        // The two ends and one handle at the midpoint of each of the three segments.
        QCOMPARE(handles->handles.size(), std::size_t(5));
        std::vector<QPointF> mids;
        for (const edgeh::Handle& h : handles->handles) {
            if (h.kind == edgeh::HandleKind::Segment)
                mids.push_back(h.position);
        }
        QCOMPARE(mids.size(), std::size_t(3));
        QCOMPARE(mids[0], QPointF(190, 70));
        QCOMPARE(mids[1], QPointF(220, 150));
        QCOMPARE(mids[2], QPointF(250, 230));
        QVERIFY(handles->handles[3].verticalSegment);
    }

    void draggingTheMiddleSegmentMovesIt()
    {
        DocumentSession session(sShapedDocument());
        auto view = makeCanvas(session);
        session.selection().set({"e"});
        const auto before = savedBytes(session);

        drag(*view, {220, 150}, {250, 150}); // +30 in x: only the vertical segment moves
        const auto points = cell(session, "e").geometry()->points();
        QCOMPARE(points.size(), std::size_t(2));
        QCOMPARE(points[0].x, 250.0);
        QCOMPARE(points[0].y, 70.0);
        QCOMPARE(points[1].x, 250.0);
        QCOMPARE(points[1].y, 230.0);
        QCOMPARE(session.undoText(), QStringLiteral("Change Points"));

        // Dragged back, the edge routes as it did without waypoints: they go away.
        drag(*view, {250, 150}, {220, 150});
        QVERIFY(cell(session, "e").geometry()->points().empty());
        QVERIFY(session.undo());
        QVERIFY(session.undo());
        QCOMPARE(savedBytes(session), before);
    }

    void draggingAStraightEdgeTheMiddleHandleBendsIt()
    {
        DocumentSession session(simpleDocument()); // a straight orthogonal edge at y = 70
        auto view = makeCanvas(session);
        session.selection().set({"e"});
        const auto before = savedBytes(session);
        drag(*view, {220, 70}, {220, 150});
        QVERIFY(!cell(session, "e").geometry()->points().empty());
        const render::CellState* state = view->diagramScene().state("e");
        QVERIFY(state->points.size() > 2);
        QVERIFY(session.undo());
        QCOMPARE(savedBytes(session), before);
    }

    void endDragPreviewFollowsTheEdgeStyle()
    {
        DocumentSession session(simpleDocument()); // orthogonal edge a -> b
        auto view = makeCanvas(session);
        const render::PageView& pageView = view->diagramScene().pageView();
        render::RouteOverride change;
        change.target.kind = render::RouteEnd::Kind::Point;
        change.target.point = {300, 300};
        const auto route = pageView.previewRoute(*pageView.find("e"), change);
        // Not the straight line: the orthogonal style turns a corner and ends at the free point.
        QVERIFY(route.size() >= 3);
        QCOMPARE(route.back().x, 300.0);
        QCOMPARE(route.back().y, 300.0);
        for (std::size_t i = 0; i + 1 < route.size(); ++i)
            QVERIFY(route[i].x == route[i + 1].x || route[i].y == route[i + 1].y);
    }

    void insertionIndexFollowsSegmentOrder()
    {
        const std::vector<QPointF> route{{0, 0}, {100, 0}, {100, 100}, {200, 100}};
        const std::vector<QPointF> waypoints{{100, 0}, {100, 100}};
        QCOMPARE(edgeh::insertionIndex(route, waypoints, 0), std::size_t(0));
        QCOMPARE(edgeh::insertionIndex(route, waypoints, 1), std::size_t(1));
        QCOMPARE(edgeh::insertionIndex(route, waypoints, 2), std::size_t(2));
        QCOMPARE(edgeh::nearestSegment(route, {100, 50}), std::size_t(1));
    }
};

QTEST_MAIN(TestConnectEdit)
#include "tst_connect_edit.moc"
