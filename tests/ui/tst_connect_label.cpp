// inline label editing (LabelEditor) and the label markup
// conversion.

#include "support/test_support.h"

#include "../../src/ui/label_html.h"

#include "qraw/commands/ops_properties.h"
#include "qraw/ui/cell_queries.h"

#include <QGraphicsProxyWidget>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextEdit>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

namespace {

// The visible editor widget (a hidden one may still await deletion).
QTextEdit* editorWidget(CanvasView& view)
{
    for (QTextEdit* edit : view.viewport()->findChildren<QTextEdit*>()) {
        if (edit->isVisible())
            return edit;
    }
    return nullptr;
}

// The editor of a rotated label lives in the scene.
QGraphicsProxyWidget* sceneEditor(CanvasView& view)
{
    for (QGraphicsItem* item : view.scene()->items()) {
        if (auto* proxy = qgraphicsitem_cast<QGraphicsProxyWidget*>(item))
            return proxy;
    }
    return nullptr;
}

constexpr char kTextCellDiagram[] = R"(<mxfile><diagram id="p1" name="Page-1"><mxGraphModel grid="1" gridSize="10" page="1" pageScale="1" pageWidth="827" pageHeight="1169"><root><mxCell id="0"/><mxCell id="1" parent="0"/><mxCell id="t" value="x" style="text;html=1;align=center;verticalAlign=middle;resizable=0;points=[];autosize=1;strokeColor=none;fillColor=none;" vertex="1" parent="1"><mxGeometry x="100" y="100" width="40" height="20" as="geometry"/></mxCell></root></mxGraphModel></diagram></mxfile>)";

QString valueOf(const DocumentSession& session, const char* id)
{
    return QString::fromStdString(session.currentPage()->findCell(id)->value());
}

std::string exportOf(const QString& html)
{
    QTextDocument document;
    label::loadHtml(document, html.toStdString(), 1);
    return label::exportHtml(document, 1, Qt::black);
}

void commit(QTextEdit* edit)
{
    QTest::keyClick(edit, Qt::Key_Return, Qt::ControlModifier);
}

} // namespace

class TestConnectLabel : public QObject {
    Q_OBJECT

private slots:
    void editorExists()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        QVERIFY(labelEditorAvailable(*view));
    }

    void typingAndCtrlEnterCommit()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        LabelEditor* editor = view->interaction().labelEditor();
        const auto before = savedBytes(session);

        QVERIFY(editor->startEditing("a"));
        QVERIFY(editor->isEditing());
        QCOMPARE(QString::fromStdString(editor->editingCell()), QStringLiteral("a"));
        QTextEdit* edit = editorWidget(*view);
        QVERIFY(edit != nullptr);
        QCOMPARE(edit->toPlainText(), QStringLiteral("A"));
        QCOMPARE(edit->textCursor().selectedText(), QStringLiteral("A")); // selected on start
        grabToPng(*view, QStringLiteral("connect-label-editor"));

        QTest::keyClicks(edit, "Hello");
        QTest::keyClick(edit, Qt::Key_Return); // Enter alone is a newline
        QTest::keyClicks(edit, "World");
        QVERIFY(editor->isEditing());
        commit(edit);

        QVERIFY(!editor->isEditing());
        QCOMPARE(valueOf(session, "a"), QStringLiteral("Hello\nWorld"));
        QCOMPARE(session.undoText(), QStringLiteral("Edit Label"));
        QVERIFY(session.undo());
        QCOMPARE(savedBytes(session), before);
    }

    void escapeCancels()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        LabelEditor* editor = view->interaction().labelEditor();
        const auto before = savedBytes(session);

        QVERIFY(editor->startEditing("a"));
        QTextEdit* edit = editorWidget(*view);
        QTest::keyClicks(edit, "changed");
        QTest::keyClick(edit, Qt::Key_Escape);
        QVERIFY(!editor->isEditing());
        QCOMPARE(savedBytes(session), before);
        QVERIFY(!session.canUndo());
    }

    void unchangedTextRecordsNoEdit()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        LabelEditor* editor = view->interaction().labelEditor();
        QVERIFY(editor->startEditing("b")); // html=1
        commit(editorWidget(*view));
        QVERIFY(!session.canUndo());
    }

    void htmlCellCommitsBoldMarkup()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        LabelEditor* editor = view->interaction().labelEditor();

        QVERIFY(editor->startEditing("b"));
        QTextEdit* edit = editorWidget(*view);
        QTest::keyClick(edit, Qt::Key_B, Qt::ControlModifier); // the text is selected
        commit(edit);
        QCOMPARE(valueOf(session, "b"), QStringLiteral("<b>B</b>"));
    }

    void clickOutsideCommits()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        LabelEditor* editor = view->interaction().labelEditor();
        QVERIFY(editor->startEditing("a"));
        QTest::keyClicks(editorWidget(*view), "X");
        click(*view, {600, 500}); // blank canvas, outside the editor
        QVERIFY(!editor->isEditing());
        QCOMPARE(valueOf(session, "a"), QStringLiteral("X"));
    }

    void edgeLabelsAreEditable()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        LabelEditor* editor = view->interaction().labelEditor();
        QVERIFY(editor->startEditing("e"));
        QTextEdit* edit = editorWidget(*view);
        QVERIFY(edit != nullptr);
        QVERIFY(edit->width() >= 60);
        QTest::keyClicks(edit, "link");
        commit(edit);
        QCOMPARE(valueOf(session, "e"), QStringLiteral("link"));
    }

    void lockedAndNonEditableCellsRefuse()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        LabelEditor* editor = view->interaction().labelEditor();
        QVERIFY(session.edit(QStringLiteral("style"), [](commands::Transaction& tx) {
            return commands::ops::setStyle(tx, 0, {"a"}, "editable", "0");
        }));
        QVERIFY(session.edit(QStringLiteral("style"), [](commands::Transaction& tx) {
            return commands::ops::setStyle(tx, 0, {"b"}, "locked", "1");
        }));
        QVERIFY(!editor->startEditing("a"));
        QVERIFY(!editor->startEditing("b"));
        QVERIFY(!editor->startEditing("missing"));
        QVERIFY(!editor->isEditing());
    }

    void editorFollowsTheZoom()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        LabelEditor* editor = view->interaction().labelEditor();
        QVERIFY(editor->startEditing("a"));
        QTextEdit* edit = editorWidget(*view);
        const int pixelSize = edit->document()->defaultFont().pixelSize();
        const int width = edit->width();
        view->setZoom(2);
        QVERIFY(editor->isEditing());
        QCOMPARE(edit->document()->defaultFont().pixelSize(), pixelSize * 2);
        QVERIFY(edit->width() > width);
        editor->stopEditing(true);
    }

    // --- double click ---------------------------------------------------------------

    void doubleClickOnCellStartsEditing()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        doubleClick(*view, {100, 70});
        LabelEditor* editor = view->interaction().labelEditor();
        QVERIFY(editor->isEditing());
        QCOMPARE(QString::fromStdString(editor->editingCell()), QStringLiteral("a"));
        editor->stopEditing(true);
    }

    void doubleClickOnBlankInsertsTextInOneStep()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        LabelEditor* editor = view->interaction().labelEditor();
        const auto before = savedBytes(session);
        const std::size_t cells = session.currentPage()->cellCount();

        doubleClick(*view, {600, 400});
        QVERIFY(editor->isEditing());
        QCOMPARE(session.currentPage()->cellCount(), cells); // nothing is inserted yet
        QVERIFY(!session.canUndo());

        QTextEdit* edit = editorWidget(*view);
        QCOMPARE(edit->textCursor().selectedText(), QStringLiteral("Text"));
        QTest::keyClicks(edit, "Hello world");
        commit(edit);

        QCOMPARE(session.currentPage()->cellCount(), cells + 1);
        QCOMPARE(session.undoText(), QStringLiteral("Insert Text"));
        const core::Cell& text = *session.currentPage()->findCell(session.selection().first());
        QVERIFY(text.isVertex());
        QVERIFY(text.style().hasName("text"));
        QCOMPARE(QString::fromStdString(text.value()), QStringLiteral("Hello world"));
        const core::Rect bounds = text.geometry()->bounds();
        QVERIFY(bounds.width > 40); // autosized to the text
        QCOMPARE(bounds.x + bounds.width / 2, 600.0); // around the (grid snapped) pointer
        QCOMPARE(bounds.y + bounds.height / 2, 400.0);

        QVERIFY(session.undo()); // one step
        QCOMPARE(savedBytes(session), before);
        QVERIFY(!session.canUndo());
    }

    void escapeOrEmptyTextOnBlankInsertsNothing()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        LabelEditor* editor = view->interaction().labelEditor();
        const auto before = savedBytes(session);

        doubleClick(*view, {600, 400});
        QTest::keyClicks(editorWidget(*view), "typed");
        QTest::keyClick(editorWidget(*view), Qt::Key_Escape);
        QVERIFY(!editor->isEditing());
        QCOMPARE(savedBytes(session), before);
        QVERIFY(!session.canUndo());

        doubleClick(*view, {600, 400});
        QTextEdit* edit = editorWidget(*view);
        edit->clear();
        commit(edit);
        QVERIFY(!editor->isEditing());
        QCOMPARE(savedBytes(session), before);
        QVERIFY(!session.canUndo());
    }

    // --- transparent editor, hidden label, rotation, autosize ---------------------

    void editorIsTransparentAndHidesTheCellLabel()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        LabelEditor* editor = view->interaction().labelEditor();
        QVERIFY(session.edit(QStringLiteral("fill"), [](commands::Transaction& tx) {
            return commands::ops::setStyle(tx, 0, {"a"}, "fillColor", "#ff0000");
        }));
        QVERIFY(editor->startEditing("a"));
        QTextEdit* edit = editorWidget(*view);
        QVERIFY(!edit->viewport()->autoFillBackground());
        QCOMPARE(view->diagramScene().hiddenLabel(), std::string("a"));

        // The shape's fill shows through the editor (it covers the whole cell).
        const QImage image = view->grab().toImage();
        const QPoint corner = devicePixel(*view, QPointF(46, 46));
        QCOMPARE(image.pixelColor(corner), QColor(0xff, 0, 0));

        editor->stopEditing(true);
        QVERIFY(view->diagramScene().hiddenLabel().empty());
    }

    void rotatedLabelIsEditedRotated()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        LabelEditor* editor = view->interaction().labelEditor();
        QVERIFY(session.edit(QStringLiteral("rotate"), [](commands::Transaction& tx) {
            return commands::ops::setStyle(tx, 0, {"a"}, "rotation", "90");
        }));
        const auto before = savedBytes(session);

        QVERIFY(editor->startEditing("a"));
        QGraphicsProxyWidget* proxy = sceneEditor(*view);
        QVERIFY(proxy != nullptr);
        QVERIFY(editorWidget(*view) == nullptr); // not a child of the viewport
        QCOMPARE(qRound(proxy->rotation()), 90);
        QVERIFY(std::abs(proxy->sceneTransform().m12()) > 0.99); // turned in the scene
        // Rotated about the label anchor: the editor stays on the cell.
        QVERIFY(proxy->sceneBoundingRect().center().x() > 40 && proxy->sceneBoundingRect().center().x() < 160);

        auto* edit = qobject_cast<QTextEdit*>(proxy->widget());
        QVERIFY(edit != nullptr);
        QTest::keyClicks(edit, "turned");
        commit(edit);
        QVERIFY(!editor->isEditing());
        QVERIFY(sceneEditor(*view) == nullptr || !sceneEditor(*view)->isVisible());
        QCOMPARE(valueOf(session, "a"), QStringLiteral("turned"));
        QVERIFY(session.undo());
        QCOMPARE(savedBytes(session), before);
    }

    void autosizeGrowsTheCellInOneUndoStep()
    {
        DocumentSession session(documentFromXml(kTextCellDiagram));
        auto view = makeCanvas(session);
        LabelEditor* editor = view->interaction().labelEditor();
        const auto before = savedBytes(session);

        QVERIFY(editor->startEditing("t"));
        QTextEdit* edit = editorWidget(*view);
        QTest::keyClicks(edit, "A much longer label");
        commit(edit);

        const core::Rect bounds = session.currentPage()->findCell("t")->geometry()->bounds();
        QVERIFY(bounds.width > 100);
        QVERIFY(bounds.height >= 20);
        QCOMPARE(bounds.x + bounds.width / 2, 120.0); // the centre stays (align=center)
        QCOMPARE(bounds.y + bounds.height / 2, 110.0);
        QCOMPARE(session.undoText(), QStringLiteral("Edit Label"));
        QVERIFY(session.undo());
        QCOMPARE(savedBytes(session), before);
        QVERIFY(!session.canUndo());
    }

    void autosizeOfWrappedTextKeepsTheWidth()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        LabelEditor* editor = view->interaction().labelEditor();
        QVERIFY(session.edit(QStringLiteral("autosize"), [](commands::Transaction& tx) {
            return commands::ops::setStyle(tx, 0, {"a"}, "autosize", "1"); // 'a' has whiteSpace=wrap
        }));
        QVERIFY(editor->startEditing("a"));
        QTest::keyClicks(editorWidget(*view), "one two three four five six seven eight nine ten eleven twelve");
        commit(editorWidget(*view));
        const core::Rect bounds = session.currentPage()->findCell("a")->geometry()->bounds();
        QCOMPARE(bounds.x, 40.0);
        QCOMPARE(bounds.width, 120.0); // wrapped at the width, which stays
        QVERIFY(bounds.height > 60);   // so the height grows
    }

    void noAutosizeWithoutTheStyleFlag()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        LabelEditor* editor = view->interaction().labelEditor();
        QVERIFY(editor->startEditing("a"));
        QTest::keyClicks(editorWidget(*view), "A much longer label than the box");
        commit(editorWidget(*view));
        const core::Rect bounds = session.currentPage()->findCell("a")->geometry()->bounds();
        QCOMPARE(bounds.width, 120.0);
        QCOMPARE(bounds.height, 60.0);
    }

    void doubleClickOnGroupIsDeclined()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        LabelEditor* editor = view->interaction().labelEditor();
        const std::size_t cells = session.currentPage()->cellCount();

        CanvasEvent event;
        event.scenePos = {140, 250}; // inside 'g', outside its children
        event.scale = 1;
        QVERIFY(!editor->doubleClick(event));
        QVERIFY(!editor->isEditing());
        QCOMPARE(session.currentPage()->cellCount(), cells);
    }

    void enterStartsEditingTheSelection()
    {
        DocumentSession session(simpleDocument());
        auto view = makeCanvas(session);
        LabelEditor* editor = view->interaction().labelEditor();
        session.selection().set({"b"});
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QVERIFY(editor->keyPress(enter));
        QVERIFY(editor->isEditing());
        editor->stopEditing(true);
    }

    // --- markup conversion ----------------------------------------------------------

    void markupRoundTrips_data()
    {
        QTest::addColumn<QString>("html");
        QTest::addRow("plain") << "Hello";
        QTest::addRow("bold") << "a <b>bold</b> word";
        QTest::addRow("nested") << "<b><i>both</i></b>";
        QTest::addRow("underline strike") << "<u>u</u><s>s</s>";
        QTest::addRow("line break") << "one<br>two";
        QTest::addRow("divs") << "first<div>second</div><div><br></div><div>fourth</div>";
        QTest::addRow("entities") << "1 &lt; 2 &amp; 3 &gt; 2";
        QTest::addRow("spaces") << "a&nbsp;&nbsp;b";
        QTest::addRow("colour") << "<font color=\"#ff0000\">red</font> text";
        QTest::addRow("sub sup") << "x<sub>2</sub><sup>3</sup>";
        QTest::addRow("link") << "<a href=\"https://example.com/\">site</a>";
        QTest::addRow("list") << "<ul><li>one</li><li>two</li></ul>";
    }

    void markupRoundTrips()
    {
        QFETCH(QString, html);
        QCOMPARE(QString::fromStdString(exportOf(html)), html);
    }

    void fontSizesScaleWithZoom()
    {
        QTextDocument document;
        document.setDefaultFont([] {
            QFont f;
            f.setPixelSize(22);
            return f;
        }());
        label::loadHtml(document, "<span style=\"font-size: 20px;\">big</span>", 2);
        // Stored 20 px at zoom 2 is 40 px in the editor and 20 px again on export.
        QCOMPARE(document.begin().begin().fragment().charFormat().property(QTextFormat::FontPixelSize).toDouble(), 40.0);
        QCOMPARE(QString::fromStdString(label::exportHtml(document, 2, Qt::black)),
                 QStringLiteral("<span style=\"font-size: 20px;\">big</span>"));
    }
};

QTEST_MAIN(TestConnectLabel)
#include "tst_connect_label.moc"
