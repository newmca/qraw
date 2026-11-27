// Insert > Advanced: the From Text, CSV and Mermaid dialogs (import_dialog.h) and the
// insertion of what they import (insert_actions.h insertImported, main_window_insert.cpp). The
// parsers have their own tests (tests/commands/tst_import_*); here: what the user sees and the
// document that comes out. Every insertion is one undo step that restores the document byte for
// byte, lands at the insert point, takes the default style and ends up selected.
#include "support/shell_env.h"
#include "support/test_support.h"

#include "qraw/commands/default_style.h"
#include "qraw/ui/app_settings.h"
#include "qraw/ui/import_dialog.h"
#include "qraw/ui/insert_actions.h"
#include "qraw/ui/main_window.h"

#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>

using namespace qraw;
using namespace qraw::ui;
namespace id = qraw::ui::action_id;

namespace {

const char* kDiagram =
    R"xml(<mxfile><diagram id="p1" name="Page-1"><mxGraphModel gridSize="10" grid="1"><root><mxCell id="0"/><mxCell id="1" parent="0"/>)xml"
    R"xml(<mxCell id="g" value="" style="group" vertex="1" connectable="0" parent="1"><mxGeometry x="200" y="200" width="200" height="100" as="geometry"/></mxCell>)xml"
    R"xml(<mxCell id="L2" value="Locked" style="locked=1;" parent="0"/>)xml"
    R"xml(</root></mxGraphModel></diagram></mxfile>)xml";

constexpr char kFlowchart[] = "graph TD\nA[Start] --> B{Ok?}\nB -->|yes| C((Done))\nclassDef hot fill:#ff0000\nclass C hot\n";
constexpr char kSequence[] = "sequenceDiagram\nA->>+B: hello\nB-->>-A: hi\n";

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

std::size_t cellCount(const DocumentSession& session)
{
    return session.currentPage()->descendants("0").size();
}

// The cell with this label in the current page.
const core::Cell* byValue(const DocumentSession& session, const std::string& value)
{
    for (const core::Cell* cell : session.currentPage()->descendants("0")) {
        if (cell->value() == value)
            return cell;
    }
    return nullptr;
}

QPlainTextEdit& textOf(ImportDialog& dialog)
{
    return *dialog.findChild<QPlainTextEdit*>("import.text");
}

} // namespace

class TstImportDialogs : public QObject {
    Q_OBJECT

private slots:
    void init() { AppSettings().setDefaultStyles({}); }

    // --- the dialogs ----------------------------------------------------------------------
    void everyDialogHasAMonospaceTextAreaAndTheExampleAsPlaceholder()
    {
        const commands::imports::LabelMeasure measure;
        for (const ImportDialog::Kind kind : {ImportDialog::Kind::FromText, ImportDialog::Kind::Csv, ImportDialog::Kind::Mermaid}) {
            ImportDialog dialog(kind, measure);
            QPlainTextEdit& edit = textOf(dialog);
            QCOMPARE(edit.font().family(), QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
            QVERIFY(edit.toPlainText().isEmpty());
            QVERIFY(!edit.placeholderText().isEmpty());
            QCOMPARE(edit.placeholderText(), dialog.example());
            QVERIFY(dialog.findChild<QPushButton*>("import.insert") != nullptr);
            QCOMPARE(dialog.findChild<QPushButton*>("import.insert")->text(), QStringLiteral("Insert"));
            QVERIFY(dialog.findChild<QDialogButtonBox*>() != nullptr);
            QVERIFY(!dialog.findChild<QListWidget*>("import.errors")->isVisibleTo(&dialog));
            QVERIFY(!edit.accessibleName().isEmpty());
        }
        QVERIFY(ImportDialog(ImportDialog::Kind::Csv, measure).example().startsWith(QStringLiteral("##\n## Example CSV import.")));
        QVERIFY(ImportDialog(ImportDialog::Kind::Mermaid, measure).example().startsWith(QStringLiteral("flowchart")));
    }

    void loadExampleFillsTheTextAreaAndTheExampleImports()
    {
        const commands::imports::LabelMeasure measure;
        for (const ImportDialog::Kind kind : {ImportDialog::Kind::FromText, ImportDialog::Kind::Csv, ImportDialog::Kind::Mermaid}) {
            ImportDialog dialog(kind, measure);
            dialog.findChild<QPushButton*>("import.example")->click();
            QCOMPARE(textOf(dialog).toPlainText(), dialog.example());
            dialog.tryInsert();
            QCOMPARE(dialog.result().errors.size(), std::size_t(0));
            QVERIFY(dialog.result().ok());
            QCOMPARE(dialog.result().warnings.size(), std::size_t(0));
        }
    }

    void theTypeOfFromTextChoosesTheLayoutsAndTheExample()
    {
        ImportDialog dialog(ImportDialog::Kind::FromText, {});
        auto* type = dialog.findChild<QComboBox*>("import.type");
        auto* layout = dialog.findChild<QComboBox*>("import.layout");
        QCOMPARE(dialog.textType(), commands::imports::TextType::List);
        QStringList lists;
        for (int i = 0; i < layout->count(); ++i)
            lists << layout->itemText(i);
        QCOMPARE(lists, (QStringList{"Vertical Tree", "Horizontal Tree", "Radial Tree"}));
        const QString listExample = textOf(dialog).placeholderText();

        type->setCurrentIndex(type->findData(static_cast<int>(commands::imports::TextType::Diagram)));
        QCOMPARE(dialog.textType(), commands::imports::TextType::Diagram);
        QStringList flows;
        for (int i = 0; i < layout->count(); ++i)
            flows << layout->itemText(i);
        QCOMPARE(flows, (QStringList{"Vertical Flow", "Horizontal Flow"}));
        QVERIFY(textOf(dialog).placeholderText() != listExample);
        QVERIFY(textOf(dialog).placeholderText().contains(QStringLiteral("->")));

        dialog.setTextLayout(commands::imports::TextLayout::RadialTree);
        QCOMPARE(dialog.textType(), commands::imports::TextType::List);
        QCOMPARE(dialog.textLayout(), commands::imports::TextLayout::RadialTree);
    }

    void insertAcceptsWhenTheTextImportsAndKeepsTheResult()
    {
        ImportDialog dialog(ImportDialog::Kind::Mermaid, {});
        dialog.setText(QString::fromLatin1(kFlowchart));
        dialog.show();
        dialog.tryInsert();
        QCOMPARE(dialog.result().errors.size(), std::size_t(0));
        QVERIFY(dialog.result().ok());
        QCOMPARE(dialog.result().cells.size(), std::size_t(5)); // 3 nodes, 2 edges
    }

    void errorsAreListedWithTheirLinesAndTheTextIsKept()
    {
        ImportDialog dialog(ImportDialog::Kind::Mermaid, {});
        const QString text = QStringLiteral("graph TD\nA --> B\nB --> [broken\nC --> D\n");
        dialog.setText(text);
        dialog.show();
        dialog.tryInsert();
        QVERIFY(dialog.isVisible()); // not accepted
        QCOMPARE(dialog.result().cells.size(), std::size_t(0));
        auto* errors = dialog.findChild<QListWidget*>("import.errors");
        QVERIFY(errors->isVisibleTo(&dialog));
        QCOMPARE(errors->count(), 1);
        QVERIFY2(errors->item(0)->text().startsWith(QStringLiteral("Line 3")), qPrintable(errors->item(0)->text()));
        QCOMPARE(textOf(dialog).toPlainText(), text);
        // The cursor is on the line of the error.
        QCOMPARE(textOf(dialog).textCursor().blockNumber(), 2);

        // Fixing the text and inserting again closes the dialog and hides nothing the user typed.
        dialog.setText(QStringLiteral("graph TD\nA --> B\n"));
        dialog.tryInsert();
        QVERIFY(!dialog.isVisible());
        QVERIFY(dialog.result().ok());
    }

    void everyKindShowsItsErrorsAndKeepsTheText()
    {
        const struct {
            ImportDialog::Kind kind;
            const char* text;
            int line;
        } cases[] = {
            {ImportDialog::Kind::FromText, "A -> B\nC ->\n", 2},
            {ImportDialog::Kind::Csv, "# connect: {\n#\nname\nA\n", 1},
            {ImportDialog::Kind::Mermaid, "classDiagram\nA <|-- B\n", 1},
        };
        for (const auto& c : cases) {
            ImportDialog dialog(c.kind, {});
            if (c.kind == ImportDialog::Kind::FromText)
                dialog.setTextLayout(commands::imports::TextLayout::VerticalFlow);
            dialog.setText(QString::fromLatin1(c.text));
            dialog.show();
            dialog.tryInsert();
            QVERIFY(dialog.isVisible());
            auto* errors = dialog.findChild<QListWidget*>("import.errors");
            QCOMPARE(errors->count(), 1);
            QVERIFY2(errors->item(0)->text().startsWith(QStringLiteral("Line %1").arg(c.line)), qPrintable(errors->item(0)->text()));
            QCOMPARE(textOf(dialog).toPlainText(), QString::fromLatin1(c.text));
        }
    }

    void anEmptyTextShowsAnErrorToo()
    {
        for (const ImportDialog::Kind kind : {ImportDialog::Kind::FromText, ImportDialog::Kind::Csv, ImportDialog::Kind::Mermaid}) {
            ImportDialog dialog(kind, {});
            dialog.show();
            dialog.tryInsert();
            QVERIFY(dialog.isVisible());
            QCOMPARE(dialog.findChild<QListWidget*>("import.errors")->count(), 1);
        }
    }

    // --- the insertion -------------------------------------------------------------------------
    void aFlowchartIsInsertedAsOneUndoStepAndSelected()
    {
        DocumentSession session(test::documentFromXml(kDiagram));
        const QByteArray before = test::savedBytes(session);
        const std::size_t cellsBefore = cellCount(session);
        const commands::imports::Result result = commands::imports::importMermaid(kFlowchart);
        QVERIFY(result.ok());
        QVERIFY(actions::insertImported(session, result, QPointF(600, 400), QStringLiteral("Insert Mermaid Diagram")));
        QCOMPARE(session.undoText(), QStringLiteral("Insert Mermaid Diagram"));
        QCOMPARE(cellCount(session), cellsBefore + 5);
        // Everything on the top level is selected: the 3 nodes and the 2 edges.
        QCOMPARE(session.selection().cells().size(), std::size_t(5));
        // Centred on the point (the grid snaps the corner).
        QRectF box;
        for (const std::string& cellId : session.selection().cells()) {
            const core::Cell& cell = *session.currentPage()->findCell(cellId);
            if (cell.isVertex()) {
                const core::Rect r = cell.geometry()->bounds();
                box = box.united(QRectF(r.x, r.y, r.width, r.height));
            }
        }
        QVERIFY(std::abs(box.center().x() - 600) <= 20);
        QVERIFY(std::abs(box.center().y() - 400) <= 20);
        // The edges are connected to the new vertices.
        for (const std::string& cellId : session.selection().cells()) {
            const core::Cell& cell = *session.currentPage()->findCell(cellId);
            if (cell.isEdge()) {
                QVERIFY(session.currentPage()->findCell(cell.source()) != nullptr);
                QVERIFY(session.currentPage()->findCell(cell.target()) != nullptr);
            }
        }
        QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), before);
        QVERIFY(!session.canUndo());
        QVERIFY(session.redo());
        QCOMPARE(session.selection().cells().size(), std::size_t(5));
    }

    void aSequenceDiagramKeepsItsMessagesConnected()
    {
        DocumentSession session(test::documentFromXml(kDiagram));
        const commands::imports::Result result = commands::imports::importMermaid(kSequence);
        QVERIFY(result.ok());
        QVERIFY(actions::insertImported(session, result, QPointF(300, 300), QStringLiteral("Insert")));
        int messages = 0;
        for (const core::Cell* cell : session.currentPage()->descendants("0")) {
            if (cell->isEdge()) {
                ++messages;
                QVERIFY(!cell->source().empty() && !cell->target().empty());
            }
        }
        QCOMPARE(messages, 2);
    }

    void csvCellsAreUserObjectsAndSurviveSaving()
    {
        DocumentSession session(test::documentFromXml(kDiagram));
        const commands::imports::Result result = commands::imports::importCsv(commands::imports::csvExample());
        QVERIFY2(result.ok(), "the example");
        QVERIFY(actions::insertImported(session, result, QPointF(500, 300), QStringLiteral("Insert CSV")));
        const QByteArray saved = test::savedBytes(session);
        QVERIFY(saved.contains("<UserObject label=\"%name%"));
        QVERIFY(saved.contains("placeholders=\"1\""));
        QVERIFY(saved.contains("name=\"Tessa Miller\""));
        // The document reads back, the cells are still wrapped.
        DocumentSession again(test::documentFromXml(saved.toStdString()));
        int objects = 0;
        for (const core::Cell* cell : again.currentPage()->descendants("0"))
            objects += cell->hasWrapper() && cell->wrapper()->tag == "UserObject";
        QCOMPARE(objects, 4);
    }

    void importedCellsTakeTheDefaultStyleButKeepTheirOwnKeys()
    {
        commands::DefaultStyles defaults;
        commands::setDefaultFrom(defaults, core::Style("rounded=1;strokeColor=#112233;fillColor=#445566;"), commands::StyleTarget::Vertex);
        AppSettings().setDefaultStyles(defaults);
        DocumentSession session(test::documentFromXml(kDiagram));
        const commands::imports::Result result = commands::imports::importMermaid(kFlowchart);
        QVERIFY(actions::insertImported(session, result, QPointF(600, 400), QStringLiteral("Insert")));
        const core::Cell* start = byValue(session, "Start");
        QVERIFY(start != nullptr);
        QCOMPARE(QString::fromStdString(start->style().value("strokeColor", "")), QStringLiteral("#112233"));
        // `class C hot` set fillColor itself: the default does not replace it.
        const core::Cell* done = byValue(session, "Done");
        QVERIFY(done != nullptr);
        QCOMPARE(QString::fromStdString(done->style().value("fillColor", "")), QStringLiteral("#ff0000"));
    }

    void insertionGoesIntoTheEnteredGroupAndNeverIntoALockedLayer()
    {
        DocumentSession session(test::documentFromXml(kDiagram));
        session.enterGroup("g");
        const commands::imports::Result result = commands::imports::importMermaid(kFlowchart);
        QVERIFY(actions::insertImported(session, result, QPointF(300, 250), QStringLiteral("Insert")));
        QCOMPARE(session.currentPage()->findCell(session.selection().first())->parent(), std::string("g"));
        session.exitGroup();
        session.setCurrentLayer("L2");
        const QByteArray before = test::savedBytes(session);
        QVERIFY(!actions::insertImported(session, result, QPointF(0, 0), QStringLiteral("Insert")));
        QCOMPARE(test::savedBytes(session), before);
        QVERIFY(!actions::insertImported(session, commands::imports::Result{}, QPointF(0, 0), QStringLiteral("Insert")));
    }

    void theLabelMeasureFollowsTheTextAndTheStyle()
    {
        DocumentSession session(test::documentFromXml(kDiagram));
        const commands::imports::LabelMeasure measure = actions::importLabelMeasure(session);
        const core::Point small = measure("whiteSpace=wrap;html=1;", "ab");
        const core::Point wide = measure("whiteSpace=wrap;html=1;", "a much longer label than the first");
        const core::Point tall = measure("whiteSpace=wrap;html=1;", "one<br>two<br>three");
        QVERIFY(wide.x > small.x + 50);
        QVERIFY(tall.y > small.y + 20);
        QVERIFY(small.x > 0 && small.y > 0);
        // A bigger font makes a bigger shape.
        QVERIFY(measure("html=1;fontSize=30;", "ab").x > small.x);
        QCOMPARE(measure("html=1;", "").x, 40.0); // an empty label: 4 grid steps square
    }

    // --- the menu -----------------------------------------------------------------------------
    void theAdvancedSubmenuHoldsTheThreeDialogs()
    {
        MainWindow window;
        const QMenu* insert = window.findChild<QMenu*>("menu.insert");
        const QMenu* advanced = insert->findChild<QMenu*>("menu.insertAdvanced");
        QVERIFY(advanced != nullptr);
        QStringList items;
        for (const QAction* action : advanced->actions())
            items << action->text();
        QCOMPARE(items, (QStringList{"From Text\u2026", "CSV\u2026", "Mermaid\u2026"}));
        QCOMPARE(advanced->actions().at(0), window.actions()[id::kInsertFromText]);
        QCOMPARE(advanced->actions().at(1), window.actions()[id::kInsertCsv]);
        QCOMPARE(advanced->actions().at(2), window.actions()[id::kInsertMermaid]);
    }

    void theActionsFollowTheInsertionParent()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDiagram));
        const char* ids[] = {id::kInsertFromText, id::kInsertCsv, id::kInsertMermaid};
        for (const char* actionId : ids)
            QVERIFY(window.actions()[actionId]->isEnabled());
        window.session().setCurrentLayer("L2");
        for (const char* actionId : ids)
            QVERIFY2(!window.actions()[actionId]->isEnabled(), actionId);
    }

    void theMermaidActionOpensTheDialogAndInsertsAtTheViewportCentre()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDiagram));
        window.show();
        const QByteArray before = test::savedBytes(window.session());
        const QPointF centre = window.canvas().centerInsertPoint();
        withModalDialog<ImportDialog>([](ImportDialog& dialog) {
            QCOMPARE(dialog.kind(), ImportDialog::Kind::Mermaid);
            dialog.setText(QString::fromLatin1(kSequence));
            dialog.tryInsert();
        });
        window.actions()[id::kInsertMermaid]->trigger();
        QCOMPARE(window.session().undoText(), QStringLiteral("Insert Mermaid Diagram"));
        QVERIFY(window.session().selection().cells().size() >= 4);
        QRectF box;
        for (const std::string& cellId : window.session().selection().cells()) {
            const core::Cell& cell = *window.session().currentPage()->findCell(cellId);
            if (cell.isVertex()) {
                const core::Rect r = cell.geometry()->bounds();
                box = box.united(QRectF(r.x, r.y, r.width, r.height));
            }
        }
        QVERIFY(std::abs(box.center().x() - centre.x()) <= 20);
        QVERIFY(std::abs(box.center().y() - centre.y()) <= 20);
        QVERIFY(window.session().undo());
        QCOMPARE(test::savedBytes(window.session()), before);
    }

    void cancellingTheDialogChangesNothing()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDiagram));
        window.show();
        for (const char* actionId : {id::kInsertFromText, id::kInsertCsv, id::kInsertMermaid}) {
            withModalDialog<ImportDialog>([](ImportDialog& dialog) { dialog.setText(QStringLiteral("graph TD\nA-->B")); },
                                          QDialog::Rejected);
            window.actions()[actionId]->trigger();
            QVERIFY(!window.session().canUndo());
        }
    }

    void fromTextInsertsAListWithTheChosenLayout()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDiagram));
        window.show();
        withModalDialog<ImportDialog>([](ImportDialog& dialog) {
            dialog.setTextLayout(commands::imports::TextLayout::HorizontalTree);
            dialog.setText(QStringLiteral("Root\n  A\n  B\n"));
            dialog.tryInsert();
        });
        window.actions()[id::kInsertFromText]->trigger();
        QCOMPARE(window.session().undoText(), QStringLiteral("Insert From Text"));
        const core::Cell* root = byValue(window.session(), "Root");
        const core::Cell* a = byValue(window.session(), "A");
        QVERIFY(root != nullptr && a != nullptr);
        QVERIFY(a->geometry()->x() > root->geometry()->x() + root->geometry()->width()); // horizontal tree: to the right
    }

    void warningsAreShownAfterTheInsertionWithoutStoppingIt()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDiagram));
        window.show();
        withModalDialog<ImportDialog>([](ImportDialog& dialog) {
            dialog.setText(QStringLiteral("# bogus: 1\nname\nA\n"));
            dialog.tryInsert();
        });
        window.actions()[id::kInsertCsv]->trigger();
        QCOMPARE(window.session().undoText(), QStringLiteral("Insert CSV"));
        auto* box = window.findChild<QMessageBox*>("import.warnings");
        QVERIFY(box != nullptr);
        QVERIFY(box->informativeText().contains(QStringLiteral("Line 1")));
        QVERIFY(box->informativeText().contains(QStringLiteral("bogus")));
        box->close();
    }

    void theDialogsAreGrabbedForReview()
    {
        ImportDialog dialog(ImportDialog::Kind::Mermaid, {});
        dialog.setText(QStringLiteral("graph TD\nA --> B\nB --> [broken\n"));
        dialog.show();
        dialog.tryInsert();
        test::grabToPng(dialog, "import-mermaid-error");
        ImportDialog csv(ImportDialog::Kind::Csv, {});
        csv.show();
        test::grabToPng(csv, "import-csv-dialog");
    }
};

SHELL_TEST_MAIN(TstImportDialogs)
#include "tst_import_dialogs.moc"
