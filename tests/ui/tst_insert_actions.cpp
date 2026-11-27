// Insert menu: text, image, link, lines and the table size picker (insert_actions.h,
// table_size_picker.h, main_window_insert.cpp). Every insertion is one undo step that restores the
// document byte for byte, lands at the insert point on the grid, takes the default style, goes into
// the entered group and ends up selected.
#include "support/shell_env.h"
#include "support/test_support.h"

#include "qraw/commands/default_style.h"
#include "qraw/ui/app_settings.h"
#include "qraw/ui/edit_image_dialog.h"
#include "qraw/ui/edit_link_dialog.h"
#include "qraw/ui/insert_actions.h"
#include "qraw/ui/label_editor.h"
#include "qraw/ui/main_window.h"
#include "qraw/ui/table_size_picker.h"

#include <QAction>
#include <QBuffer>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTimer>

using namespace qraw;
using namespace qraw::ui;
namespace id = qraw::ui::action_id;

namespace {

// A page with a group "g" and a locked layer "L2" (its cell "x" is only there to have something in it).
const char* kDiagram =
    R"xml(<mxfile><diagram id="p1" name="Page-1"><mxGraphModel gridSize="10" grid="1"><root><mxCell id="0"/><mxCell id="1" parent="0"/>)xml"
    R"xml(<mxCell id="g" value="" style="group" vertex="1" connectable="0" parent="1"><mxGeometry x="200" y="200" width="200" height="100" as="geometry"/></mxCell>)xml"
    R"xml(<mxCell id="L2" value="Locked" style="locked=1;" parent="0"/>)xml"
    R"xml(<mxCell id="x" value="X" vertex="1" parent="L2"><mxGeometry x="0" y="0" width="40" height="40" as="geometry"/></mxCell>)xml"
    R"xml(</root></mxGraphModel></diagram><diagram id="p2" name="Second"><mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/></root></mxGraphModel></diagram></mxfile>)xml";

QImage redImage(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::red);
    return image;
}

QByteArray pngBytes(int width, int height)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    redImage(width, height).save(&buffer, "PNG");
    return bytes;
}

template <class T>
void withModalDialog(std::function<void(T&)> fn, QDialog::DialogCode result = QDialog::Accepted)
{
    QTimer::singleShot(0, [fn = std::move(fn), result] {
        auto* dialog = qobject_cast<T*>(QApplication::activeModalWidget());
        QVERIFY(dialog != nullptr);
        fn(*dialog);
        dialog->done(result);
    });
}

// The one cell that the last edit selected.
const core::Cell& inserted(const DocumentSession& session)
{
    return *session.currentPage()->findCell(session.selection().first());
}

QRectF bounds(const core::Cell& cell)
{
    const core::Rect r = cell.geometry()->bounds();
    return {r.x, r.y, r.width, r.height};
}

std::size_t cellCount(const DocumentSession& session)
{
    return session.currentPage()->descendants("0").size();
}

} // namespace

class TstInsertActions : public QObject {
    Q_OBJECT

private slots:
    void init() { AppSettings().setDefaultStyles({}); }

    // --- lines ------------------------------------------------------------------------------
    void linesAreCentredOnThePointOnTheGridAndSelected()
    {
        DocumentSession session(test::documentFromXml(kDiagram));
        const QByteArray before = test::savedBytes(session);
        QVERIFY(actions::insertLine(session, false, QPointF(500, 400)));
        QCOMPARE(session.undoText(), QStringLiteral("Insert Horizontal Line"));
        QCOMPARE(session.selection().cells().size(), std::size_t(1));
        const core::Cell& line = inserted(session);
        QCOMPARE(line.parent(), std::string("1"));
        QCOMPARE(QString::fromStdString(line.styleText()), QStringLiteral("line;strokeWidth=2;html=1;"));
        QCOMPARE(bounds(line), QRectF(420, 400, 160, 10));

        QVERIFY(actions::insertLine(session, true, QPointF(500, 400)));
        QCOMPARE(session.undoText(), QStringLiteral("Insert Vertical Line"));
        QCOMPARE(bounds(inserted(session)), QRectF(500, 320, 10, 160));
        QVERIFY(inserted(session).style().value("direction", "") == "south");

        QVERIFY(session.undo());
        QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), before);
        QVERIFY(!session.canUndo());
    }

    void insertedCellsTakeTheDefaultStyle()
    {
        commands::DefaultStyles defaults;
        commands::setDefaultFrom(defaults, core::Style("rounded=1;strokeColor=#112233;"), commands::StyleTarget::Vertex);
        AppSettings().setDefaultStyles(defaults);
        DocumentSession session(test::documentFromXml(kDiagram));
        QVERIFY(actions::insertLine(session, false, QPointF(100, 100)));
        QCOMPARE(QString::fromStdString(inserted(session).style().value("strokeColor", "")), QStringLiteral("#112233"));
        QVERIFY(actions::insertTable(session, 1, 1, QPointF(100, 100)));
        QCOMPARE(QString::fromStdString(inserted(session).style().value("strokeColor", "")), QStringLiteral("#112233"));
    }

    void insertionGoesIntoTheEnteredGroupAndNotIntoALockedLayer()
    {
        DocumentSession session(test::documentFromXml(kDiagram));
        session.enterGroup("g");
        QVERIFY(actions::insertLine(session, false, QPointF(300, 250)));
        QCOMPARE(inserted(session).parent(), std::string("g")); // relative to the group's origin
        QCOMPARE(bounds(inserted(session)), QRectF(20, 50, 160, 10));

        session.exitGroup();
        session.setCurrentLayer("L2");
        QVERIFY(!actions::canInsert(session));
        const QByteArray before = test::savedBytes(session);
        QVERIFY(!actions::insertLine(session, false, QPointF(0, 0)));
        QVERIFY(!actions::insertTable(session, 2, 2, QPointF(0, 0)));
        QVERIFY(!actions::insertLinkCell(session, "https://example.com/", false, QPointF(0, 0)));
        QCOMPARE(test::savedBytes(session), before);
    }

    // --- tables -----------------------------------------------------------------------------
    void aTableHasRowsTimesColumnsCellsLaidOut()
    {
        DocumentSession session(test::documentFromXml(kDiagram));
        const QByteArray before = test::savedBytes(session);
        const std::size_t cellsBefore = cellCount(session);
        QVERIFY(actions::insertTable(session, 3, 4, QPointF(500, 400)));
        QCOMPARE(session.undoText(), QStringLiteral("Insert Table"));
        QCOMPARE(cellCount(session), cellsBefore + 1 + 3 + 12);

        const core::Page& page = *session.currentPage();
        const core::Cell& table = inserted(session);
        QCOMPARE(table.style().value("shape", ""), std::string("table"));
        QCOMPARE(table.style().value("childLayout", ""), std::string("tableLayout"));
        QCOMPARE(bounds(table), QRectF(380, 340, 240, 120));
        const auto rows = page.children(table.id());
        QCOMPARE(rows.size(), std::size_t(3));
        for (std::size_t r = 0; r < rows.size(); ++r) {
            QCOMPARE(rows[r]->style().value("shape", ""), std::string("tableRow"));
            QCOMPARE(bounds(*rows[r]), QRectF(0, 40.0 * r, 240, 40));
            const auto cells = page.children(rows[r]->id());
            QCOMPARE(cells.size(), std::size_t(4));
            for (std::size_t c = 0; c < cells.size(); ++c) {
                QCOMPARE(cells[c]->style().value("shape", ""), std::string("partialRectangle"));
                QCOMPARE(bounds(*cells[c]), QRectF(60.0 * c, 0, 60, 40));
            }
        }
        QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), before);
    }

    void tableSizeIsLimitedToTheRangeOfThePicker()
    {
        DocumentSession session(test::documentFromXml(kDiagram));
        QVERIFY(!actions::insertTable(session, 0, 3, {}));
        QVERIFY(!actions::insertTable(session, 3, 21, {}));
        QVERIFY(!session.canUndo());
        const std::size_t cellsBefore = cellCount(session);
        QVERIFY(actions::insertTable(session, 20, 20, QPointF(0, 0)));
        QCOMPARE(cellCount(session), cellsBefore + 1 + 20 + 400);
    }

    // --- links ------------------------------------------------------------------------------
    void aLinkIsAnAutosizedBlueTextCellWithTheLink()
    {
        DocumentSession session(test::documentFromXml(kDiagram));
        const QByteArray before = test::savedBytes(session);
        QVERIFY(!actions::insertLinkCell(session, "  ", false, {}));
        QVERIFY(!session.canUndo());
        // A title so long that no font fits it in the default 100 x 40 (a short one can snap to exactly that).
        const char* const longName = "a-link-title-that-is-far-wider-than-the-default-link-cell.html";
        const std::string address = std::string("https://example.com/docs/") + longName;
        QVERIFY(actions::insertLinkCell(session, address, true, QPointF(300, 100)));
        QCOMPARE(session.undoText(), QStringLiteral("Insert Link"));
        const core::Cell& link = inserted(session);
        QCOMPARE(QString::fromStdString(link.value()), QString::fromLatin1(longName)); // Graph.getLinkTitle
        QCOMPARE(QString::fromStdString(link.wrapper()->attributes.value("link")), QString::fromStdString(address));
        QCOMPARE(QString::fromStdString(link.wrapper()->attributes.value("linkTarget")), QStringLiteral("_blank"));
        QCOMPARE(link.style().value("fontColor", ""), std::string("#0000EE"));
        QCOMPARE(link.style().value("fontStyle", ""), std::string("4"));
        const QRectF box = bounds(link);
        QVERIFY(box.width() > 100); // sized to its label
        QVERIFY(std::abs((box.center().x()) - 300) <= 10 && std::abs((box.center().y()) - 100) <= 10);

        // An address that ends in a slash is its own title, a link to a page shows the page's name.
        QVERIFY(actions::insertLinkCell(session, "https://example.com/", false, {}));
        QCOMPARE(QString::fromStdString(inserted(session).value()), QStringLiteral("https://example.com/"));
        QVERIFY(actions::insertLinkCell(session, actions::pageLink("p2").c_str(), false, {}));
        QCOMPARE(QString::fromStdString(inserted(session).value()), QStringLiteral("Second"));
        for (int i = 0; i < 3; ++i)
            QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), before);
    }

    // --- images -----------------------------------------------------------------------------
    void anImageCellEmbedsThePayloadAtItsNaturalSizeScaledToFit()
    {
        QCOMPARE(actions::insertedImageSize(QSize(48, 32)), QSizeF(48, 32));
        QCOMPARE(actions::insertedImageSize(QSize(2400, 1200)), QSizeF(1200, 600));
        QCOMPARE(actions::insertedImageSize(QSize(600, 3000)), QSizeF(240, 1200));
        QCOMPARE(actions::insertedImageSize(QSize()), QSizeF(120, 120)); // size unknown

        DocumentSession session(test::documentFromXml(kDiagram));
        const QByteArray before = test::savedBytes(session);
        const std::optional<EmbeddedImage> image = embedImageBytes(pngBytes(48, 32));
        QVERIFY(image);
        QVERIFY(actions::insertImage(session, image->value, actions::insertedImageSize(image->size), QPointF(300, 300)));
        QCOMPARE(session.undoText(), QStringLiteral("Insert Image"));
        const core::Cell& cell = inserted(session);
        QCOMPARE(cell.style().value("shape", ""), std::string("image"));
        QCOMPARE(cell.style().value("aspect", ""), std::string("fixed"));
        QCOMPARE(cell.style().value("image", ""), image->value);
        QCOMPARE(bounds(cell), QRectF(280, 280, 48, 32));
        QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), before);
        QVERIFY(!actions::insertImage(session, "", QSizeF(10, 10), {}));
    }

    void aClipboardImageIsEmbeddedAsPng()
    {
        DocumentSession session(test::documentFromXml(kDiagram));
        QMimeData none;
        none.setText("no image here");
        QVERIFY(!actions::insertClipboardImage(session, none, {}));
        QVERIFY(!session.canUndo());

        QMimeData mime;
        mime.setImageData(redImage(64, 24));
        QVERIFY(actions::insertClipboardImage(session, mime, QPointF(300, 300)));
        const core::Cell& cell = inserted(session);
        QVERIFY(QString::fromStdString(cell.style().value("image", "")).startsWith("data:image/png,"));
        QCOMPARE(bounds(cell).width(), 64.0);
        QCOMPARE(bounds(cell).height(), 24.0);
        QVERIFY(QString::fromStdString(cell.styleText()).contains("labelBackgroundColor=default"));
    }

    // --- the picker -------------------------------------------------------------------------
    void thePickerFollowsTheMouseAndTheKeys()
    {
        TableSizePicker picker;
        picker.show();
        QCOMPARE(picker.accessibleName(), QStringLiteral("Table Size"));
        QCOMPARE(picker.sizeText(), QStringLiteral("1 column x 1 row"));
        QSignalSpy chosen(&picker, &TableSizePicker::sizeChosen);

        QTest::mouseMove(&picker, picker.cellCenter(2, 3));
        QCOMPARE(picker.rows(), 3);
        QCOMPARE(picker.columns(), 4);
        QCOMPARE(picker.sizeText(), QStringLiteral("4 columns x 3 rows"));
        QCOMPARE(picker.accessibleDescription(), picker.sizeText());
        QCOMPARE(chosen.count(), 0); // hovering chooses nothing
        QTest::mouseClick(&picker, Qt::LeftButton, Qt::NoModifier, picker.cellCenter(0, 1));
        QCOMPARE(chosen.count(), 1);
        QCOMPARE(chosen.last(), (QList<QVariant>{1, 2}));

        picker.setSize(1, 1);
        QTest::keyClick(&picker, Qt::Key_Right);
        QTest::keyClick(&picker, Qt::Key_Down);
        QTest::keyClick(&picker, Qt::Key_Down);
        QCOMPARE(picker.rows(), 3);
        QCOMPARE(picker.columns(), 2);
        QTest::keyClick(&picker, Qt::Key_Return);
        QCOMPARE(chosen.last(), (QList<QVariant>{3, 2}));
        QTest::keyClick(&picker, Qt::Key_Space);
        QCOMPARE(chosen.count(), 3);

        // The edges of the grid hand the key back (to the menu), the size stays.
        picker.setSize(1, 1);
        QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
        QApplication::sendEvent(&picker, &up);
        QVERIFY(!up.isAccepted());
        QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
        QApplication::sendEvent(&picker, &left);
        QVERIFY(!left.isAccepted());
        picker.setSize(20, 20);
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QApplication::sendEvent(&picker, &down);
        QVERIFY(!down.isAccepted());
        QCOMPARE(picker.rows(), 20);
        picker.setSize(99, -3);
        QCOMPARE(picker.rows(), 20);
        QCOMPARE(picker.columns(), 1);
    }

    void aPickerGrabForReview()
    {
        TableSizePicker picker;
        picker.setAutoFillBackground(true);
        picker.show();
        picker.setSize(3, 4); // showing starts the picker over at 1 x 1
        const QString dir = qEnvironmentVariable("QRAW_SCREENSHOT_DIR");
        QVERIFY(test::grabToPng(picker, "insert-table-picker") || dir.isEmpty());
        QVERIFY(!picker.grab().isNull());
    }

    // --- the main window ----------------------------------------------------------------------
    void theInsertMenuSitsBetweenArrangeAndExtras()
    {
        MainWindow window;
        QStringList titles;
        for (const QAction* action : window.menuBar()->actions())
            titles << action->text();
        const int insert = titles.indexOf(QStringLiteral("&Insert"));
        QVERIFY(insert > 0);
        QCOMPARE(titles.at(insert - 1), QStringLiteral("&Arrange"));
        QCOMPARE(titles.at(insert + 1), QStringLiteral("E&xtras"));

        const QMenu* menu = window.findChild<QMenu*>("menu.insert");
        QVERIFY(menu != nullptr);
        QStringList items;
        for (const QAction* action : menu->actions())
            items << action->text();
        QCOMPARE(items, (QStringList{"Text", "Image\u2026", "Link\u2026", "", "Horizontal Line", "Vertical Line", "", "Freehand", "Table", "", "Advanced"}));
        QVERIFY(menu->findChild<QMenu*>("menu.insertTable")->findChild<TableSizePicker*>() != nullptr);
    }

    void insertActionsFollowTheInsertionParent()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDiagram));
        const char* ids[] = {id::kInsertText, id::kInsertImage, id::kInsertLink, id::kInsertHorizontalLine, id::kInsertVerticalLine};
        for (const char* actionId : ids)
            QVERIFY(window.actions()[actionId]->isEnabled());
        window.session().setCurrentLayer("L2");
        for (const char* actionId : ids)
            QVERIFY2(!window.actions()[actionId]->isEnabled(), actionId);
    }

    void linesInsertAtTheViewportCentreFromTheMenu()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDiagram));
        window.show();
        const QPointF centre = window.canvas().centerInsertPoint();
        window.actions()[id::kInsertVerticalLine]->trigger();
        const QRectF box = bounds(inserted(window.session()));
        QVERIFY(std::abs(box.center().x() - centre.x()) <= 10);
        QVERIFY(std::abs(box.center().y() - centre.y()) <= 10);
        QVERIFY(window.session().undo());
        QVERIFY(!window.session().canUndo());
    }

    void theInsertPointIsTheMouseOnlyWhileItIsOverTheCanvas()
    {
        DocumentSession session(test::documentFromXml(kDiagram));
        auto canvas = test::makeCanvas(session);
        QCOMPARE(canvas->insertPoint(), canvas->centerInsertPoint());
        test::move(*canvas, QPointF(123, 234), Qt::NoModifier, Qt::NoButton);
        QVERIFY(QLineF(canvas->insertPoint(), QPointF(123, 234)).length() < 1);
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(canvas->viewport(), &leave);
        QCOMPARE(canvas->insertPoint(), canvas->centerInsertPoint());
    }

    void insertTextOpensTheLabelEditorAndInsertsOnCommit()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDiagram));
        window.show();
        window.actions()[id::kInsertText]->trigger();
        QCoreApplication::processEvents();
        LabelEditor* editor = window.canvas().interaction().labelEditor();
        QVERIFY(editor != nullptr);
        QVERIFY(editor->isEditing());
        QVERIFY(!window.session().canUndo()); // nothing is inserted before the text is committed
        editor->stopEditing(false);
        QCOMPARE(window.session().undoText(), QStringLiteral("Insert Text"));
        QCOMPARE(window.session().selection().cells().size(), std::size_t(1));
        QVERIFY(window.session().undo());
        QVERIFY(!window.session().canUndo());

        window.actions()[id::kInsertText]->trigger();
        QCoreApplication::processEvents();
        editor->stopEditing(true); // Escape inserts nothing
        QVERIFY(!window.session().canUndo());
    }

    void insertImageThroughTheDialog()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDiagram));
        QTemporaryDir dir;
        QFile file(dir.filePath("pic.png"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(pngBytes(80, 50));
        file.close();
        const QString path = file.fileName();

        // OK waits for an image.
        withModalDialog<EditImageDialog>([&](EditImageDialog& dialog) {
            QVERIFY(!dialog.insertion());
            QVERIFY(!dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled());
            QVERIFY(dialog.windowTitle() == "Insert Image");
            QVERIFY(dialog.useFile(path));
            QVERIFY(dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled());
        });
        window.actions()[id::kInsertImage]->trigger();
        QCOMPARE(window.session().undoText(), QStringLiteral("Insert Image"));
        QVERIFY(QString::fromStdString(inserted(window.session()).style().value("image", "")).startsWith("data:image/png,"));
        QCOMPARE(bounds(inserted(window.session())).width(), 80.0);

        window.session().undo();
        withModalDialog<EditImageDialog>([](EditImageDialog&) {}, QDialog::Rejected);
        window.actions()[id::kInsertImage]->trigger();
        QVERIFY(!window.session().canUndo());
    }

    void insertLinkThroughTheDialog()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDiagram));
        withModalDialog<EditLinkDialog>([](EditLinkDialog& dialog) {
            QVERIFY(dialog.windowTitle() == "Insert Link");
            dialog.findChild<QLineEdit*>("link.url")->setText("https://example.com/a/b.html");
        });
        window.actions()[id::kInsertLink]->trigger();
        QCOMPARE(window.session().undoText(), QStringLiteral("Insert Link"));
        QCOMPARE(QString::fromStdString(inserted(window.session()).value()), QStringLiteral("b.html"));

        window.session().undo();
        withModalDialog<EditLinkDialog>([](EditLinkDialog&) {}, QDialog::Accepted); // OK without an address
        window.actions()[id::kInsertLink]->trigger();
        QVERIFY(!window.session().canUndo());
    }

    void choosingASizeInTheTableMenuInsertsTheTable()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDiagram));
        auto* picker = window.findChild<QMenu*>("menu.insertTable")->findChild<TableSizePicker*>();
        QVERIFY(picker != nullptr);
        picker->setSize(2, 5);
        QTest::keyClick(picker, Qt::Key_Return);
        QCOMPARE(window.session().undoText(), QStringLiteral("Insert Table"));
        const core::Cell& table = inserted(window.session());
        QCOMPARE(bounds(table).width(), 300.0);
        QCOMPARE(bounds(table).height(), 80.0);
        QCOMPARE(window.session().currentPage()->descendants(table.id()).size(), std::size_t(2 + 10));
    }

    void theTablePickerWorksFromTheKeyboardInTheOpenMenu()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDiagram));
        window.show();
        QMenu* insert = window.findChild<QMenu*>("menu.insert");
        QMenu* table = insert->findChild<QMenu*>("menu.insertTable");
        insert->popup(window.mapToGlobal(QPoint(20, 20)));
        QVERIFY(QTest::qWaitForWindowExposed(insert));
        // Down to Table (the 7th entry), Right opens it and focuses the picker.
        for (int i = 0; i < 7; ++i)
            QTest::keyClick(insert, Qt::Key_Down);
        QTest::keyClick(insert, Qt::Key_Right);
        QVERIFY(table->isVisible());
        auto* picker = table->findChild<TableSizePicker*>();
        QCOMPARE(table->focusWidget(), picker); // keys reach the picker, not the menu behind it
        QTest::keyClick(picker, Qt::Key_Right);
        QTest::keyClick(picker, Qt::Key_Right);
        QTest::keyClick(picker, Qt::Key_Down);
        QCOMPARE(picker->sizeText(), QStringLiteral("3 columns x 2 rows"));
        test::grabToPng(*insert, "insert-menu");
        test::grabToPng(*table, "insert-table-menu");
        // At the first column Left is the menu's: it closes the submenu and leaves the picker.
        picker->setSize(2, 1);
        QTest::keyClick(picker, Qt::Key_Left);
        QVERIFY(!table->isVisible());
        QVERIFY(!window.session().canUndo());
        QTest::keyClick(insert, Qt::Key_Right);
        QTest::keyClick(picker, Qt::Key_Right);
        QTest::keyClick(picker, Qt::Key_Right);
        QCOMPARE(picker->sizeText(), QStringLiteral("3 columns x 1 row")); // reopening starts at 1 x 1
        picker->setSize(2, 3);
        QTest::keyClick(picker, Qt::Key_Return);
        QCOMPARE(window.session().undoText(), QStringLiteral("Insert Table"));
        QVERIFY(!table->isVisible());
        QVERIFY(!insert->isVisible());
        QCOMPARE(window.session().currentPage()->descendants(inserted(window.session()).id()).size(), std::size_t(2 + 6));
    }

    void aClipboardImagePastesAsAnImageCell()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDiagram));
        QGuiApplication::clipboard()->setImage(redImage(40, 40));
        window.actions()[id::kPaste]->trigger();
        QCOMPARE(window.session().undoText(), QStringLiteral("Insert Image"));
        QCOMPARE(inserted(window.session()).style().value("shape", ""), std::string("image"));
        // Cells on the clipboard stay a paste of cells.
        window.session().selection().set({"g"});
        window.actions()[id::kCopy]->trigger();
        window.actions()[id::kPaste]->trigger();
        QCOMPARE(window.session().undoText(), QStringLiteral("Paste"));
    }

    void theBackgroundContextMenuInsertsAtItsPoint()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDiagram));
        window.show();
        QTimer::singleShot(0, [&] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            QVERIFY(menu != nullptr);
            QVERIFY(menu->findChild<QMenu*>("menu.contextInsert") != nullptr);
            window.actions()[id::kInsertHorizontalLine]->trigger();
            menu->close();
        });
        emit window.canvas().contextMenuRequested(window.mapToGlobal(QPoint(300, 300)), QPointF(600, 500));
        QCOMPARE(window.session().undoText(), QStringLiteral("Insert Horizontal Line"));
        QCOMPARE(bounds(inserted(window.session())), QRectF(520, 500, 160, 10));
    }
};

SHELL_TEST_MAIN(TstInsertActions)
#include "tst_insert_actions.moc"
