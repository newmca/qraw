// PageView::update patches a view after an edit that kept the tree: the
// result equals a full build, and an edit it cannot handle leaves the view
// untouched (nullopt).

#include "qraw/format/codec.h"
#include "qraw/render/page_view.h"
#include "qraw/render/stats.h"

#include <QTest>

#include <algorithm>
#include <functional>

using namespace qraw;
using render::CellState;
using render::PageView;

namespace {

// A page with everything an update has to follow: terminals and edges
// (an edge attached to another edge, and a loop), a group with a rotated child
// and a relative child, an edge label (a child of an edge), a collapsed
// container with an edge to a cell inside it, line jumps over crossing edges,
// and a label that reads an attribute of its parent.
constexpr char kDiagram[] = R"(<mxfile><diagram id="p" name="p"><mxGraphModel gridSize="10"><root>
<mxCell id="0"/><mxCell id="1" parent="0"/>
<mxCell id="a" value="A" style="rounded=1;" vertex="1" parent="1"><mxGeometry x="40" y="40" width="120" height="60" as="geometry"/></mxCell>
<mxCell id="b" value="B" style="ellipse;" vertex="1" parent="1"><mxGeometry x="300" y="40" width="120" height="60" as="geometry"/></mxCell>
<mxCell id="e1" style="edgeStyle=orthogonalEdgeStyle;endArrow=classic;" edge="1" parent="1" source="a" target="b"><mxGeometry relative="1" as="geometry"/></mxCell>
<mxCell id="lbl" value="label" style="edgeLabel;" vertex="1" connectable="0" parent="e1"><mxGeometry x="-0.2" relative="1" as="geometry"><mxPoint as="offset"/></mxGeometry></mxCell>
<mxCell id="g" value="" style="group" vertex="1" connectable="0" parent="1"><mxGeometry x="40" y="200" width="240" height="120" as="geometry"/></mxCell>
<mxCell id="c1" value="C1" style="rounded=0;rotation=30;" vertex="1" parent="g"><mxGeometry x="10" y="10" width="80" height="40" as="geometry"/></mxCell>
<mxCell id="c2" value="C2" style="" vertex="1" parent="g"><mxGeometry x="0.5" y="0.5" width="60" height="30" relative="1" as="geometry"/></mxCell>
<mxCell id="x1" style="endArrow=block;jumpStyle=arc;" edge="1" parent="1"><mxGeometry relative="1" as="geometry"><mxPoint x="200" y="20" as="sourcePoint"/><mxPoint x="200" y="140" as="targetPoint"/></mxGeometry></mxCell>
<mxCell id="x2" style="endArrow=block;jumpStyle=arc;" edge="1" parent="1" source="a"><mxGeometry relative="1" as="geometry"><mxPoint x="360" y="80" as="targetPoint"/></mxGeometry></mxCell>
<mxCell id="ee" style="endArrow=oval;" edge="1" parent="1" source="x1" target="b"><mxGeometry relative="1" as="geometry"/></mxCell>
<mxCell id="loop" style="edgeStyle=orthogonalEdgeStyle;" edge="1" parent="1" source="b" target="b"><mxGeometry relative="1" as="geometry"/></mxCell>
<mxCell id="box" value="Box" style="swimlane;" vertex="1" collapsed="1" parent="1"><mxGeometry x="500" y="40" width="160" height="120" as="geometry"><mxRectangle x="500" y="40" width="160" height="30" as="alternateBounds"/></mxGeometry></mxCell>
<mxCell id="inBox" value="in" style="" vertex="1" parent="box"><mxGeometry x="20" y="40" width="60" height="30" as="geometry"/></mxCell>
<mxCell id="toBox" style="endArrow=classic;" edge="1" parent="1" source="b" target="inBox"><mxGeometry relative="1" as="geometry"/></mxCell>
<object label="Name: %name%" name="Zed" id="holder"><mxCell style="" vertex="1" parent="1"><mxGeometry x="40" y="400" width="100" height="40" as="geometry"/></mxCell></object>
<mxCell id="part" value="%name%" style="" vertex="1" parent="holder"><mxGeometry x="10" y="10" width="40" height="20" as="geometry"/></mxCell>
<mxCell id="lonely" value="L" style="" vertex="1" parent="1"><mxGeometry x="700" y="400" width="40" height="40" as="geometry"/></mxCell>
<mxCell id="dead" style="" edge="1" parent="1" source="lonely"><mxGeometry relative="1" as="geometry"/></mxCell>
</root></mxGraphModel></diagram></mxfile>)";

core::Document makeDocument()
{
    auto read = format::readDocument(QByteArray(kDiagram));
    if (!read.ok())
        qFatal("fixture does not parse");
    return std::move(*read.document);
}

const render::RenderContext& context()
{
    static const render::RenderContext c = render::RenderContext::defaults();
    return c;
}

// Everything in a state the canvas and the export read (the diagnostics of an
// updated view are the earlier build's, so they are not compared).
bool sameState(const CellState& a, const CellState& b)
{
    return a.id == b.id && a.kind == b.kind && a.parent == b.parent && a.depth == b.depth && a.collapsed == b.collapsed
        && a.shape == b.shape && a.bounds == b.bounds && a.paintBounds == b.paintBounds && a.points == b.points
        && a.routedPoints == b.routedPoints && a.label == b.label && a.labelText == b.labelText
        && a.valueText == b.valueText && a.style == b.style;
}

QString firstDifference(const PageView& a, const PageView& b)
{
    if (a.states().size() != b.states().size())
        return QStringLiteral("state count %1 vs %2").arg(a.states().size()).arg(b.states().size());
    for (std::size_t i = 0; i < a.states().size(); ++i) {
        if (!sameState(a.states()[i], b.states()[i]))
            return QString::fromStdString(a.states()[i].id);
    }
    return {};
}

using Edit = std::function<void(core::Page&)>;

void change(core::Page& page, const std::string& id, const std::function<void(core::Cell&)>& edit)
{
    core::Cell cell = *page.findCell(id);
    edit(cell);
    QVERIFY(page.replaceCell(std::move(cell)));
}

} // namespace

class PageViewUpdateTest : public QObject {
    Q_OBJECT

    // Applies `edit` to the page, patches a view with update() and compares it with a full build.
    static void expectEqualToBuild(const std::vector<std::string>& changed, const Edit& edit)
    {
        core::Document doc = makeDocument();
        core::Page& page = *doc.page(0);
        PageView view = PageView::build(page, context());
        edit(page);
        const auto update = view.update(page, context(), std::set<std::string>(changed.begin(), changed.end()));
        QVERIFY(update.has_value());
        const PageView expected = PageView::build(page, context());
        QCOMPARE(firstDifference(view, expected), QString());
        // What the update says it rebuilt: ascending, with the earlier states.
        QCOMPARE(update->indices.size(), update->before.size());
        QVERIFY(std::is_sorted(update->indices.begin(), update->indices.end()));
        for (const std::string& id : changed)
            QVERIFY(std::ranges::any_of(update->indices, [&](std::size_t i) { return view.states()[i].id == id; }));
    }

    // The edit is one update() cannot handle: nullopt, and the view is untouched.
    static void expectRefused(const std::vector<std::string>& changed, const Edit& edit)
    {
        core::Document doc = makeDocument();
        core::Page& page = *doc.page(0);
        PageView view = PageView::build(page, context());
        const PageView untouched = PageView::build(page, context());
        // The refused states are compared against a build of the page as it was.
        edit(page);
        QVERIFY(!view.update(page, context(), std::set<std::string>(changed.begin(), changed.end())).has_value());
        QCOMPARE(view.states().size(), untouched.states().size());
        for (std::size_t i = 0; i < view.states().size(); ++i) {
            const CellState& s = view.states()[i];
            const CellState& u = untouched.states()[i];
            // Only the cell the edit replaced is different in the page, never in the states.
            QVERIFY2(s.id == u.id && s.bounds == u.bounds && s.points == u.points && s.paintBounds == u.paintBounds
                         && s.style == u.style,
                     s.id.c_str());
        }
    }

private slots:
    void moveVertexReroutesItsEdgesAndLabels()
    {
        expectEqualToBuild({"b"}, [](core::Page& page) {
            change(page, "b", [](core::Cell& c) {
                c.geometry()->setX(380);
                c.geometry()->setY(150);
            });
        });
    }

    void moveGroupMovesChildrenAndTheirEdges()
    {
        expectEqualToBuild({"g"}, [](core::Page& page) {
            change(page, "g", [](core::Cell& c) { c.geometry()->setX(120); });
        });
    }

    void editingAChildOfAGroupLeavesTheRestAlone()
    {
        expectEqualToBuild({"c1"}, [](core::Page& page) {
            change(page, "c1", [](core::Cell& c) { c.setStyleText("rounded=0;rotation=75;fillColor=#ff0000;"); });
        });
    }

    void styleAndValueChanges()
    {
        expectEqualToBuild({"a", "e1"}, [](core::Page& page) {
            change(page, "a", [](core::Cell& c) {
                c.setStyleText("rounded=1;fontSize=20;fontStyle=1;strokeWidth=4;");
                c.setValue("Another, longer value");
            });
            change(page, "e1", [](core::Cell& c) { c.setStyleText("edgeStyle=elbowEdgeStyle;endArrow=diamond;"); });
        });
    }

    void aLabelFollowsAnAttributeOfItsParent()
    {
        expectEqualToBuild({"holder"}, [](core::Page& page) {
            change(page, "holder", [](core::Cell& c) { c.setData("name", "Changed"); });
        });
    }

    void lineJumpsAreRecomputed()
    {
        // x2 crosses x1; moving the end of x2 changes where it jumps, and moving x1 does too.
        expectEqualToBuild({"x1"}, [](core::Page& page) {
            change(page, "x1", [](core::Cell& c) {
                c.geometry()->setSourcePoint(core::Point{330, 20});
                c.geometry()->setTargetPoint(core::Point{330, 140});
            });
        });
    }

    void movingACollapsedContainerFollowsEdgesToItsHiddenCells()
    {
        expectEqualToBuild({"box"}, [](core::Page& page) {
            change(page, "box", [](core::Cell& c) { c.geometry()->setY(300); });
        });
    }

    void terminalsOfEdgesChange()
    {
        expectEqualToBuild({"e1"}, [](core::Page& page) {
            change(page, "e1", [](core::Cell& c) { c.setTarget("holder"); });
        });
    }

    void severalChangesAtOnce()
    {
        expectEqualToBuild({"a", "b", "g", "x1"}, [](core::Page& page) {
            change(page, "a", [](core::Cell& c) { c.geometry()->setWidth(200); });
            change(page, "b", [](core::Cell& c) { c.geometry()->setX(320); });
            change(page, "g", [](core::Cell& c) { c.geometry()->setHeight(160); });
            change(page, "x1", [](core::Cell& c) { c.setStyleText("endArrow=block;jumpStyle=gap;"); });
        });
    }

    void refusesWhatItCannotPatch()
    {
        // A cell that is not a state (a layer), an unknown id.
        expectRefused({"1"}, [](core::Page& page) { change(page, "1", [](core::Cell& c) { c.setValue("x"); }); });
        expectRefused({"nope"}, [](core::Page&) {});
        // Hidden, collapsed, expanded, no geometry any more, an edge turned into a vertex.
        expectRefused({"a"}, [](core::Page& page) { change(page, "a", [](core::Cell& c) { c.setVisible(false); }); });
        expectRefused({"g"}, [](core::Page& page) { change(page, "g", [](core::Cell& c) { c.setCollapsed(true); }); });
        expectRefused({"box"}, [](core::Page& page) { change(page, "box", [](core::Cell& c) { c.setCollapsed(false); }); });
        expectRefused({"b"}, [](core::Page& page) {
            change(page, "b", [](core::Cell& c) { c.setGeometry(std::nullopt); });
        });
        expectRefused({"e1"}, [](core::Page& page) {
            change(page, "e1", [](core::Cell& c) {
                c.setEdge(false);
                c.setVertex(true);
            });
        });
    }

    void refusesWhenAnAttachedCellIsNotDrawn()
    {
        // The edge has one terminal and no points: it is not drawn, and a change to the
        // terminal might make it drawable, which an update cannot add to the view.
        core::Document doc = makeDocument();
        core::Page& page = *doc.page(0);
        PageView view = PageView::build(page, context());
        QVERIFY(view.find("dead") == nullptr);
        change(page, "lonely", [](core::Cell& c) { c.geometry()->setX(720); });
        QVERIFY(!view.update(page, context(), {"lonely"}).has_value());
        // A hidden one is legitimately absent.
        change(page, "dead", [](core::Cell& c) { c.setVisible(false); });
        PageView again = PageView::build(page, context());
        change(page, "lonely", [](core::Cell& c) { c.geometry()->setX(730); });
        QVERIFY(again.update(page, context(), {"lonely"}).has_value());
    }

    void refusesAChangedGridSize()
    {
        core::Document doc = makeDocument();
        core::Page& page = *doc.page(0);
        PageView view = PageView::build(page, context());
        page.setGridSize(20);
        QVERIFY(!view.update(page, context(), {"a"}).has_value());
    }

    void aCycleOfEdgesIsRefused()
    {
        // Two edges attached to each other: the order of resolution decides what they look like,
        // and a partial update would not reproduce the build's.
        core::Document doc = makeDocument();
        core::Page& page = *doc.page(0);
        change(page, "x1", [](core::Cell& c) { c.setSource("x2"); });
        change(page, "x2", [](core::Cell& c) { c.setTarget("x1"); });
        PageView view = PageView::build(page, context());
        change(page, "x2", [](core::Cell& c) { c.setStyleText("endArrow=block;"); });
        QVERIFY(!view.update(page, context(), {"x2"}).has_value());
    }

    void updateCountsInTheStats()
    {
        core::Document doc = makeDocument();
        core::Page& page = *doc.page(0);
        PageView view = PageView::build(page, context());
        const auto before = render::stats::get(render::stats::Counter::pageViewUpdates);
        change(page, "a", [](core::Cell& c) { c.geometry()->setX(60); });
        QVERIFY(view.update(page, context(), {"a"}).has_value());
        QCOMPARE(render::stats::get(render::stats::Counter::pageViewUpdates), before + 1);
    }
};

QTEST_MAIN(PageViewUpdateTest)
#include "tst_page_view_update.moc"
