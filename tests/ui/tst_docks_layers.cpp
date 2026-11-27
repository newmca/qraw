// Tests of LayersPanel: topmost-first rows, add/rename/visibility/lock/delete,
// and moving the selection into another layer (all undoable).

#include "support/test_support.h"

#include "qraw/ui/layers_panel.h"

#include <QTreeWidget>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

namespace {

// Column numbers of the layers tree.
constexpr int kVisible = 0;
constexpr int kLocked = 1;
constexpr int kName = 2;

QString cellParent(const DocumentSession& session, const char* id)
{
    return QString::fromStdString(session.document().page(0)->findCell(id)->parent());
}

} // namespace

class TestDocksLayers : public QObject {
    Q_OBJECT

private slots:
    void listsTopmostFirst();
    void renamesHidesAndLocks();
    void checkboxClickChangesVisibility();
    void movesSelectionToANewLayer();
    void deletesALayer();
};

void TestDocksLayers::listsTopmostFirst()
{
    DocumentSession session(simpleDocument());
    LayersPanel panel(session);
    QTreeWidget* list = panel.list();
    QCOMPARE(list->topLevelItemCount(), 1);

    QVERIFY(panel.addLayer());
    QCOMPARE(list->topLevelItemCount(), 2);
    QCOMPARE(list->topLevelItem(0)->text(kName), QStringLiteral("Untitled Layer"));
    QCOMPARE(list->topLevelItem(1)->text(kName), QStringLiteral("Background"));
    // The new layer is the current one and the topmost row.
    const auto layers = session.document().page(0)->layers();
    QCOMPARE(QString::fromStdString(session.currentLayer()), QString::fromStdString(layers.back()->id()));
    QCOMPARE(int(layers.size()), 2);
    grabToPng(panel, QStringLiteral("docks-layers"));
}

void TestDocksLayers::renamesHidesAndLocks()
{
    DocumentSession session(simpleDocument());
    LayersPanel panel(session);
    QTreeWidget* list = panel.list();
    const QByteArray before = savedBytes(session);

    QVERIFY(panel.renameLayer("1", QStringLiteral("Base")));
    QCOMPARE(list->topLevelItem(0)->text(kName), QStringLiteral("Base"));

    QVERIFY(panel.setLayerVisible("1", false));
    QCOMPARE(list->topLevelItem(0)->checkState(kVisible), Qt::Unchecked);

    QVERIFY(panel.setLayerLocked("1", true));
    QCOMPARE(list->topLevelItem(0)->checkState(kLocked), Qt::Checked);

    while (session.canUndo())
        QVERIFY(session.undo());
    QCOMPARE(savedBytes(session), before);
}

void TestDocksLayers::checkboxClickChangesVisibility()
{
    DocumentSession session(simpleDocument());
    LayersPanel panel(session);
    QTreeWidget* list = panel.list();

    list->topLevelItem(0)->setCheckState(kVisible, Qt::Unchecked);
    QVERIFY(!session.document().page(0)->findCell("1")->isVisible());
    QCOMPARE(session.undoText(), QStringLiteral("Hide Layer"));
}

void TestDocksLayers::movesSelectionToANewLayer()
{
    DocumentSession session(simpleDocument());
    LayersPanel panel(session);
    const QByteArray before = savedBytes(session);

    QVERIFY(panel.addLayer());
    const std::string top = session.currentLayer();
    session.selection().set({"a", "b"});
    QVERIFY(panel.moveSelectionToLayer(top));
    QCOMPARE(cellParent(session, "a"), QString::fromStdString(top));
    QCOMPARE(cellParent(session, "b"), QString::fromStdString(top));

    QVERIFY(session.undo());
    QCOMPARE(cellParent(session, "a"), QStringLiteral("1"));
    QVERIFY(session.undo());
    QCOMPARE(savedBytes(session), before);
}

void TestDocksLayers::deletesALayer()
{
    DocumentSession session(simpleDocument());
    LayersPanel panel(session);
    QVERIFY(panel.addLayer());
    const std::string top = session.currentLayer();

    QVERIFY(panel.removeLayer(top));
    QCOMPARE(panel.list()->topLevelItemCount(), 1);
    QCOMPARE(QString::fromStdString(session.currentLayer()), QStringLiteral("1"));
}

QTEST_MAIN(TestDocksLayers)
#include "tst_docks_layers.moc"
