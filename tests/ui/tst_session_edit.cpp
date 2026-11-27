// the model side of the Edit and Arrange menus.

#include "support/test_support.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_pages.h"

#include <QClipboard>
#include <QMimeData>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;

namespace {

core::Rect boundsOf(const DocumentSession& session, const std::string& id)
{
    return session.currentPage()->findCell(id)->geometry()->bounds();
}

// One undo restores exactly what was saved before the action (ARCHITECTURE.md: undo identity).
#define VERIFY_UNDO_RESTORES(session, before)                                                                          \
    do {                                                                                                               \
        QVERIFY((session).undo());                                                                                     \
        QCOMPARE(test::savedBytes(session), (before));                                                                 \
    } while (false)

const char* kThreeInARow =
    R"(<mxfile><diagram id="p" name="P"><mxGraphModel gridSize="10" grid="1"><root><mxCell id="0"/><mxCell id="1" parent="0"/><mxCell id="v1" vertex="1" parent="1"><mxGeometry x="0" y="0" width="40" height="40" as="geometry"/></mxCell><mxCell id="v2" vertex="1" parent="1"><mxGeometry x="60" y="0" width="40" height="40" as="geometry"/></mxCell><mxCell id="v3" vertex="1" parent="1"><mxGeometry x="360" y="0" width="40" height="40" as="geometry"/></mxCell></root></mxGraphModel></diagram></mxfile>)";

} // namespace

class TestSessionEdit : public QObject {
    Q_OBJECT

private slots:
    // --- clipboard -------------------------------------------------------------

    void pastingTwiceOffsetsByTheGridSizeEachTime()
    {
        DocumentSession session(test::simpleDocument());
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"a"});
        QVERIFY(actions::copySelection(session, *QGuiApplication::clipboard()));
        const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
        QVERIFY(mime->hasFormat(QString::fromLatin1(kCellsMimeType)));

        QVERIFY(actions::paste(session, *mime));
        const std::string first = session.selection().first();
        QVERIFY(first != "a");
        QCOMPARE(boundsOf(session, first), (core::Rect{50, 50, 120, 60}));
        QVERIFY(actions::paste(session, *mime));
        const std::string second = session.selection().first();
        QCOMPARE(boundsOf(session, second), (core::Rect{60, 60, 120, 60}));
        QCOMPARE(session.undoText(), QStringLiteral("Paste"));

        QVERIFY(session.undo());
        QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), before);
    }

    void pasteAfterCutLandsInPlace()
    {
        DocumentSession session(test::simpleDocument());
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"b"});
        QVERIFY(actions::cutSelection(session, *QGuiApplication::clipboard()));
        QCOMPARE(session.undoText(), QStringLiteral("Cut"));
        QVERIFY(session.currentPage()->findCell("b") == nullptr);
        const core::Cell* edge = session.currentPage()->findCell("e");
        QVERIFY(edge->target().empty()); // the edge was disconnected, not deleted

        QVERIFY(actions::paste(session, *QGuiApplication::clipboard()->mimeData()));
        QCOMPARE(boundsOf(session, session.selection().first()), (core::Rect{280, 40, 120, 60}));
        QVERIFY(session.undo());
        QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), before);
    }

    void pasteHereSnapsTheTopLeftToTheGrid()
    {
        DocumentSession session(test::simpleDocument());
        session.selection().set({"a", "b", "e"});
        QVERIFY(actions::copySelection(session, *QGuiApplication::clipboard()));
        QVERIFY(actions::paste(session, *QGuiApplication::clipboard()->mimeData(), QPointF(203, 97)));
        QCOMPARE(session.selection().size(), std::size_t(3));
        const core::Rect rect = boundsOf(session, session.selection().cells()[0]);
        QCOMPARE(rect.x, 200.0);
        QCOMPARE(rect.y, 100.0);
        QCOMPARE(boundsOf(session, session.selection().cells()[1]).x, 440.0); // 280 - 40 + 200
    }

    void pastedTextBecomesATextVertex()
    {
        DocumentSession session(test::simpleDocument());
        const QByteArray before = test::savedBytes(session);
        QMimeData mime;
        mime.setText(QStringLiteral("a <b>\nline"));
        QVERIFY(actions::paste(session, mime, QPointF(30, 40)));
        const core::Cell* text = session.currentPage()->findCell(session.selection().first());
        QCOMPARE(text->value(), std::string("a &lt;b&gt;<br>line"));
        QVERIFY(text->style().hasName("text"));
        QCOMPARE(text->geometry()->bounds(), (core::Rect{30, 40, 120, 40}));
        VERIFY_UNDO_RESTORES(session, before);

        QMimeData empty;
        QVERIFY(!actions::paste(session, empty));
    }

    // --- delete / duplicate -------------------------------------------------------

    void deleteKeepsOrRemovesConnectedEdges()
    {
        DocumentSession session(test::simpleDocument());
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"a"});
        QVERIFY(actions::deleteSelection(session, false));
        QVERIFY(session.currentPage()->findCell("a") == nullptr);
        QVERIFY(session.currentPage()->findCell("e") != nullptr);
        QVERIFY(session.currentPage()->findCell("e")->source().empty());
        QVERIFY(session.selection().isEmpty());
        VERIFY_UNDO_RESTORES(session, before);

        session.selection().set({"a"});
        QVERIFY(actions::deleteSelection(session, true));
        QVERIFY(session.currentPage()->findCell("e") == nullptr);
        VERIFY_UNDO_RESTORES(session, before);
    }

    void lockedCellsAreNotDeleted()
    {
        DocumentSession session(test::simpleDocument());
        session.selection().set({"a"});
        QVERIFY(actions::toggleLock(session));
        QVERIFY(!actions::deleteSelection(session, false));
        QVERIFY(session.currentPage()->findCell("a") != nullptr);
        QVERIFY(actions::duplicateSelection(session)); // duplicating a locked cell is allowed
    }

    void duplicateClonesIntoTheSameParent()
    {
        DocumentSession session(test::simpleDocument());
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"c1"});
        QVERIFY(actions::duplicateSelection(session));
        const core::Cell* clone = session.currentPage()->findCell(session.selection().first());
        QVERIFY(clone->id() != "c1");
        QCOMPARE(clone->parent(), std::string("g"));
        QCOMPARE(clone->geometry()->bounds(), (core::Rect{10, 10, 80, 40})); // (0,0) + grid
        VERIFY_UNDO_RESTORES(session, before);

        session.selection().set({"g"});
        QVERIFY(actions::duplicateSelection(session));
        QCOMPARE(session.currentPage()->cellCount(), std::size_t(8 + 3)); // group and both children
        VERIFY_UNDO_RESTORES(session, before);
    }

    // --- selection ------------------------------------------------------------------

    void selectionScopes()
    {
        DocumentSession session(test::simpleDocument());
        actions::selectAll(session);
        QCOMPARE(session.selection().cells(), (std::vector<std::string>{"a", "b", "e", "g"}));
        actions::selectVertices(session);
        QCOMPARE(session.selection().cells(), (std::vector<std::string>{"a", "b", "g", "c1", "c2"}));
        actions::selectEdges(session);
        QCOMPARE(session.selection().cells(), (std::vector<std::string>{"e"}));
        actions::selectNone(session);
        QVERIFY(session.selection().isEmpty());

        session.enterGroup("g");
        actions::selectAll(session);
        QCOMPARE(session.selection().cells(), (std::vector<std::string>{"c1", "c2"}));
    }

    // --- arrange ----------------------------------------------------------------------

    void groupAndUngroup()
    {
        DocumentSession session(test::simpleDocument());
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"a", "b"});
        QVERIFY(actions::group(session));
        QCOMPARE(session.selection().size(), std::size_t(1));
        const std::string groupId = session.selection().first();
        QCOMPARE(session.currentPage()->findCell("a")->parent(), groupId);
        QCOMPARE(boundsOf(session, groupId), (core::Rect{40, 40, 360, 60}));
        QCOMPARE(boundsOf(session, "a").x, 0.0);

        QVERIFY(actions::ungroup(session));
        // the edge between them moved into the group on grouping and comes out with them
        QCOMPARE(session.selection().cells(), (std::vector<std::string>{"a", "b", "e"}));
        QCOMPARE(boundsOf(session, "a").x, 40.0);
        QVERIFY(session.currentPage()->findCell(groupId) == nullptr);
        QVERIFY(session.undo());
        QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), before);
    }

    void removeFromGroupMovesTheCellToTheLayer()
    {
        DocumentSession session(test::simpleDocument());
        session.selection().set({"c2"});
        QVERIFY(actions::removeFromGroup(session));
        const core::Cell* c2 = session.currentPage()->findCell("c2");
        QCOMPARE(c2->parent(), std::string("1"));
        QCOMPARE(c2->geometry()->bounds(), (core::Rect{160, 260, 80, 40}));
        session.selection().set({"a"});
        QVERIFY(!actions::removeFromGroup(session)); // already in the layer
    }

    void zOrder()
    {
        DocumentSession session(test::simpleDocument());
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"a"});
        QVERIFY(actions::order(session, commands::ops::ZOrder::ToFront));
        QVERIFY(*session.currentPage()->zIndex("a") > *session.currentPage()->zIndex("g"));
        QCOMPARE(session.undoText(), QStringLiteral("To Front"));
        VERIFY_UNDO_RESTORES(session, before);
        session.selection().set({"g"});
        QVERIFY(actions::order(session, commands::ops::ZOrder::ToBack));
        QCOMPARE(session.undoText(), QStringLiteral("To Back"));
    }

    void alignLeftUsesTheMinimumAbsoluteLeft()
    {
        DocumentSession session(test::simpleDocument());
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"b", "c2"}); // 280 and 40 + 120 (inside the group)
        QVERIFY(actions::align(session, actions::Align::Left));
        QCOMPARE(boundsOf(session, "b").x, 160.0);
        QCOMPARE(boundsOf(session, "c2").x, 120.0); // unchanged
        VERIFY_UNDO_RESTORES(session, before);

        session.selection().set({"a", "c2"}); // min left is "a" at 40 -> c2 at 40 - 40 in the group
        QVERIFY(actions::align(session, actions::Align::Left));
        QCOMPARE(boundsOf(session, "c2").x, 0.0);
        QCOMPARE(session.currentPage()->findCell("e")->source(), std::string("a")); // edges stay connected
    }

    void alignCenterUsesTheFirstSelectedCell()
    {
        DocumentSession session(test::simpleDocument());
        session.selection().set({"a", "b"});
        QVERIFY(actions::align(session, actions::Align::Center));
        QCOMPARE(boundsOf(session, "b").x, 40.0); // centre 100 of "a"
        QCOMPARE(boundsOf(session, "a").x, 40.0);
        QVERIFY(!actions::align(session, actions::Align::Center)); // nothing left to do

        session.selection().set({"a"});
        QVERIFY(!actions::align(session, actions::Align::Left)); // needs two cells
    }

    void alignRightAndBottom()
    {
        DocumentSession session(test::simpleDocument());
        session.selection().set({"a", "b"});
        QVERIFY(actions::align(session, actions::Align::Right));
        QCOMPARE(boundsOf(session, "a").x, 280.0);
        session.selection().set({"a", "g"});
        QVERIFY(actions::align(session, actions::Align::Bottom));
        QCOMPARE(boundsOf(session, "a").y, 240.0); // bottom of the group at 300
    }

    void distributeSpacesCentresEvenly()
    {
        DocumentSession session(test::documentFromXml(kThreeInARow));
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"v3", "v1", "v2"});
        QVERIFY(actions::distribute(session, true));
        QCOMPARE(boundsOf(session, "v1").x, 0.0);
        QCOMPARE(boundsOf(session, "v3").x, 360.0);
        QCOMPARE(boundsOf(session, "v2").x, 180.0); // centres 20, 200, 380
        VERIFY_UNDO_RESTORES(session, before);

        session.selection().set({"v1", "v2"});
        QVERIFY(!actions::distribute(session, true)); // needs three
        session.selection().set({"v1", "v2", "v3"});
        QVERIFY(!actions::distribute(session, false)); // equal y centres: nothing moves
    }

    void toggleLockWritesAllSevenKeys()
    {
        DocumentSession session(test::simpleDocument());
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"a"});
        QVERIFY(actions::toggleLock(session));
        const core::Style locked = session.currentPage()->findCell("a")->style();
        for (const char* key : {"movable", "resizable", "rotatable", "deletable", "editable", "connectable"})
            QCOMPARE(locked.value(key, "?"), std::string("0"));
        QCOMPARE(locked.value("locked", "?"), std::string("1"));
        QVERIFY(session.currentPage()->findCell("a")->style().has("rounded")); // other keys survive

        QVERIFY(actions::toggleLock(session)); // the first cell is locked now: unlock
        const core::Style unlocked = session.currentPage()->findCell("a")->style();
        QCOMPARE(unlocked.value("editable", "?"), std::string("1"));
        QCOMPARE(unlocked.value("locked", "?"), std::string("0"));
        QVERIFY(session.undo());
        QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), before);
    }

    void clearWaypointsRemovesTheBends()
    {
        DocumentSession session(test::simpleDocument());
        QVERIFY(session.edit(QStringLiteral("Bend"), [](commands::Transaction& tx) {
                       return commands::ops::setWaypoints(tx, 0, "e", {{200, 20}, {200, 120}});
                   }).ok);
        const QByteArray bent = test::savedBytes(session);
        session.selection().set({"e"});
        QVERIFY(actions::clearWaypoints(session));
        QVERIFY(session.currentPage()->findCell("e")->geometry()->points().empty());
        VERIFY_UNDO_RESTORES(session, bent);
        QVERIFY(actions::clearWaypoints(session));
        QVERIFY(!actions::clearWaypoints(session)); // nothing to clear any more
    }

    void copyAndPasteStyle()
    {
        DocumentSession session(test::simpleDocument());
        const QByteArray before = test::savedBytes(session);
        std::string buffer;
        session.selection().set({"b"});
        QVERIFY(actions::copyStyle(session, buffer));
        QCOMPARE(buffer, std::string("ellipse;whiteSpace=wrap;html=1;"));
        session.selection().set({"a", "c1"});
        QVERIFY(actions::pasteStyle(session, buffer));
        QCOMPARE(session.currentPage()->findCell("a")->styleText(), buffer);
        QCOMPARE(session.currentPage()->findCell("c1")->styleText(), buffer);
        VERIFY_UNDO_RESTORES(session, before);
        QVERIFY(!actions::pasteStyle(session, {}));
    }

    // --- insertion ----------------------------------------------------------------------

    void insertBundleCentresAndSnaps()
    {
        DocumentSession session(test::simpleDocument());
        const QByteArray before = test::savedBytes(session);
        const auto bundle = decodeCells(
            R"(<mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/><mxCell id="s" value="New" style="rounded=1;" vertex="1" parent="1"><mxGeometry x="5" y="7" width="100" height="60" as="geometry"/></mxCell></root></mxGraphModel>)");
        QVERIFY(bundle);
        QVERIFY(actions::insertBundle(session, *bundle, QPointF(307, 203), QStringLiteral("Insert Shape")));
        QCOMPARE(session.undoText(), QStringLiteral("Insert Shape"));
        QCOMPARE(boundsOf(session, session.selection().first()), (core::Rect{260, 170, 100, 60})); // 257,173 snapped
        VERIFY_UNDO_RESTORES(session, before);

        QVERIFY(!actions::insertBundle(session, CellBundle{}, QPointF(0, 0), QStringLiteral("Nothing")));
    }
};

QTEST_MAIN(TestSessionEdit)
#include "tst_session_edit.moc"
