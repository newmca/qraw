// Containers (docs/design/ui.md section 4): dropping dragged and palette
// cells into swimlanes, stack layouts, and the collapse/expand control.

#include "support/select_fixture.h"

#include "qraw/ui/palette_dock.h"

#include <QMimeData>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

namespace {

// Layer 1 with a free box, a plain swimlane and a vertical stack of two cards.
constexpr char kContainers[] = R"(<mxfile><diagram id="p1" name="Page-1"><mxGraphModel grid="1" gridSize="10" page="0"><root><mxCell id="0"/><mxCell id="1" parent="0"/><mxCell id="box" value="" style="rounded=0;" vertex="1" parent="1"><mxGeometry x="20" y="20" width="60" height="40" as="geometry"/></mxCell><mxCell id="lane" value="Lane" style="swimlane;startSize=30;" vertex="1" parent="1"><mxGeometry x="200" y="200" width="200" height="200" as="geometry"/></mxCell><mxCell id="stack" value="Stack" style="swimlane;startSize=30;childLayout=stackLayout;horizontalStack=0;resizeParent=1;resizeParentMax=0;" vertex="1" parent="1"><mxGeometry x="500" y="100" width="160" height="200" as="geometry"/></mxCell><mxCell id="s1" value="1" style="rounded=0;" vertex="1" parent="stack"><mxGeometry y="30" width="160" height="40" as="geometry"/></mxCell><mxCell id="s2" value="2" style="rounded=0;" vertex="1" parent="stack"><mxGeometry y="70" width="160" height="40" as="geometry"/></mxCell></root></mxGraphModel></diagram></mxfile>)";

struct ContainerFixture : SelectFixture {
    ContainerFixture()
    {
        session.resetDocument(documentFromXml(kContainers));
        original = savedBytes(session);
    }
    [[nodiscard]] std::string parentOf(const std::string& id) const { return cell(id).parent(); }
    [[nodiscard]] std::vector<std::string> childrenOf(const std::string& id) const
    {
        std::vector<std::string> ids;
        for (const core::Cell* child : session.currentPage()->children(id))
            ids.push_back(child->id());
        return ids;
    }
};

QMimeData* rectangleMime()
{
    const PaletteEntry* entry = PaletteLibrary::builtin().entry(QStringLiteral("general/rectangle"));
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kCellsMimeType), QByteArray::fromStdString(entry->cellsXml()));
    return mime;
}

} // namespace

class TestSelectContainers : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qraw::ui::test::isolateSettings(); }

    void dragIntoSwimlaneKeepsTheAbsolutePosition()
    {
        ContainerFixture f;
        drag(f.canvas(), {50, 40}, {300, 300});
        QCOMPARE(f.parentOf("box"), std::string("lane"));
        QCOMPARE(f.bounds("box"), QRectF(270, 280, 60, 40));
        QCOMPARE(f.geometry("box").x, 70.0); // lane coordinates
        QVERIFY(f.undoRestores(f.original));
        QVERIFY(!f.session.canUndo());
    }

    void dropPastTheEdgeGrowsTheContainer()
    {
        ContainerFixture f;
        drag(f.canvas(), {50, 40}, {400, 300}); // the box ends at x = 430, the lane at 400
        QCOMPARE(f.parentOf("box"), std::string("lane"));
        QCOMPARE(f.geometry("lane").width, 230.0);
        QVERIFY(f.undoRestores(f.original));
    }

    void draggingOutLeavesTheContainer()
    {
        ContainerFixture f;
        drag(f.canvas(), {50, 40}, {300, 300});
        QCOMPARE(f.parentOf("box"), std::string("lane"));
        const QByteArray inLane = savedBytes(f.session);
        f.session.selection().set({"box"});
        drag(f.canvas(), {300, 300}, {100, 500});
        QCOMPARE(f.parentOf("box"), std::string("1"));
        QCOMPARE(f.bounds("box"), QRectF(70, 480, 60, 40)); // moved by (-200, +200) from (270, 280)
        QVERIFY(f.undoRestores(inLane));
    }

    void altDragDoesNotDrop()
    {
        ContainerFixture f;
        drag(f.canvas(), {50, 40}, {300, 300}, Qt::AltModifier);
        QCOMPARE(f.parentOf("box"), std::string("1"));
    }

    void aContainerIsNotDroppedIntoItself()
    {
        ContainerFixture f;
        f.session.selection().set({"lane"});
        drag(f.canvas(), {300, 360}, {310, 370});
        QCOMPARE(f.parentOf("lane"), std::string("1"));
    }

    void ctrlDragCopiesIntoTheContainer()
    {
        ContainerFixture f;
        drag(f.canvas(), {50, 40}, {300, 300}, Qt::ControlModifier);
        QCOMPARE(f.parentOf("box"), std::string("1"));
        const std::vector<std::string> kids = f.childrenOf("lane");
        QCOMPARE(kids.size(), std::size_t(1));
        QVERIFY(f.undoRestores(f.original));
    }

    void draggingACardWithinAStackReordersIt()
    {
        ContainerFixture f;
        f.session.selection().set({"s1"});
        drag(f.canvas(), {580, 150}, {580, 215});
        QCOMPARE(f.childrenOf("stack"), (std::vector<std::string>{"s2", "s1"}));
        QCOMPARE(f.geometry("s2").y, 30.0);
        QCOMPARE(f.geometry("s1").y, 70.0);
        QVERIFY(f.undoRestores(f.original));
        QVERIFY(!f.session.canUndo());
    }

    void paletteDropIntoAStackAppendsAndStacks()
    {
        ContainerFixture f;
        AppSettings settings;
        PaletteDock dock(PaletteLibrary::builtin(), f.session, f.canvas(), settings);
        std::unique_ptr<QMimeData> mime(rectangleMime());
        QVERIFY(dock.drop(*mime, QPointF(580, 280)));
        const std::vector<std::string> kids = f.childrenOf("stack");
        QCOMPARE(kids.size(), std::size_t(3));
        QCOMPARE(kids[0], std::string("s1"));
        QCOMPARE(kids[1], std::string("s2"));
        const core::Rect added = f.geometry(kids[2]);
        QCOMPARE(added.x, 0.0);
        QCOMPARE(added.y, 110.0);     // below s2 (70 + 40)
        QCOMPARE(added.width, 160.0); // fills the stack
        QCOMPARE(f.geometry("stack").height, 110.0 + added.height); // resizeParentMax=0
        QVERIFY(f.undoRestores(f.original));
        QVERIFY(!f.session.canUndo());
    }

    void paletteDropBetweenCardsChoosesThePosition()
    {
        ContainerFixture f;
        AppSettings settings;
        PaletteDock dock(PaletteLibrary::builtin(), f.session, f.canvas(), settings);
        std::unique_ptr<QMimeData> mime(rectangleMime());
        QVERIFY(dock.drop(*mime, QPointF(580, 150))); // 50 below the top: before s2 (centre 90)
        const std::vector<std::string> kids = f.childrenOf("stack");
        QCOMPARE(kids.size(), std::size_t(3));
        QCOMPARE(kids[0], std::string("s1"));
        QCOMPARE(kids[2], std::string("s2"));
        QCOMPARE(f.geometry("s2").y, 70.0 + f.geometry(kids[1]).height);
        QVERIFY(f.undoRestores(f.original));
    }

    void foldControlCollapsesAndExpands()
    {
        ContainerFixture f;
        const QPointF control(209, 209); // 9 units from the lane's top-left corner
        click(f.canvas(), control);
        QVERIFY(f.cell("lane").isCollapsed());
        QCOMPARE(f.geometry("lane").height, 30.0);
        QVERIFY(f.selected().empty()); // the control is not a selection click
        QVERIFY(f.undoRestores(f.original));
        QVERIFY(!f.cell("lane").isCollapsed());

        click(f.canvas(), control);
        QVERIFY(f.cell("lane").isCollapsed());
        click(f.canvas(), control);
        QVERIFY(!f.cell("lane").isCollapsed());
        QCOMPARE(f.geometry("lane").height, 200.0);
    }

    void collapsedContainersDoNotTakeDrops()
    {
        ContainerFixture f;
        click(f.canvas(), {209, 209});
        QVERIFY(f.cell("lane").isCollapsed());
        drag(f.canvas(), {50, 40}, {300, 215}); // onto the collapsed title bar
        QCOMPARE(f.parentOf("box"), std::string("1"));
    }
};

QTEST_MAIN(TestSelectContainers)
#include "tst_select_containers.moc"
