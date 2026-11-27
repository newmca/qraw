// Behaviour of the M4 editor driven through the real MainWindow with
// synthetic input (offscreen): shortcuts versus text fields, pending label
// edits, clipboard input, gestures under zoom and rotation, cell flags,
// failed opens. Found by the M4 behaviour review; each test pins a
// behaviour that a defect had broken or that the review confirmed.

#include "support/shell_env.h"
#include "support/test_support.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_pages.h"
#include "qraw/ui/main_window.h"
#include "qraw/ui/page_tabs.h"
#include "qraw/ui/layers_panel.h"
#include "qraw/ui/format_panel.h"
#include "qraw/ui/find_panel.h"
#include "qraw/ui/palette_dock.h"
#include "../../src/ui/format_edit.h"
#include <QAbstractSpinBox>
#include <QLineEdit>
#include <QTimer>
#include "qraw/ui/palette_library.h"
#include <QTextEdit>

#include <QAction>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QImage>
#include <QMimeData>
#include <QTemporaryDir>

#include <map>
#include <set>

using namespace qraw;
using namespace qraw::ui;
namespace id = qraw::ui::action_id;

namespace {

class FakePrompts final : public DocumentPrompts {
public:
    SaveChoice askSaveChanges(const QString&) override { ++saveAsked; return choice; }
    std::optional<QString> askOpenPath(const QString&) override { return std::nullopt; }
    std::optional<QString> askSavePath(const QString&, const QString&) override { return savePath; }
    void showError(const QString& title, const QString& message) override { errors << title + ": " + message; }
    QString savePath;
    QStringList errors;
    SaveChoice choice = SaveChoice::Discard;
    int saveAsked = 0;
};

QImage renderScene(DiagramScene& scene, const QRectF& area)
{
    QImage image(int(area.width()), int(area.height()), QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    scene.render(&painter, QRectF(0, 0, area.width(), area.height()), area);
    return image;
}

// The live scene must paint exactly what a scene built from the saved bytes paints.
bool sceneIsFresh(CanvasView& canvas, DocumentSession& session)
{
    DocumentSession fresh(test::documentFromXml(std::string_view(test::savedBytes(session).constData(),
                                                                  size_t(test::savedBytes(session).size()))));
    fresh.setCurrentPage(session.currentPageIndex());
    fresh.setDiagramDark(session.diagramDark());
    DiagramScene scene(fresh);
    const QRectF area(-100, -100, 900, 700);
    return renderScene(canvas.diagramScene(), area) == renderScene(scene, area);
}

// One vertex "v" whose label is `value` (html=1 unless it is a plain label).
std::string labelDiagram(const QString& value)
{
    const bool html = value.contains(QLatin1Char('<')) || value.contains(QLatin1Char('&'));
    const QString style = QStringLiteral("rounded=0;whiteSpace=wrap;") + (html ? QStringLiteral("html=1;") : QString());
    QString escaped = value.toHtmlEscaped();
    escaped.replace(QLatin1Char('\n'), QStringLiteral("&#xa;"));
    const QString xml = QStringLiteral("<mxfile><diagram id=\"p\" name=\"P\"><mxGraphModel grid=\"1\" gridSize=\"10\"><root>"
                                       "<mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
                                       "<mxCell id=\"v\" value=\"%1\" style=\"%2\" vertex=\"1\" parent=\"1\">"
                                       "<mxGeometry x=\"40\" y=\"40\" width=\"160\" height=\"80\" as=\"geometry\"/>"
                                       "</mxCell></root></mxGraphModel></diagram></mxfile>")
                            .arg(escaped, style);
    return xml.toStdString();
}

} // namespace

class TestEditorBehaviour : public QObject {
    Q_OBJECT

private slots:
    void shortcutsAreUnambiguous()
    {
        MainWindow window;
        std::map<QString, QStringList> byKey;
        for (const ActionInfo& info : window.actions().actions())
            for (const QKeySequence& k : info.action->shortcuts())
                byKey[k.toString()] << info.id;
        for (const auto& [key, ids] : byKey)
            QVERIFY2(ids.size() == 1, qPrintable(key + " -> " + ids.join(',')));
    }

    void sceneStaysFreshThroughOperations()
    {
        FakePrompts prompts;
        MainWindow window;
        window.setPrompts(&prompts);
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        CanvasView& canvas = window.canvas();
        canvas.setZoom(1);
        auto step = [&](const char* name, auto&& fn) {
            fn();
            QVERIFY2(sceneIsFresh(canvas, s), name);
        };
        step("select a", [&] { s.selection().set({"a"}); });
        step("move", [&] { QVERIFY(s.edit("m", [](commands::Transaction& tx) { return commands::ops::moveCells(tx, 0, {"a"}, 30, 20); })); });
        step("undo", [&] { QVERIFY(s.undo()); });
        step("redo", [&] { QVERIFY(s.redo()); });
        step("delete", [&] { QVERIFY(actions::deleteSelection(s, true)); });
        step("undo delete", [&] { QVERIFY(s.undo()); });
        step("group", [&] { s.selection().set({"a", "b"}); QVERIFY(actions::group(s)); });
        step("undo group", [&] { QVERIFY(s.undo()); });
        step("ungroup", [&] { s.selection().set({"g"}); QVERIFY(actions::ungroup(s)); });
        step("undo ungroup", [&] { QVERIFY(s.undo()); });
        step("dup", [&] { s.selection().set({"a", "e", "b"}); QVERIFY(actions::duplicateSelection(s)); });
        step("undo dup", [&] { QVERIFY(s.undo()); });
        step("front", [&] { s.selection().set({"a"}); QVERIFY(actions::order(s, commands::ops::ZOrder::ToFront)); });
        step("undo front", [&] { QVERIFY(s.undo()); });
    }

    // ---- label editing -----------------------------------------------------
    void unchangedLabelCommitKeepsTheValue_data()
    {
        QTest::addColumn<QString>("value");
        QTest::newRow("plain") << "Hello";
        QTest::newRow("html-basic") << "<b>bold</b> &amp; text<br>second line";
        QTest::newRow("html-table") << "<table><tr><td>a</td><td>b</td></tr></table>";
        QTest::newRow("html-div") << "<div style=\"text-align: left;\">left</div><div>x &lt; y</div>";
        QTest::newRow("entities") << "a &amp;nbsp; b &lt;i&gt;";
        QTest::newRow("html-p") << "<p>para</p><ul><li>one</li><li>two</li></ul>";
        QTest::newRow("multiline-plain") << "line1\nline2";
        QTest::newRow("font") << "<font color=\"#ff0000\" style=\"font-size: 20px;\">red</font>";
    }

    void unchangedLabelCommitKeepsTheValue()
    {
        QFETCH(QString, value);
        const std::string xml = labelDiagram(value);
        MainWindow window;
        window.show();
        window.session().resetDocument(test::documentFromXml(xml));
        const QByteArray before = test::savedBytes(window.session());
        const std::string original = window.session().currentPage()->findCell("v")->value();
        QVERIFY(window.canvas().editLabel("v"));
        QVERIFY(window.canvas().interaction().labelEditor()->isEditing());
        window.canvas().interaction().labelEditor()->stopEditing(false); // commit
        QCOMPARE(QString::fromStdString(window.session().currentPage()->findCell("v")->value()),
                 QString::fromStdString(original));
        QCOMPARE(test::savedBytes(window.session()), before);
        QVERIFY(!window.session().canUndo() || !window.session().isModified());
    }

    // ---- dirty flag ----------------------------------------------------------
    void dirtyFlagFollowsUndoAndSave()
    {
        FakePrompts prompts;
        QTemporaryDir dir;
        prompts.savePath = dir.path() + "/d.drawio";
        MainWindow window;
        window.setPrompts(&prompts);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        QVERIFY(!window.isWindowModified());
        auto move = [&] { QVERIFY(s.edit("m", [](commands::Transaction& tx) { return commands::ops::moveCells(tx, 0, {"a"}, 10, 10); })); s.endMerge(); };
        move();
        QVERIFY(window.isWindowModified());
        QVERIFY(s.undo());
        QVERIFY(!window.isWindowModified());
        QVERIFY(s.redo());
        QVERIFY(window.isWindowModified());
        QVERIFY(window.controller().saveAs());
        QVERIFY(!window.isWindowModified());
        QVERIFY(s.undo());
        QVERIFY(window.isWindowModified());
        QVERIFY(s.redo());
        QVERIFY(!window.isWindowModified());
        move();
        QVERIFY(window.isWindowModified());
        QVERIFY(window.controller().save());
        QVERIFY(!window.isWindowModified());
        QVERIFY(prompts.errors.isEmpty());
    }

    // ---- clipboard ids -------------------------------------------------------
    void pasteGroupWithEdgesKeepsStructureAndUniqueIds()
    {
        DocumentSession s(test::simpleDocument());
        s.selection().set({"a", "b", "e", "g", "c1", "c2"});
        QVERIFY(actions::copySelection(s, *QGuiApplication::clipboard()));
        const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
        for (int i = 0; i < 3; ++i)
            QVERIFY(actions::paste(s, *mime));
        std::set<std::string> ids;
        for (const core::Cell* c : s.currentPage()->cells())
            QVERIFY2(ids.insert(c->id()).second, c->id().c_str());
        for (const core::Cell* c : s.currentPage()->cells()) {
            if (!c->isEdge())
                continue;
            if (!c->source().empty()) QVERIFY(s.currentPage()->findCell(c->source()) != nullptr);
            if (!c->target().empty()) QVERIFY(s.currentPage()->findCell(c->target()) != nullptr);
        }
        // the pasted edges join the pasted vertices, not the originals
        int edgesJoiningOriginals = 0;
        for (const core::Cell* c : s.currentPage()->cells())
            if (c->isEdge() && c->id() != "e" && (c->source() == "a" || c->target() == "b")) ++edgesJoiningOriginals;
        QCOMPARE(edgesJoiningOriginals, 0);
    }

    void pasteIntoOtherPage()
    {
        DocumentSession s(test::simpleDocument());
        s.selection().set({"a", "b", "e"});
        QVERIFY(actions::copySelection(s, *QGuiApplication::clipboard()));
        QVERIFY(s.edit("p", [](commands::Transaction& tx) { return commands::ops::addPage(tx, 1, "P2"); }));
        s.setCurrentPage(1);
        const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
        QVERIFY(actions::paste(s, *mime));
        QCOMPARE(s.currentPage()->cells().size(), std::size_t(2 + 3));
        QVERIFY(s.undo());
    }

    // ---- keyboard on the canvas ---------------------------------------------
    void canvasKeysReachTheActions()
    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        CanvasView& canvas = window.canvas();
        canvas.setFocus();
        window.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&window));
        QTest::keyClick(canvas.viewport(), Qt::Key_A, Qt::ControlModifier);
        QVERIFY(s.selection().size() >= 4);
        QTest::keyClick(canvas.viewport(), Qt::Key_Escape);
        s.selection().set({"a"});
        const double x = s.currentPage()->findCell("a")->geometry()->x();
        QTest::keyClick(canvas.viewport(), Qt::Key_Right);
        QCOMPARE(s.currentPage()->findCell("a")->geometry()->x(), x + 1);
        QTest::keyClick(canvas.viewport(), Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(s.currentPage()->findCell("a")->geometry()->x(), x + 1 + 10);
        QTest::keyClick(canvas.viewport(), Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(s.selection().size(), std::size_t(1));
        QVERIFY(s.selection().first() != "a");
        QTest::keyClick(canvas.viewport(), Qt::Key_Delete);
        QVERIFY(s.currentPage()->findCell(s.selection().first()) == nullptr || s.selection().isEmpty());
        QTest::keyClick(canvas.viewport(), Qt::Key_Z, Qt::ControlModifier);
        s.selection().set({"a"});
        QTest::keyClick(canvas.viewport(), Qt::Key_C, Qt::ControlModifier);
        QTest::keyClick(canvas.viewport(), Qt::Key_V, Qt::ControlModifier);
        QVERIFY(s.selection().first() != "a");
        QTest::keyClick(canvas.viewport(), Qt::Key_F2);
        QVERIFY(canvas.interaction().labelEditor()->isEditing());
        QTest::keyClick(canvas.viewport(), Qt::Key_Escape);
    }

    // ---- moves under zoom ------------------------------------------------------
    void dragMoveUnderZoom_data()
    {
        QTest::addColumn<double>("zoom");
        for (double z : {0.25, 0.5, 1.0, 2.0, 4.0})
            QTest::newRow(qPrintable(QString::number(z))) << z;
    }

    void dragMoveUnderZoom()
    {
        QFETCH(double, zoom);
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        CanvasView& canvas = window.canvas();
        canvas.setZoom(zoom);
        canvas.centerOn(QRectF(0, 0, 300, 200));
        const QPointF from(100, 70); // inside a
        test::click(canvas, from);
        QCOMPARE(s.selection().cells(), std::vector<std::string>{"a"});
        // 60 screen pixels to the right/down
        const QPointF to = from + QPointF(60, 60) / zoom;
        test::drag(canvas, from, to);
        const core::Rect r = s.currentPage()->findCell("a")->geometry()->bounds();
        QVERIFY2(std::abs(r.x - (40 + 60 / zoom)) <= 10 && std::abs(r.y - (40 + 60 / zoom)) <= 10,
                 qPrintable(QString("x=%1 y=%2").arg(r.x).arg(r.y)));
        QVERIFY(std::fmod(r.x, 10) == 0 && std::fmod(r.y, 10) == 0);
        QVERIFY(s.undo());
        QVERIFY(sceneIsFresh(canvas, s));
    }

    // ---- connect ---------------------------------------------------------------
    void edgeBetweenGroupChildAndOutsideVertexIsParentedAtTheCommonAncestor()
    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        CanvasView& canvas = window.canvas();
        canvas.setZoom(1);
        canvas.centerOn(QRectF(0, 0, 500, 400));
        auto center = [&](const char* cid) {
            const render::CellState* st = canvas.diagramScene().state(cid);
            return QPointF(st->bounds.x + st->bounds.width / 2, st->bounds.y + st->bounds.height / 2);
        };
        // hover c1 (inside group g at 40,200) and drag its east arrow to b
        s.enterGroup("g");
        test::move(canvas, center("c1"), Qt::NoModifier, Qt::NoButton);
        const std::size_t before = s.currentPage()->cells().size();
        const render::CellState* c1 = canvas.diagramScene().state("c1");
        test::drag(canvas, center("c1") + QPointF(c1->bounds.width / 2 + 20, 0), center("b"));
        s.exitGroup();
        QVERIFY2(s.currentPage()->cells().size() == before + 1, "no edge created");
        const core::Cell* edge = nullptr;
        for (const core::Cell* c : s.currentPage()->cells())
            if (c->isEdge() && c->id() != "e") edge = c;
        QVERIFY(edge);
        QCOMPARE(edge->source(), std::string("c1"));
        QCOMPARE(edge->target(), std::string("b"));
        QCOMPARE(edge->parent(), std::string("1")); // not the group: the terminals' common ancestor is the layer
    }

    void arrowDroppedOnEmptyCanvasCreatesVertexAndConnectedEdge()
    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        CanvasView& canvas = window.canvas();
        canvas.setZoom(1);
        canvas.centerOn(QRectF(0, 0, 600, 400));
        test::move(canvas, {100, 70}, Qt::NoModifier, Qt::NoButton);
        test::drag(canvas, {100, 120}, {150, 500}); // south arrow -> empty space
        std::size_t vertices = 0, edges = 0;
        for (const core::Cell* c : s.currentPage()->cells()) { vertices += c->isVertex(); edges += c->isEdge(); }
        QCOMPARE(vertices, std::size_t(6)); // a clone of "a" was created at the drop point
        QCOMPARE(edges, std::size_t(2));
        for (const core::Cell* c : s.currentPage()->cells()) {
            if (!c->isEdge()) continue;
            if (!c->source().empty()) QVERIFY(s.currentPage()->findCell(c->source()));
            if (!c->target().empty()) QVERIFY(s.currentPage()->findCell(c->target()));
        }
        QVERIFY(sceneIsFresh(canvas, s));
    }

    // ---- state that outlives the model change it depends on ---------------------
    void labelEditorSurvivesTheEditedCellBeingRemoved()
    {
        MainWindow window;
        window.show();
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        QVERIFY(window.canvas().editLabel("a"));
        QVERIFY(s.edit("del", [](commands::Transaction& tx) { return commands::ops::removeCells(tx, 0, {"a"}); }));
        window.canvas().interaction().labelEditor()->stopEditing(false);
        QVERIFY(s.currentPage()->findCell("a") == nullptr);
        QVERIFY(s.undo());
        QVERIFY(s.currentPage()->findCell("a") != nullptr);
    }

    void labelEditorCommitDuringUndoDoesNotResurrectText()
    {
        MainWindow window;
        window.show();
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        const QByteArray before = test::savedBytes(s);
        QVERIFY(window.canvas().editLabel("a"));
        QTextEdit* te = nullptr;
        for (QTextEdit* e : window.canvas().viewport()->findChildren<QTextEdit*>()) if (e->isVisible()) te = e;
        QVERIFY(te);
        te->selectAll();
        QTest::keyClicks(te, "changed");
        QTest::keyClick(te, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(QString::fromStdString(s.currentPage()->findCell("a")->value()), QStringLiteral("changed"));
        QVERIFY(s.undo());
        QCOMPARE(test::savedBytes(s), before);
    }

    void pageSwitchDropsGroupEntryAndEditing()
    {
        MainWindow window;
        window.show();
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        QVERIFY(s.edit("p", [](commands::Transaction& tx) { return commands::ops::addPage(tx, std::nullopt, "P2"); }));
        s.setCurrentPage(0);
        s.enterGroup("g");
        QCOMPARE(s.insertionParent(), std::string("g"));
        QVERIFY(window.canvas().editLabel("c1"));
        s.setCurrentPage(1);
        QVERIFY(!window.canvas().interaction().labelEditor()->isEditing());
        QCOMPARE(s.insertionParent(), s.currentLayer());
        QVERIFY(s.undo()); // undo the page add while on page 2
        QVERIFY(s.currentPage() != nullptr);
        QVERIFY(sceneIsFresh(window.canvas(), s));
    }

    void undoOfPageAddWhileOnThatPage()
    {
        MainWindow window;
        window.show();
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        QVERIFY(window.pageTabs()->insertPage());
        s.setCurrentPage(1);
        s.selection().set({});
        QVERIFY(s.undo());
        QCOMPARE(s.document().pageCount(), std::size_t(1));
        QCOMPARE(s.currentPageIndex(), std::size_t(0));
        QVERIFY(s.redo());
        QVERIFY(s.undo());
        QVERIFY(sceneIsFresh(window.canvas(), s));
    }

    // ---- hostile or foreign clipboard text -----------------------------------------
    void pasteOfOddClipboardXml_data()
    {
        QTest::addColumn<QString>("xml");
        QTest::addColumn<bool>("expectInsert");
        const QString head = "<mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>";
        const QString tail = "</root></mxGraphModel>";
        const QString geo = "<mxGeometry x=\"5\" y=\"5\" width=\"40\" height=\"20\" as=\"geometry\"/>";
        QTest::newRow("drawio-clipboard") << head + "<mxCell id=\"a\" value=\"X\" vertex=\"1\" parent=\"1\">" + geo + "</mxCell>" + tail << true;
        QTest::newRow("colliding-ids") << head + "<mxCell id=\"a\" vertex=\"1\" parent=\"1\">" + geo + "</mxCell><mxCell id=\"e\" edge=\"1\" source=\"a\" target=\"b\" parent=\"1\"><mxGeometry relative=\"1\" as=\"geometry\"/></mxCell>" + tail << true;
        QTest::newRow("duplicate-ids") << head + "<mxCell id=\"x\" vertex=\"1\" parent=\"1\">" + geo + "</mxCell><mxCell id=\"x\" vertex=\"1\" parent=\"1\">" + geo + "</mxCell>" + tail << false;
        QTest::newRow("parent-cycle") << head + "<mxCell id=\"p\" vertex=\"1\" parent=\"q\">" + geo + "</mxCell><mxCell id=\"q\" vertex=\"1\" parent=\"p\">" + geo + "</mxCell>" + tail << false;
        QTest::newRow("missing-parent") << head + "<mxCell id=\"p\" vertex=\"1\" parent=\"nope\">" + geo + "</mxCell>" + tail << false;
        QTest::newRow("dangling-edge-terminals") << head + "<mxCell id=\"e2\" edge=\"1\" source=\"zz\" target=\"yy\" parent=\"1\"><mxGeometry relative=\"1\" as=\"geometry\"/></mxCell>" + tail << true;
        QTest::newRow("self-parent") << head + "<mxCell id=\"s\" vertex=\"1\" parent=\"s\">" + geo + "</mxCell>" + tail << false;
        QTest::newRow("no-geometry") << head + "<mxCell id=\"n\" vertex=\"1\" parent=\"1\"/>" + tail << true;
        QTest::newRow("nan-geometry") << head + "<mxCell id=\"n\" vertex=\"1\" parent=\"1\"><mxGeometry x=\"nan\" y=\"inf\" width=\"-5\" height=\"1e999\" as=\"geometry\"/></mxCell>" + tail << true;
        QTest::newRow("mxfile-wrapper") << "<mxfile><diagram id=\"d\">" + head + "<mxCell id=\"w\" vertex=\"1\" parent=\"1\">" + geo + "</mxCell>" + tail + "</diagram></mxfile>" << true;
        QTest::newRow("plain-text") << "hello <world> & friends" << true;
    }

    void pasteOfOddClipboardXml()
    {
        QFETCH(QString, xml);
        QFETCH(bool, expectInsert);
        DocumentSession s(test::simpleDocument());
        const QByteArray before = test::savedBytes(s);
        QMimeData mime;
        mime.setText(xml);
        const bool pasted = actions::paste(s, mime);
        if (expectInsert) QVERIFY(pasted);
        // whatever happened, the page must be consistent
        std::set<std::string> ids;
        for (const core::Cell* c : s.currentPage()->cells()) {
            QVERIFY2(ids.insert(c->id()).second, c->id().c_str());
            if (c->parent().empty()) continue;
            QVERIFY2(s.currentPage()->findCell(c->parent()) != nullptr, (c->id() + " has missing parent").c_str());
        }
        const QByteArray after = test::savedBytes(s);
        QVERIFY(test::documentFromXml(std::string_view(after.constData(), size_t(after.size()))).pageCount() == 1);
        if (pasted) { QVERIFY(s.undo()); QCOMPARE(test::savedBytes(s), before); }
    }

    // ---- gestures --------------------------------------------------------------
    void movingAGroupWithItsChildSelectedMovesTheChildOnce()
    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        CanvasView& canvas = window.canvas();
        canvas.setZoom(1);
        canvas.centerOn(QRectF(0, 0, 500, 400));
        s.selection().set({"g", "c1"});
        const QByteArray before = test::savedBytes(s);
        // grab the group by an empty point of its own (group has no fill): use the child c1 area
        const render::CellState* c1 = canvas.diagramScene().state("c1");
        const QPointF p(c1->bounds.x + 20, c1->bounds.y + 20);
        test::drag(canvas, p, p + QPointF(50, 30));
        const core::Rect c1Geo = s.currentPage()->findCell("c1")->geometry()->bounds();
        const core::Rect gGeo = s.currentPage()->findCell("g")->geometry()->bounds();
        QCOMPARE(c1Geo.x, 0.0);
        QCOMPARE(c1Geo.y, 0.0);
        QCOMPARE(gGeo.x, 90.0);
        QVERIFY(s.undo());
        QCOMPARE(test::savedBytes(s), before);
    }

    void movingBothEndsOfAnEdgeWithWaypointsTranslatesThem()
    {
        std::string xml = test::kSimpleDiagram;
        const std::string from = "<mxGeometry relative=\"1\" as=\"geometry\"/>";
        xml.replace(xml.find(from), from.size(),
                    "<mxGeometry relative=\"1\" as=\"geometry\"><Array as=\"points\"><mxPoint x=\"200\" y=\"160\"/></Array></mxGeometry>");
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::documentFromXml(xml));
        DocumentSession& s = window.session();
        CanvasView& canvas = window.canvas();
        canvas.setZoom(1);
        canvas.centerOn(QRectF(0, 0, 500, 400));
        s.selection().set({"a", "b", "e"});
        test::drag(canvas, {100, 70}, {100, 120});
        const core::Cell* e = s.currentPage()->findCell("e");
        QCOMPARE(e->source(), std::string("a"));
        QCOMPARE(e->target(), std::string("b"));
        const auto& pts = e->geometry()->points();
        QCOMPARE(pts.size(), std::size_t(1));
        QCOMPARE(pts[0].x, 200.0);
        QCOMPARE(pts[0].y, 210.0);
    }

    void rotatedVertexResizeUnderZoomKeepsTheOppositeCornerFixed_data()
    {
        QTest::addColumn<double>("zoom");
        QTest::addColumn<double>("rotation");
        for (double z : {0.5, 1.0, 2.0})
            for (double r : {0.0, 30.0, 90.0, 200.0})
                QTest::newRow(qPrintable(QString("z%1 r%2").arg(z).arg(r))) << z << r;
    }

    void rotatedVertexResizeUnderZoomKeepsTheOppositeCornerFixed()
    {
        QFETCH(double, zoom);
        QFETCH(double, rotation);
        std::string xml = test::kSimpleDiagram;
        const std::string key = "style=\"rounded=0;whiteSpace=wrap;\"";
        xml.replace(xml.find(key), key.size(), "style=\"rounded=0;rotation=" + std::to_string(int(rotation)) + ";\"");
        const std::string grid = "grid=\"1\"";
        xml.replace(xml.find(grid), grid.size(), "grid=\"0\"");
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::documentFromXml(xml));
        DocumentSession& s = window.session();
        CanvasView& canvas = window.canvas();
        canvas.setZoom(zoom);
        canvas.centerOn(QRectF(0, 0, 300, 200));
        s.selection().set({"a"});
        const core::Rect r0 = s.currentPage()->findCell("a")->geometry()->bounds();
        const QPointF c0(r0.x + r0.width / 2, r0.y + r0.height / 2);
        auto rot = [&](QPointF p, QPointF c, double deg) {
            const double t = deg * M_PI / 180;
            const QPointF d = p - c;
            return c + QPointF(d.x() * std::cos(t) - d.y() * std::sin(t), d.x() * std::sin(t) + d.y() * std::cos(t));
        };
        const QPointF nwBefore = rot({r0.x, r0.y}, c0, rotation);
        const QPointF se = rot({r0.x + r0.width, r0.y + r0.height}, c0, rotation);
        const QPointF delta = QPointF(20, 10) / zoom;
        test::drag(canvas, se, se + delta);
        const core::Rect r1 = s.currentPage()->findCell("a")->geometry()->bounds();
        const QPointF c1(r1.x + r1.width / 2, r1.y + r1.height / 2);
        const QPointF nwAfter = rot({r1.x, r1.y}, c1, rotation);
        QVERIFY2(QLineF(nwBefore, nwAfter).length() < 1e-6,
                 qPrintable(QString("NW moved %1,%2 -> %3,%4").arg(nwBefore.x()).arg(nwBefore.y()).arg(nwAfter.x()).arg(nwAfter.y())));
        // the size grew by the pointer delta projected on the rotated axes
        const double t = rotation * M_PI / 180;
        const double dw = delta.x() * std::cos(t) + delta.y() * std::sin(t);
        const double dh = -delta.x() * std::sin(t) + delta.y() * std::cos(t);
        QVERIFY2(std::abs(r1.width - (r0.width + dw)) < 1e-6 && std::abs(r1.height - (r0.height + dh)) < 1e-6,
                 qPrintable(QString("size %1x%2 expected %3x%4").arg(r1.width).arg(r1.height).arg(r0.width + dw).arg(r0.height + dh)));
    }

    void paletteDropLandsUnderThePointerWhenZoomedAndScrolled()
    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        CanvasView& canvas = window.canvas();
        canvas.setZoom(2);
        canvas.centerOn(QRectF(300, 300, 100, 100));
        const auto hits = PaletteLibrary::builtin().search(QStringLiteral("rounded rectangle"));
        QVERIFY(!hits.empty());
        QMimeData mime;
        setCellsMimeData(mime, hits.front().entry->cellsXml());
        const QPointF dropAt(350, 330);
        const QPoint at = canvas.mapFromScene(dropAt);
        QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas.viewport(), &enter);
        QDropEvent drop(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas.viewport(), &drop);
        QVERIFY(!window.session().selection().isEmpty());
        const auto& id = window.session().selection().first();
        const render::CellState* st = canvas.diagramScene().state(id);
        QVERIFY(st);
        const QPointF centre(st->bounds.x + st->bounds.width / 2, st->bounds.y + st->bounds.height / 2);
        QVERIFY(QLineF(dropAt, centre).length() <= 15); // grid-snapped
    }

    void appendingTextKeepsTheRestOfAnHtmlLabel_data()
    {
        QTest::addColumn<QString>("value");
        QTest::addColumn<QString>("expected");
        QTest::newRow("bold-br") << "<b>bold</b> &amp; text<br>second line" << "<b>bold</b> &amp; text<br>second line!";
        QTest::newRow("lt") << "x &lt; y" << "x &lt; y!";
        QTest::newRow("div") << "<div>one</div><div>two</div>" << "one<div>two!</div>" /* the editor normalises the first line */;
        QTest::newRow("font-color") << "<font color=\"#ff0000\">red</font> plain" << "<font color=\"#ff0000\">red</font> plain!";
        QTest::newRow("link") << "<a href=\"https://x.org/?a=1&amp;b=2\">link</a>" << "<a href=\"https://x.org/?a=1&amp;b=2\">link!</a>";
        QTest::newRow("span-style") << "<span style=\"font-size: 20px;\">big</span>" << "<span style=\"font-size: 20px;\">big!</span>";
        QTest::newRow("nbsp") << "a&nbsp;&nbsp;b" << "a&nbsp;&nbsp;b!";
        QTest::newRow("list") << "<ul><li>one</li><li>two</li></ul>" << "<ul><li>one</li><li>two!</li></ul>";
    }

    void appendingTextKeepsTheRestOfAnHtmlLabel()
    {
        QFETCH(QString, value);
        QFETCH(QString, expected);
        MainWindow window;
        window.resize(1000, 700);
        window.show();
        window.session().resetDocument(test::documentFromXml(labelDiagram(value)));
        DocumentSession& s = window.session();
        QVERIFY(window.canvas().editLabel("v"));
        QTextEdit* te = nullptr;
        for (QTextEdit* e : window.canvas().viewport()->findChildren<QTextEdit*>()) if (e->isVisible()) te = e;
        QVERIFY(te);
        QTextCursor c = te->textCursor();
        c.movePosition(QTextCursor::End);
        te->setTextCursor(c);
        QTest::keyClicks(te, "!");
        QTest::keyClick(te, Qt::Key_Return, Qt::ControlModifier);
        const QString got = QString::fromStdString(s.currentPage()->findCell("v")->value());
        QCOMPARE(got, expected);
    }

    // ---- shortcuts vs text widgets ---------------------------------------------------
    void editingKeysInTextFieldsDoNotReachTheDocumentActions()
    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        s.selection().set({"a"});
        window.showPanel(MainWindow::Panel::Find);
        window.showPanel(MainWindow::Panel::Palette);
        QApplication::processEvents();
        const QByteArray before = test::savedBytes(s);
        QWidget* fill = window.formatPanel()->findChild<QWidget*>(QStringLiteral("format.fillColor"));
        QVERIFY(fill);
        QList<QLineEdit*> fields;
        if (QLineEdit* f = fill->findChild<QLineEdit*>()) fields << f;
        fields << window.findPanel()->findChildren<QLineEdit*>();
        fields << window.paletteDock()->findChildren<QLineEdit*>();
        QVERIFY(fields.size() >= 3);
        for (QLineEdit* field : fields) {
            if (!field->isVisible()) continue;
            field->setFocus();
            QApplication::processEvents();
            field->setText("abc");
            for (auto key : {Qt::Key_Delete, Qt::Key_Backspace, Qt::Key_Left, Qt::Key_Right, Qt::Key_Home, Qt::Key_End}) {
                QTest::keyClick(field, key);
                QVERIFY2(s.selection().cells() == std::vector<std::string>{"a"}, qPrintable(field->objectName() + " key " + QString::number(key, 16)));
            }
            for (auto key : {Qt::Key_A, Qt::Key_C, Qt::Key_V, Qt::Key_X, Qt::Key_Z, Qt::Key_D}) {
                QTest::keyClick(field, key, Qt::ControlModifier);
                QVERIFY2(s.selection().cells() == std::vector<std::string>{"a"}, qPrintable(field->objectName() + " ctrl key " + QString::number(key, 16)));
            }
            QVERIFY2(test::savedBytes(s) == before || field->objectName().startsWith("format"),
                     qPrintable(field->objectName()));
            if (test::savedBytes(s) != before) { s.undo(); }
            QCOMPARE(s.currentPage()->cells().size(), std::size_t(7 + 1)); // 0, 1 + a b e g c1 c2
        }
    }

    void editingKeysInTheLabelEditorStayInTheEditor()
    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        s.selection().set({"a"});
        QVERIFY(window.canvas().editLabel("a"));
        QTextEdit* te = nullptr;
        for (QTextEdit* e : window.canvas().viewport()->findChildren<QTextEdit*>()) if (e->isVisible()) te = e;
        QVERIFY(te);
        te->setFocus();
        const QByteArray before = test::savedBytes(s);
        QTest::keyClick(te, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(te->textCursor().selectedText(), QStringLiteral("A"));
        QTest::keyClick(te, Qt::Key_Delete);
        QTest::keyClick(te, Qt::Key_Z, Qt::ControlModifier);
        QTest::keyClick(te, Qt::Key_Backspace);
        QCOMPARE(s.currentPage()->cells().size(), std::size_t(8));
        QTest::keyClick(te, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(s.currentPage()->cells().size(), std::size_t(8));
        QTest::keyClick(te, Qt::Key_G, Qt::ControlModifier);
        QCOMPARE(s.currentPage()->cells().size(), std::size_t(8));
        QCOMPARE(test::savedBytes(s), before);
        QTest::keyClick(te, Qt::Key_Escape);
        QCOMPARE(test::savedBytes(s), before);
    }

    void documentShortcutsDoNotFireInTextFields()
    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        window.showPanel(MainWindow::Panel::Find);
        QApplication::processEvents();
        QStringList leaks;
        QTimer closer;
        closer.setInterval(20);
        QObject::connect(&closer, &QTimer::timeout, [&] {
            if (QWidget* modal = QApplication::activeModalWidget()) {
                leaks << "dialog opened: " + QString(modal->metaObject()->className());
                modal->close();
            }
        });
        closer.start();
        for (const ActionInfo& info : window.actions().actions()) {
            if (info.category == window.actions().categories().value(0) || info.id == "about" || info.id == "keyboardShortcuts"
                || info.id == "fullscreen")
                continue; // File actions open dialogs
            for (const QKeySequence& seq : info.action->shortcuts()) {
                if (seq.isEmpty()) continue;
                for (int target = 0; target < 2; ++target) {
                    s.selection().set({"a"});
                    s.endMerge();
                    QWidget* w = nullptr;
                    if (target == 0) {
                        QLineEdit* le = window.findPanel()->findChildren<QLineEdit*>().value(0);
                        le->setFocus();
                        le->setText("abc");
                        w = le;
                    } else {
                        s.selection().set({"a"});
                        window.canvas().editLabel("a");
                        for (QTextEdit* e : window.canvas().viewport()->findChildren<QTextEdit*>()) if (e->isVisible()) w = e;
                        w->setFocus();
                    }
                    const QByteArray before = test::savedBytes(s);
                    const auto sel = s.selection().cells();
                    const bool undoable = s.canUndo();
                    const QString undoTextBefore = s.undoText();
                    QTest::keyClick(w, seq[0].key(), seq[0].keyboardModifiers());
                    if (target == 1) window.canvas().interaction().labelEditor()->stopEditing(true);
                    QApplication::processEvents();
                    if (test::savedBytes(s) != before || s.selection().cells() != sel || s.undoText() != undoTextBefore || s.canUndo() != undoable)
                        leaks << (target ? "labelEditor: " : "lineEdit: ") + info.id + " " + seq.toString()
                                   + (test::savedBytes(s) != before ? " bytes" : "") + (s.selection().cells() != sel ? " selection" : "")
                                   + " undo=" + s.undoText() + "/" + undoTextBefore + (s.canUndo() != undoable ? " canUndo" : "");
                    // reset the document
                    s.resetDocument(test::simpleDocument());
                }
            }
        }
        QVERIFY2(leaks.isEmpty(), qPrintable(leaks.join('\n')));
    }

    void saveWhileEditingALabelKeepsTheTypedText()
    {
        FakePrompts prompts;
        QTemporaryDir dir;
        prompts.savePath = dir.path() + "/s.drawio";
        MainWindow window;
        window.setPrompts(&prompts);
        window.resize(1280, 800);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.session().resetDocument(test::simpleDocument());
        QVERIFY(window.canvas().editLabel("a"));
        QTextEdit* te = nullptr;
        for (QTextEdit* e : window.canvas().viewport()->findChildren<QTextEdit*>()) if (e->isVisible()) te = e;
        QVERIFY(te);
        te->setFocus();
        te->selectAll();
        QTest::keyClicks(te, "typed");
        QTest::keyClick(te, Qt::Key_S, Qt::ControlModifier);
        QApplication::processEvents();
        QFile file(prompts.savePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray saved = file.readAll();
        QVERIFY2(saved.contains("typed"), "Ctrl+S during label editing saved the old label");
        QVERIFY(!window.isWindowModified());
        window.canvas().interaction().labelEditor()->stopEditing(true);
    }

    void undoFromTheToolbarWhileEditingACommitsTheTextFirstThenUndoesIt()
    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        QVERIFY(s.edit("m", [](commands::Transaction& tx) { return commands::ops::moveCells(tx, 0, {"b"}, 10, 10); }));
        s.endMerge();
        QVERIFY(window.canvas().editLabel("a"));
        QTextEdit* te = nullptr;
        for (QTextEdit* e : window.canvas().viewport()->findChildren<QTextEdit*>()) if (e->isVisible()) te = e;
        QVERIFY(te);
        te->selectAll();
        QTest::keyClicks(te, "typed");
        window.actions()[id::kUndo]->trigger(); // toolbar / menu: focus stays in the editor
        QVERIFY(!window.canvas().interaction().labelEditor()->isEditing());
        // the typed text was committed as one step and that step was undone; the earlier move stays
        QCOMPARE(QString::fromStdString(s.currentPage()->findCell("a")->value()), QStringLiteral("A"));
        QCOMPARE(s.currentPage()->findCell("b")->geometry()->x(), 290.0);
        QVERIFY(s.canRedo());
        QCOMPARE(s.redoText(), QStringLiteral("Edit Label"));
    }

    void clipboardActionsGoToAFocusedTextField()
    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        s.selection().set({"a"});
        window.showPanel(MainWindow::Panel::Find);
        QApplication::processEvents();
        QLineEdit* field = window.findPanel()->findChildren<QLineEdit*>().value(0);
        QVERIFY(field && field->isVisible());
        field->setFocus();
        field->setText("hello");
        field->selectAll();
        QGuiApplication::clipboard()->clear();
        window.actions()[id::kCopy]->trigger();
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("hello"));
        QVERIFY(!QGuiApplication::clipboard()->mimeData()->hasFormat(QString::fromLatin1(kCellsMimeType)));
        window.actions()[id::kCut]->trigger();
        QVERIFY(field->text().isEmpty());
        window.actions()[id::kPaste]->trigger();
        QCOMPARE(field->text(), QStringLiteral("hello"));
        window.actions()[id::kSelectAll]->trigger();
        QCOMPARE(field->selectedText(), QStringLiteral("hello"));
        QCOMPARE(s.selection().cells(), std::vector<std::string>{"a"}); // cells untouched
        QCOMPARE(s.currentPage()->cells().size(), std::size_t(8));
    }

    void connectAndRubberbandWorkUnderZoom_data()
    {
        QTest::addColumn<double>("zoom");
        for (double z : {0.25, 0.5, 2.0, 4.0})
            QTest::newRow(qPrintable(QString::number(z))) << z;
    }

    void connectAndRubberbandWorkUnderZoom()
    {
        QFETCH(double, zoom);
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::documentFromXml(
            "<mxfile><diagram id=\"p\" name=\"P\"><mxGraphModel grid=\"1\" gridSize=\"10\"><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
            "<mxCell id=\"s\" vertex=\"1\" parent=\"1\" style=\"rounded=0;\"><mxGeometry x=\"100\" y=\"100\" width=\"120\" height=\"60\" as=\"geometry\"/></mxCell>"
            "<mxCell id=\"t\" vertex=\"1\" parent=\"1\" style=\"rounded=0;\"><mxGeometry x=\"400\" y=\"100\" width=\"120\" height=\"60\" as=\"geometry\"/></mxCell>"
            "</root></mxGraphModel></diagram></mxfile>"));
        DocumentSession& s = window.session();
        CanvasView& canvas = window.canvas();
        canvas.setZoom(zoom);
        canvas.centerOn(QRectF(100, 80, 420, 100));
        // rubber band around both vertices
        test::drag(canvas, {60, 40}, {580, 220});
        QCOMPARE(s.selection().size(), std::size_t(2));
        s.selection().clear();
        // connect s -> t through the hover arrow; arrow offset scales with the zoom
        test::move(canvas, {160, 130}, Qt::NoModifier, Qt::NoButton);
        const double arrowOffset = 60 + 20.0 / zoom;
        test::drag(canvas, {160 + arrowOffset, 130}, {460, 130});
        const core::Cell* edge = nullptr;
        for (const core::Cell* c : s.currentPage()->cells()) if (c->isEdge()) edge = c;
        QVERIFY2(edge != nullptr, "no edge created");
        QCOMPARE(edge->source(), std::string("s"));
        QCOMPARE(edge->target(), std::string("t"));
        QVERIFY(sceneIsFresh(canvas, s));
    }

    void viewingAndFocusingFormatFieldsDoesNotDirtyTheDocument()
    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        CanvasView& canvas = window.canvas();
        canvas.setZoom(2);
        canvas.fitWindow();
        window.pageTabs()->setFocus();
        s.enterGroup("g");
        s.exitGroup();
        for (const char* sel : {"a", "e", "g", "c1"}) {
            s.selection().set({sel});
            QApplication::processEvents();
            for (QLineEdit* edit : window.formatPanel()->findChildren<QLineEdit*>()) {
                if (!edit->isVisible()) continue;
                edit->setFocus();
                emit edit->editingFinished();
                edit->clearFocus();
            }
            for (QAbstractSpinBox* box : window.formatPanel()->findChildren<QAbstractSpinBox*>()) {
                if (!box->isVisible()) continue;
                box->setFocus();
                emit box->editingFinished();
                box->clearFocus();
            }
            QVERIFY2(!s.isModified(), sel);
            QVERIFY2(!s.canUndo(), (QString(sel) + " undo=" + s.undoText()).toUtf8().constData());
        }
        QVERIFY(!window.isWindowModified());
    }

    void mergedFormatEditsUndoRedoCleanly()
    {
        DocumentSession s(test::simpleDocument());
        const QByteArray original = test::savedBytes(s);
        s.selection().set({"a", "b"});
        QVERIFY(applyStyle(s, "Fill", {"a", "b"}, "fillColor", std::string("#ff0000"), formatMergeKey("fillColor", {"a", "b"})));
        QVERIFY(applyStyle(s, "Fill", {"a", "b"}, "fillColor", std::string("#00ff00"), formatMergeKey("fillColor", {"a", "b"})));
        const QByteArray green = test::savedBytes(s);
        QVERIFY(s.undo());
        QCOMPARE(test::savedBytes(s), original); // one step for the merged edits
        QVERIFY(!s.canUndo());
        QVERIFY(applyStyle(s, "Fill", {"a", "b"}, "fillColor", std::string("#0000ff"), formatMergeKey("fillColor", {"a", "b"})));
        QVERIFY(s.undo());
        QCOMPARE(test::savedBytes(s), original);
        QVERIFY(s.redo());
        QVERIFY(test::savedBytes(s) != original);
        QVERIFY(s.undo());
        QCOMPARE(test::savedBytes(s), original);
        QVERIFY(test::savedBytes(s) != green);
        // an unrelated edit between two same-key edits must separate them
        QVERIFY(applyStyle(s, "Fill", {"a"}, "fillColor", std::string("#111111"), formatMergeKey("fillColor", {"a"})));
        QVERIFY(s.edit("m", [](commands::Transaction& tx) { return commands::ops::moveCells(tx, 0, {"b"}, 10, 0); }));
        QVERIFY(applyStyle(s, "Fill", {"a"}, "fillColor", std::string("#222222"), formatMergeKey("fillColor", {"a"})));
        QVERIFY(s.undo());
        QCOMPARE(s.currentPage()->findCell("a")->style().value("fillColor"), std::optional<std::string>("#111111"));
        QVERIFY(s.undo());
        QVERIFY(s.undo());
        QCOMPARE(test::savedBytes(s), original);
    }

    void cellFlagsAreRespectedByEveryEntryPoint_data()
    {
        QTest::addColumn<QString>("flag");
        QTest::newRow("editable") << "editable=0";
        QTest::newRow("movable") << "movable=0";
        QTest::newRow("deletable") << "deletable=0";
        QTest::newRow("locked") << "locked=1";
        QTest::newRow("resizable") << "resizable=0";
    }

    void cellFlagsAreRespectedByEveryEntryPoint()
    {
        QFETCH(QString, flag);
        std::string xml = test::kSimpleDiagram;
        const std::string key = "style=\"rounded=0;whiteSpace=wrap;\"";
        xml.replace(xml.find(key), key.size(), "style=\"rounded=0;" + flag.toStdString() + ";\"");
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::documentFromXml(xml));
        DocumentSession& s = window.session();
        CanvasView& canvas = window.canvas();
        canvas.setZoom(1);
        canvas.centerOn(QRectF(0, 0, 500, 400));
        const bool editable = flag != "editable=0" && flag != "locked=1";
        const bool movable = flag != "movable=0" && flag != "locked=1";
        const bool deletable = flag != "deletable=0" && flag != "locked=1";
        // edit label by double click / F2 / API
        test::doubleClick(canvas, {100, 70});
        QCOMPARE(canvas.interaction().labelEditor()->isEditing(), editable);
        canvas.interaction().labelEditor()->stopEditing(true);
        s.selection().set({"a"});
        window.actions()[id::kEditLabel]->trigger();
        QCOMPARE(canvas.interaction().labelEditor()->isEditing(), editable);
        canvas.interaction().labelEditor()->stopEditing(true);
        // move by drag and by arrow key
        test::drag(canvas, {100, 70}, {160, 130});
        QTest::keyClick(canvas.viewport(), Qt::Key_Right);
        const double x = s.currentPage()->findCell("a")->geometry()->x();
        QCOMPARE(x != 40.0, movable);
        // delete
        s.selection().set({"a"});
        window.actions()[id::kDelete]->trigger();
        QCOMPARE(s.currentPage()->findCell("a") == nullptr, deletable);
        // format panel / menu edits keep working only where draw.io allows: no crash is enough here
        QVERIFY(sceneIsFresh(canvas, s));
    }

    void closingWhileEditingALabelAsksToSaveTheTypedText()
    {
        FakePrompts prompts;
        QTemporaryDir dir;
        prompts.savePath = dir.path() + "/c.drawio";
        prompts.choice = DocumentPrompts::SaveChoice::Save;
        MainWindow window;
        window.setPrompts(&prompts);
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        QVERIFY(window.canvas().editLabel("a"));
        QTextEdit* te = nullptr;
        for (QTextEdit* e : window.canvas().viewport()->findChildren<QTextEdit*>()) if (e->isVisible()) te = e;
        QVERIFY(te);
        te->selectAll();
        QTest::keyClicks(te, "typed");
        window.close();
        QCOMPARE(prompts.saveAsked, 1);
        QFile file(prompts.savePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("typed"));
    }

    void openingAnotherFileWhileEditingALabelAsksToSave()
    {
        FakePrompts prompts;
        prompts.choice = DocumentPrompts::SaveChoice::Cancel;
        MainWindow window;
        window.setPrompts(&prompts);
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        QVERIFY(window.canvas().editLabel("a"));
        QTextEdit* te = nullptr;
        for (QTextEdit* e : window.canvas().viewport()->findChildren<QTextEdit*>()) if (e->isVisible()) te = e;
        te->selectAll();
        QTest::keyClicks(te, "typed");
        QVERIFY(!window.controller().newDocument()); // Cancel keeps the document
        QCOMPARE(prompts.saveAsked, 1);
        QCOMPARE(QString::fromStdString(window.session().currentPage()->findCell("a")->value()), QStringLiteral("typed"));
    }

    void wheelZoomKeepsThePointUnderTheCursor()
    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        CanvasView& canvas = window.canvas();
        canvas.setZoom(1);
        canvas.centerOn(QRectF(0, 0, 500, 400));
        for (int steps : {1, 2, -1, -3, 5}) {
            const QPoint anchor(300, 200);
            const QPointF before = canvas.mapToScene(anchor);
            QWheelEvent wheel(QPointF(anchor), canvas.viewport()->mapToGlobal(QPointF(anchor)), QPoint(), QPoint(0, 120 * steps),
                              Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(canvas.viewport(), &wheel);
            const QPointF after = canvas.mapToScene(anchor);
            QVERIFY2(QLineF(before, after).length() < 2.0 / canvas.zoom() + 1.0,
                     qPrintable(QString("steps %1 zoom %2: %3,%4 -> %5,%6").arg(steps).arg(canvas.zoom()).arg(before.x()).arg(before.y()).arg(after.x()).arg(after.y())));
        }
    }

    void failedOpenLeavesTheCurrentDocumentAlone_data()
    {
        QTest::addColumn<QByteArray>("content");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("garbage") << QByteArrayLiteral("\x00\x01\x02 not xml");
        QTest::newRow("truncated") << QByteArray("<mxfile><diagram id=\"p\"><mxGraphModel><root><mxCell id=\"0\"/>");
        QTest::newRow("wrong-root") << QByteArray("<html><body>hi</body></html>");
        QTest::newRow("bad-compressed") << QByteArray("<mxfile><diagram id=\"p\">!!!notbase64!!!</diagram></mxfile>");
        QTest::newRow("no-pages") << QByteArray("<mxfile></mxfile>");
        QTest::newRow("png-not-drawio") << QByteArrayLiteral("\x89PNG\r\n\x1a\n\0\0\0\rIHDR");
    }

    void failedOpenLeavesTheCurrentDocumentAlone()
    {
        QFETCH(QByteArray, content);
        FakePrompts prompts;
        QTemporaryDir dir;
        const QString path = dir.path() + "/bad.drawio";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(content);
        f.close();
        MainWindow window;
        window.setPrompts(&prompts);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        const QByteArray before = test::savedBytes(window.session());
        const bool opened = window.controller().openPath(path);
        if (!opened) {
            QCOMPARE(test::savedBytes(window.session()), before);
            QVERIFY(!prompts.errors.isEmpty());
        } else {
            QVERIFY(window.session().document().pageCount() >= 1);
            QVERIFY(window.session().currentPage() != nullptr);
            window.canvas().grab();
        }
        QVERIFY(!window.isWindowModified());
    }

    void opaquePageSurvivesEditingOtherPagesAndEveryAction()
    {
        std::string xml = test::kSimpleDiagram;
        const std::string tail = "</diagram></mxfile>";
        xml.replace(xml.rfind(tail), tail.size(), "</diagram><diagram id=\"bad\" name=\"Bad\">!!!notbase64!!!</diagram></mxfile>");
        FakePrompts prompts;
        MainWindow window;
        window.setPrompts(&prompts);
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::documentFromXml(xml));
        DocumentSession& s = window.session();
        QCOMPARE(s.document().pageCount(), std::size_t(2));
        s.setCurrentPage(1);
        QApplication::processEvents();
        window.canvas().fitWindow();
        window.canvas().grab();
        // every action on an opaque page: no crash, no change to the page
        for (const ActionInfo& info : window.actions().actions()) {
            if (info.category == window.actions().categories().value(0) || info.id == "editStyle" || info.id == "preferences"
                || info.id == "about" || info.id == "keyboardShortcuts" || info.id == "renamePage" || info.id == "findReplace"
                || info.id == "deletePage" || info.id == "fullscreen" || info.id == "insertPage" || info.id == "duplicatePage"
                || info.id == "diagramFollowTheme" || info.id == "diagramLight" || info.id == "diagramDark")
                continue;
            if (info.action->isEnabled())
                info.action->trigger();
            QApplication::processEvents();
        }
        QVERIFY(test::savedBytes(s).contains("!!!notbase64!!!"));
        s.setCurrentPage(0);
        s.selection().set({"a"});
        window.actions()[id::kDuplicate]->trigger();
        QVERIFY(test::savedBytes(s).contains("!!!notbase64!!!"));
        window.canvas().grab();
    }

    void everyActionOnEverySelectionKeepsTheSceneFreshAndUndoesToTheOriginal()
    {
        FakePrompts prompts;
        MainWindow window;
        window.setPrompts(&prompts);
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        CanvasView& canvas = window.canvas();
        canvas.setZoom(1);
        QTimer closer;
        closer.setInterval(10);
        QObject::connect(&closer, &QTimer::timeout, [] { if (QWidget* m = QApplication::activeModalWidget()) m->close(); });
        closer.start();
        const QByteArray original = test::savedBytes(s);
        const std::vector<std::vector<std::string>> selections{{}, {"a"}, {"a", "b"}, {"a", "b", "e"}, {"g"}, {"c1"}, {"g", "c1", "c2"}, {"e"}};
        QStringList problems;
        for (const auto& sel : selections) {
            for (const ActionInfo& info : window.actions().actions()) {
                static const QStringList skip{"new", "open", "openRecent", "save", "saveAs", "revert", "exportAs", "pageSetup", "print",
                                              "printPreview", "close", "quit", "fullscreen", "deletePage", "undo", "redo", "preferences",
                                              "diagramFollowTheme", "diagramLight", "diagramDark"}; // settings that outlive the window; see tst_diagram_scheme
                if (skip.contains(info.id))
                    continue;
                s.selection().set(sel);
                if (!info.action->isEnabled())
                    continue;
                info.action->trigger();
                QApplication::processEvents();
                if (!sceneIsFresh(canvas, s))
                    problems << "stale after " + info.id + " on " + QString::number(sel.size()) + " cells";
            }
        }
        int guard = 0;
        while (s.canUndo() && guard++ < 2000)
            s.undo();
        if (test::savedBytes(s) != original)
            problems << "bytes differ after undoing everything";
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
    }

    void rightDragPansWithoutAContextMenuAndRightClickSelects()
    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        CanvasView& canvas = window.canvas();
        canvas.setZoom(1);
        canvas.centerOn(QRectF(0, 0, 500, 400));
        QSignalSpy menu(&canvas, &CanvasView::contextMenuRequested);
        QTimer closer; // the context menu runs its own event loop
        closer.setInterval(20);
        QObject::connect(&closer, &QTimer::timeout, [] { if (QWidget* p = QApplication::activePopupWidget()) p->close(); });
        closer.start();
        const QPointF origin = canvas.mapToScene(QPoint(0, 0));
        test::press(canvas, {600, 300}, Qt::NoModifier, Qt::RightButton);
        for (int i = 1; i <= 5; ++i)
            test::sendMouse(canvas, QEvent::MouseMove, QPointF(600 - 20 * i, 300 - 10 * i), Qt::NoButton, Qt::RightButton);
        test::release(canvas, {500, 250}, Qt::NoModifier, Qt::RightButton);
        QCOMPARE(menu.count(), 0);
        QVERIFY(canvas.mapToScene(QPoint(0, 0)) != origin);
        // a plain right click on a vertex selects it and asks for the menu
        canvas.centerOn(QRectF(0, 0, 500, 400));
        window.session().selection().clear();
        const QPointF on = QPointF(100, 70);
        test::press(canvas, on, Qt::NoModifier, Qt::RightButton);
        test::release(canvas, on, Qt::NoModifier, Qt::RightButton);
        QCOMPARE(window.session().selection().cells(), std::vector<std::string>{"a"});
    }

    void doubleClickOnBlankCanvasInsertsOneTextCellThatUndoRemoves()
    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        CanvasView& canvas = window.canvas();
        canvas.setZoom(1);
        canvas.centerOn(QRectF(0, 0, 600, 400));
        const QByteArray before = test::savedBytes(s);
        test::doubleClick(canvas, {500, 300});
        QTextEdit* te = nullptr;
        for (QTextEdit* e : canvas.viewport()->findChildren<QTextEdit*>()) if (e->isVisible()) te = e;
        QVERIFY2(te != nullptr, "no editor on blank double click");
        QTest::keyClicks(te, "note <b> & more");
        QTest::keyClick(te, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(s.currentPage()->cells().size(), std::size_t(9));
        const core::Cell* created = nullptr;
        for (const core::Cell* c : s.currentPage()->cells()) if (c->value().find("note") != std::string::npos) created = c;
        QVERIFY(created);
        QVERIFY(created->isVertex());
        QCOMPARE(created->parent(), std::string("1"));
        QVERIFY(s.undo()); // the insertion and the typed text are one step each, both undone
        if (s.canUndo())
            QVERIFY(s.undo());
        QCOMPARE(test::savedBytes(s), before);
    }

    void edgeLabelEditKeepsRoutingAndSavesAsPlainValue()
    {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        CanvasView& canvas = window.canvas();
        canvas.setZoom(1);
        canvas.centerOn(QRectF(0, 0, 600, 400));
        const QByteArray before = test::savedBytes(s);
        test::doubleClick(canvas, {220, 70}); // on the edge a -> b
        QTextEdit* te = nullptr;
        for (QTextEdit* e : canvas.viewport()->findChildren<QTextEdit*>()) if (e->isVisible()) te = e;
        QVERIFY2(te != nullptr, "no editor on edge double click");
        QTest::keyClicks(te, "label");
        QTest::keyClick(te, Qt::Key_Return, Qt::ControlModifier);
        const core::Cell* e = s.currentPage()->findCell("e");
        QCOMPARE(e->value(), std::string("label"));
        QCOMPARE(e->source(), std::string("a"));
        QCOMPARE(e->target(), std::string("b"));
        QVERIFY(e->geometry()->relative());
        QVERIFY(s.undo());
        QCOMPARE(test::savedBytes(s), before);
    }

    void commandsDuringADragDoNotCorruptTheDocument_data()
    {
        QTest::addColumn<QString>("action");
        for (const char* a : {"undo", "redo", "delete", "group", "duplicate", "selectNone", "toFront"})
            QTest::newRow(a) << a;
    }

    void commandsDuringADragDoNotCorruptTheDocument()
    {
        QFETCH(QString, action);
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        DocumentSession& s = window.session();
        CanvasView& canvas = window.canvas();
        canvas.setZoom(1);
        canvas.centerOn(QRectF(0, 0, 500, 400));
        QVERIFY(s.edit("m", [](commands::Transaction& tx) { return commands::ops::moveCells(tx, 0, {"b"}, 10, 0); }));
        s.endMerge();
        test::click(canvas, {100, 70});
        test::press(canvas, {100, 70});
        for (int i = 1; i <= 3; ++i)
            test::move(canvas, QPointF(100 + 10 * i, 70 + 10 * i));
        window.actions().action(action)->trigger();
        for (int i = 4; i <= 6; ++i)
            test::move(canvas, QPointF(100 + 10 * i, 70 + 10 * i));
        test::release(canvas, {160, 130});
        QApplication::processEvents();
        QVERIFY(sceneIsFresh(canvas, s));
        std::set<std::string> ids;
        for (const core::Cell* c : s.currentPage()->cells()) {
            QVERIFY(ids.insert(c->id()).second);
            if (!c->parent().empty())
                QVERIFY(s.currentPage()->findCell(c->parent()) != nullptr);
        }
        int guard = 0;
        while (s.canUndo() && guard++ < 20)
            s.undo();
        QVERIFY(!s.isModified());
    }
};

SHELL_TEST_MAIN(TestEditorBehaviour)
#include "tst_editor_behaviour.moc"
