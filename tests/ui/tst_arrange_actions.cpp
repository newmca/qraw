// Arrange: Flip Horizontal / Vertical, Rotate 90 Degrees, Autosize, Edit Geometry and the
// Layout submenu actions (edit_actions.h). Each action is one undo step that restores
// the document byte for byte (ARCHITECTURE.md undo identity).
#include "support/test_support.h"
#include "qraw/ui/edit_actions.h"
#include "qraw/ui/edit_geometry_dialog.h"

#include <QDoubleSpinBox>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;

namespace {

// a: 40x20 at (10,20) with a label; b: 40x20 at (200,20); e: a free edge a -> b with an exit constraint.
const char* kArrangeDiagram =
    R"(<mxfile><diagram id="p" name="P"><mxGraphModel gridSize="10" grid="1"><root><mxCell id="0"/><mxCell id="1" parent="0"/>)"
    R"(<mxCell id="a" value="A" style="rounded=0;" vertex="1" parent="1"><mxGeometry x="10" y="20" width="40" height="20" as="geometry"/></mxCell>)"
    R"(<mxCell id="b" value="B" style="rounded=0;" vertex="1" parent="1"><mxGeometry x="200" y="20" width="40" height="20" as="geometry"/></mxCell>)"
    R"(<mxCell id="e" style="exitX=1;exitY=0.5;entryX=0;entryY=0.5;" edge="1" parent="1" source="a" target="b"><mxGeometry relative="1" as="geometry"><mxPoint x="50" y="200" as="sourcePoint"/><mxPoint x="190" y="200" as="targetPoint"/><Array as="points"><mxPoint x="60" y="120"/><mxPoint x="120" y="80"/></Array></mxGeometry></mxCell>)"
    R"(</root></mxGraphModel></diagram></mxfile>)";

core::Rect boundsOf(const DocumentSession& session, const char* id)
{
    return session.currentPage()->findCell(id)->geometry()->bounds();
}

QString styleOf(const DocumentSession& session, const char* id, const char* key)
{
    return QString::fromStdString(session.currentPage()->findCell(id)->style().value(key, "<none>"));
}

#define VERIFY_UNDO_RESTORES(session, before)                                                                          \
    do {                                                                                                               \
        QVERIFY((session).undo());                                                                                     \
        QCOMPARE(test::savedBytes(session), (before));                                                                 \
    } while (false)

} // namespace

class TstArrangeActions : public QObject {
    Q_OBJECT

private slots:
    void flipTogglesFromTheFirstCellAndUndoes()
    {
        DocumentSession session(test::documentFromXml(kArrangeDiagram));
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"a", "b"});
        QVERIFY(actions::flip(session, actions::Flip::Horizontal));
        QCOMPARE(styleOf(session, "a", "flipH"), QStringLiteral("1"));
        QCOMPARE(styleOf(session, "b", "flipH"), QStringLiteral("1"));
        QCOMPARE(styleOf(session, "a", "flipV"), QStringLiteral("<none>"));
        QCOMPARE(session.undoText(), QStringLiteral("Flip Horizontal"));

        // The first cell is now flipped, so the next flip turns both back off.
        QVERIFY(actions::flip(session, actions::Flip::Horizontal));
        QCOMPARE(styleOf(session, "a", "flipH"), QStringLiteral("0"));
        QCOMPARE(styleOf(session, "b", "flipH"), QStringLiteral("0"));
        QVERIFY(session.undo());
        QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), before);
    }

    void flipVerticalIsIndependentOfHorizontal()
    {
        DocumentSession session(test::documentFromXml(kArrangeDiagram));
        session.selection().set({"a"});
        QVERIFY(actions::flip(session, actions::Flip::Vertical));
        QCOMPARE(styleOf(session, "a", "flipV"), QStringLiteral("1"));
        QCOMPARE(styleOf(session, "a", "flipH"), QStringLiteral("<none>"));
        QCOMPARE(session.undoText(), QStringLiteral("Flip Vertical"));
    }

    void turnRotatesAVertexAboutItsCentreAndAdvancesDirection()
    {
        DocumentSession session(test::documentFromXml(kArrangeDiagram));
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"a"});
        // Centre (30,30) stays; 40x20 becomes 20x40 at (20,10).
        QVERIFY(actions::turn(session));
        QCOMPARE(boundsOf(session, "a"), (core::Rect{20, 10, 20, 40}));
        QCOMPARE(styleOf(session, "a", "direction"), QStringLiteral("south"));
        QCOMPARE(session.undoText(), QStringLiteral("Rotate 90 Degrees"));
        VERIFY_UNDO_RESTORES(session, before);

        QVERIFY(actions::turn(session, true));
        QCOMPARE(styleOf(session, "a", "direction"), QStringLiteral("north"));
    }

    void turnReversesAnEdgeWithItsConstraintsAndWaypoints()
    {
        DocumentSession session(test::documentFromXml(kArrangeDiagram));
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"e"});
        QVERIFY(actions::turn(session));
        const core::Cell* edge = session.currentPage()->findCell("e");
        QCOMPARE(QString::fromStdString(edge->source()), QStringLiteral("b"));
        QCOMPARE(QString::fromStdString(edge->target()), QStringLiteral("a"));
        QCOMPARE(styleOf(session, "e", "exitX"), QStringLiteral("0"));
        QCOMPARE(styleOf(session, "e", "entryX"), QStringLiteral("1"));
        const core::Geometry* geo = edge->geometry();
        QCOMPARE(*geo->sourcePoint(), (core::Point{190, 200}));
        QCOMPARE(*geo->targetPoint(), (core::Point{50, 200}));
        QCOMPARE(geo->points(), (std::vector<core::Point>{{120, 80}, {60, 120}}));
        VERIFY_UNDO_RESTORES(session, before);
    }

    void autosizeShrinksAVertexWithAShortLabel()
    {
        DocumentSession session(test::documentFromXml(kArrangeDiagram));
        const QByteArray before = test::savedBytes(session);
        // A one-letter label in a 40x20 box: the preferred width is smaller, so the box narrows.
        session.selection().set({"a"});
        QVERIFY(actions::autosize(session));
        QVERIFY(boundsOf(session, "a").width < 40);
        QCOMPARE(session.undoText(), QStringLiteral("Autosize"));
        VERIFY_UNDO_RESTORES(session, before);
    }

    void autosizeGrowsAVertexWithALongLabel()
    {
        const char* xml =
            R"(<mxfile><diagram id="p" name="P"><mxGraphModel gridSize="10" grid="1"><root><mxCell id="0"/><mxCell id="1" parent="0"/>)"
            R"(<mxCell id="v" value="A considerably longer label than the box" style="rounded=0;" vertex="1" parent="1"><mxGeometry x="10" y="20" width="40" height="20" as="geometry"/></mxCell>)"
            R"(</root></mxGraphModel></diagram></mxfile>)";
        DocumentSession session(test::documentFromXml(xml));
        session.selection().set({"v"});
        QVERIFY(actions::autosize(session));
        QVERIFY(boundsOf(session, "v").width > 40);
    }

    void autosizeNeedsAVertexSelected()
    {
        DocumentSession session(test::documentFromXml(kArrangeDiagram));
        session.selection().set({"e"});
        QVERIFY(!actions::autosize(session));
    }

    void editGeometryAppliesToTwoVerticesInOneUndoStep()
    {
        DocumentSession session(test::documentFromXml(kArrangeDiagram));
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"a", "b"});
        QVERIFY(actions::canEditGeometry(session));
        QCOMPARE(actions::selectedVertexBounds(session).size(), std::size_t{2});

        actions::GeometryEdit edit;
        edit.x = 100;
        edit.width = 60;
        QVERIFY(actions::editGeometry(session, edit));
        QCOMPARE(boundsOf(session, "a"), (core::Rect{100, 20, 60, 20}));
        QCOMPARE(boundsOf(session, "b"), (core::Rect{100, 20, 60, 20}));
        QCOMPARE(session.undoText(), QStringLiteral("Edit Geometry"));
        VERIFY_UNDO_RESTORES(session, before);
    }

    void editGeometryNeedsAVertexSelected()
    {
        DocumentSession session(test::documentFromXml(kArrangeDiagram));
        session.selection().set({"e"});
        QVERIFY(!actions::canEditGeometry(session));
        session.selection().clear();
        QVERIFY(!actions::canEditGeometry(session));
    }

    void dialogShowsMixedValuesBlankAndReportsOnlyChangedFields()
    {
        // Left differs between the two vertices, Top and Width are shared.
        const std::vector<core::Rect> bounds{{10, 20, 40, 20}, {100, 20, 40, 20}};
        EditGeometryDialog dialog(bounds);
        auto* left = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("geometry.left"));
        auto* top = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("geometry.top"));
        QVERIFY(left != nullptr && top != nullptr);
        QCOMPARE(left->value(), left->minimum());
        QCOMPARE(top->value(), 20.0);
        QVERIFY(!dialog.edit().x && !dialog.edit().y);

        left->setValue(55);
        const actions::GeometryEdit edit = dialog.edit();
        QVERIFY(edit.x && *edit.x == 55);
        QVERIFY(!edit.y && !edit.width && !edit.height);
    }

    // --- Arrange > Layout ------------------------------------------------------------

    void layoutArrangesTheTreeOfTheSelectedRootAsOneUndoStep()
    {
        // a -> b through the free edge e.
        DocumentSession session(test::documentFromXml(kArrangeDiagram));
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"a"});
        QVERIFY(actions::arrangeLayout(session, commands::ops::LayoutKind::VerticalTree));
        QCOMPARE(boundsOf(session, "a"), (core::Rect{10, 20, 40, 20})); // the root stays
        QCOMPARE(boundsOf(session, "b").y, 70.0);                       // below it, levelDistance 30
        QVERIFY(session.currentPage()->findCell("e")->geometry()->points().empty());
        QCOMPARE(session.undoText(), QStringLiteral("Vertical Tree Layout"));
        VERIFY_UNDO_RESTORES(session, before);
    }

    void layoutWithoutATreeShowsAMessageAndChangesNothing()
    {
        DocumentSession session(test::documentFromXml(kArrangeDiagram));
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"b"}); // b has no outgoing edge
        QSignalSpy messages(&session, &DocumentSession::statusMessage);
        QVERIFY(!actions::arrangeLayout(session, commands::ops::LayoutKind::RadialTree));
        QCOMPARE(messages.count(), 1);
        QCOMPARE(test::savedBytes(session), before);
        QVERIFY(!session.canUndo());
    }

    void circleLayoutOfTheWholePageNeedsNoSelectionAndIsDisabledForEdgesOnly()
    {
        DocumentSession session(test::documentFromXml(kArrangeDiagram));
        QVERIFY(actions::canArrangeLayout(session));
        QVERIFY(actions::arrangeLayout(session, commands::ops::LayoutKind::Circle));
        QCOMPARE(styleOf(session, "e", "noEdgeStyle"), QStringLiteral("1"));
        session.selection().set({"e"});
        QVERIFY(!actions::canArrangeLayout(session));
    }

    void flowAndOrganicLayoutsAreOneUndoStepEach()
    {
        using commands::ops::LayoutKind;
        const struct {
            LayoutKind kind;
            const char* undoText;
        } cases[] = {{LayoutKind::HorizontalFlow, "Horizontal Flow Layout"},
                     {LayoutKind::VerticalFlow, "Vertical Flow Layout"},
                     {LayoutKind::Organic, "Organic Layout"}};
        for (const auto& c : cases) {
            DocumentSession session(test::documentFromXml(kArrangeDiagram));
            const QByteArray before = test::savedBytes(session);
            QVERIFY(actions::arrangeLayout(session, c.kind));
            QCOMPARE(session.undoText(), QString::fromLatin1(c.undoText));
            VERIFY_UNDO_RESTORES(session, before);
        }
    }

};

QTEST_MAIN(TstArrangeActions)
#include "tst_arrange_actions.moc"
