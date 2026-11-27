// End-to-end editor path through the real MainWindow with synthetic input
// (offscreen): startup with a corpus file -> select / move / resize ->
// connect two shapes -> edit a label -> change the fill in the Format panel ->
// undo / redo -> add a layer and a page -> drop a palette shape -> export PNG
// -> save -> reopen identical.

#include "support/shell_env.h"
#include "support/test_support.h"

#include "qraw/format/codec.h"
#include "qraw/ui/cell_transfer.h"
#include "qraw/ui/export_dialog.h"
#include "qraw/ui/format_panel.h"
#include "qraw/ui/layers_panel.h"
#include "qraw/ui/main_window.h"
#include "qraw/ui/page_tabs.h"
#include "qraw/ui/palette_library.h"

#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QImage>
#include <QLineEdit>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTextEdit>

using namespace qraw;
using namespace qraw::ui;
namespace id = qraw::ui::action_id;

namespace {

// Ids of the vertices of the flowchart template (tests/corpus/upstream-templates/basic/flowchart.xml).
constexpr char kStart[] = "WIyWlLk6GJQsqaUBKTNV-3";   // "Lamp doesn't work", 160,80 120x40
constexpr char kPlugIn[] = "WIyWlLk6GJQsqaUBKTNV-7";  // "Plug in lamp", 320,190 120x40
constexpr char kRepair[] = "WIyWlLk6GJQsqaUBKTNV-11"; // "Repair Lamp", 160,430 120x40
constexpr char kBulb[] = "WIyWlLk6GJQsqaUBKTNV-12";   // "Replace Bulb", 320,310 120x40

class FakePrompts final : public DocumentPrompts {
public:
    SaveChoice askSaveChanges(const QString&) override { return SaveChoice::Discard; }
    std::optional<QString> askOpenPath(const QString&) override { return std::nullopt; }
    std::optional<QString> askSavePath(const QString&, const QString&) override { return savePath; }
    void showError(const QString& title, const QString& message) override { errors << title + ": " + message; }

    QString savePath;
    QStringList errors;
};

const core::Cell& cell(const MainWindow& window, const char* cellId)
{
    return *window.session().currentPage()->findCell(cellId);
}

QRectF bounds(const MainWindow& window, const char* cellId)
{
    const render::CellState* state = window.canvas().diagramScene().state(cellId);
    return state != nullptr ? QRectF(state->bounds.x, state->bounds.y, state->bounds.width, state->bounds.height)
                            : QRectF();
}

std::size_t edgeCount(const MainWindow& window)
{
    std::size_t edges = 0;
    for (const core::Cell* c : window.session().currentPage()->cells())
        edges += c->isEdge() ? 1 : 0;
    return edges;
}

// Drag of a palette entry onto the canvas (DragEnter, DragMove, Drop on the viewport).
void dropEntry(CanvasView& canvas, const PaletteEntry& entry, QPointF scenePos)
{
    QMimeData mime;
    setCellsMimeData(mime, entry.cellsXml());
    const QPoint at = canvas.mapFromScene(scenePos);
    QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &enter);
    QDropEvent drop(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &drop);
}

} // namespace

class TestEndToEnd : public QObject {
    Q_OBJECT

private slots:
    void editorPathFromOpenToReopen()
    {
        const QString source = QStringLiteral(QRAW_SOURCE_DIR "/tests/corpus/upstream-templates/basic/flowchart.xml");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        FakePrompts prompts;
        prompts.savePath = directory.path() + QStringLiteral("/e2e.drawio");

        MainWindow window;
        window.setPrompts(&prompts);
        window.resize(1280, 800);
        window.show();

        // 1. Launch with a file: opens without any dialog.
        window.startup({source});
        QVERIFY(window.session().currentPage() != nullptr);
        QVERIFY(!window.isWindowModified());
        CanvasView& canvas = window.canvas();
        canvas.setZoom(1);
        canvas.centerOn(QRectF(100, 60, 400, 440));
        const QByteArray opened = test::savedBytes(window.session());

        // 2. Select and move a vertex by dragging it (grid snapping applies).
        test::click(canvas, bounds(window, kStart).center());
        QCOMPARE(window.session().selection().cells(), std::vector<std::string>{kStart});
        const double startX = cell(window, kStart).geometry()->x();
        test::drag(canvas, bounds(window, kStart).center(), bounds(window, kStart).center() + QPointF(40, 0));
        QCOMPARE(cell(window, kStart).geometry()->x(), startX + 40);
        QVERIFY(window.isWindowModified());

        // 3. Resize another vertex by its south-east handle.
        test::click(canvas, bounds(window, kPlugIn).center());
        const QRectF before = bounds(window, kPlugIn);
        test::drag(canvas, before.bottomRight(), before.bottomRight() + QPointF(20, 20));
        QVERIFY(bounds(window, kPlugIn).width() > before.width());
        QVERIFY(bounds(window, kPlugIn).height() > before.height());

        // 4. Connect two shapes: hover the source, drag its east arrow onto the target.
        const std::size_t edgesBefore = edgeCount(window);
        const QRectF repair = bounds(window, kRepair);
        test::click(canvas, {900, 900}); // deselect (outside any shape)
        test::move(canvas, repair.center(), Qt::NoModifier, Qt::NoButton);
        test::drag(canvas, repair.center() + QPointF(repair.width() / 2 + 20, 0), bounds(window, kBulb).center());
        QCOMPARE(edgeCount(window), edgesBefore + 1);
        const core::Cell* edge = nullptr;
        for (const core::Cell* c : window.session().currentPage()->cells()) {
            if (c->isEdge() && c->source() == kRepair && c->target() == kBulb)
                edge = c;
        }
        QVERIFY2(edge != nullptr, "no edge Repair -> Bulb");

        // 5. Edit a label inline: double click, type, commit with Ctrl+Enter.
        test::doubleClick(canvas, bounds(window, kBulb).center());
        QTextEdit* editor = nullptr;
        for (QTextEdit* e : canvas.viewport()->findChildren<QTextEdit*>()) {
            if (e->isVisible())
                editor = e;
        }
        QVERIFY2(editor != nullptr, "label editor did not open");
        editor->selectAll();
        QTest::keyClicks(editor, "Fit new bulb");
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(QString::fromStdString(cell(window, kBulb).value()).contains(QStringLiteral("Fit new bulb")));

        // 6. Change the fill in the Format panel.
        window.session().selection().set({kBulb});
        QWidget* fill = window.formatPanel()->findChild<QWidget*>(QStringLiteral("format.fillColor"));
        QVERIFY2(fill != nullptr, "no fill control");
        QLineEdit* fillText = fill->findChild<QLineEdit*>();
        QVERIFY(fillText != nullptr);
        fillText->setText(QStringLiteral("#ffcc00"));
        QTest::keyClick(fillText, Qt::Key_Return);
        QCOMPARE(cell(window, kBulb).style().value("fillColor"), std::optional<std::string>("#ffcc00"));

        // 7. Undo / redo through the actions.
        const QByteArray filled = test::savedBytes(window.session());
        window.actions()[id::kUndo]->trigger();
        QVERIFY(cell(window, kBulb).style().value("fillColor") != std::optional<std::string>("#ffcc00"));
        window.actions()[id::kRedo]->trigger();
        QCOMPARE(test::savedBytes(window.session()), filled);

        // 8. Add a layer (drop target), then drag a palette shape in.
        const std::size_t cellsBefore = window.session().currentPage()->cells().size();
        QVERIFY(window.layersPanel()->addLayer());
        const auto hits = PaletteLibrary::builtin().search(QStringLiteral("rounded rectangle"));
        QVERIFY(!hits.empty());
        dropEntry(canvas, *hits.front().entry, {600, 120});
        QVERIFY(window.session().currentPage()->cells().size() > cellsBefore + 1); // layer + shape
        QVERIFY(!window.session().selection().isEmpty());

        // 9. Add a page, switch back to the first.
        QVERIFY(window.pageTabs()->insertPage());
        QCOMPARE(window.session().document().pageCount(), std::size_t(2));
        window.session().setCurrentPage(0);

        // 10. Export the first page as PNG (dialog request -> runExport).
        const QString png = directory.path() + QStringLiteral("/e2e.png");
        ExportDialog dialog(false, png);
        dialog.setFormat(render::ExportFormat::Png);
        dialog.setOutputPath(png);
        QString error;
        QVERIFY2(runExport(window.session().document(), 0, {}, dialog.request(), &error), qPrintable(error));
        const QImage image(png);
        QVERIFY(!image.isNull());
        QVERIFY(image.width() > 200 && image.height() > 200);

        // 11. Save, reopen in a fresh window: identical bytes.
        const QByteArray edited = test::savedBytes(window.session());
        QVERIFY(edited != opened);
        QVERIFY(window.controller().saveAs());
        QVERIFY(!window.isWindowModified());
        MainWindow reopened;
        reopened.setPrompts(&prompts);
        QVERIFY(reopened.controller().openPath(prompts.savePath));
        QCOMPARE(test::savedBytes(reopened.session()), edited);
        QCOMPARE(reopened.session().document().pageCount(), std::size_t(2));
        QVERIFY(prompts.errors.isEmpty());
    }
};

SHELL_TEST_MAIN(TestEndToEnd)
#include "tst_e2e_editor.moc"
