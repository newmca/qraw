// Cell data: Edit Data, Edit Link, Edit Tooltip, Edit Image (data_actions.h and the four
// dialogs), the cell tooltip on the canvas and the link icon. Every edit is one undo step that
// restores the document byte for byte.
#include "support/shell_env.h"
#include "support/test_support.h"

#include "qraw/commands/ops_properties.h"
#include "qraw/commands/transaction.h"
#include "qraw/format/codec.h"
#include "qraw/render/image_source.h"
#include "qraw/ui/cell_tooltip.h"
#include "qraw/ui/data_actions.h"
#include "qraw/ui/edit_data_dialog.h"
#include "qraw/ui/edit_image_dialog.h"
#include "qraw/ui/edit_link_dialog.h"
#include "qraw/ui/edit_tooltip_dialog.h"
#include "qraw/ui/image_embed.h"
#include "qraw/ui/main_window.h"

#include <QAction>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>

using namespace qraw;
using namespace qraw::ui;
namespace id = qraw::ui::action_id;

namespace {

// a: an <object> with data, a link, a tooltip and placeholders in an HTML label; b: a plain ellipse;
// c: an image shape; d: a plain cell in a locked layer; l: a label shape with an image; e: an ellipse with an image; p2 is a second page.
const char* kDataDiagram =
    R"xml(<mxfile><diagram id="p1" name="Page-1"><mxGraphModel gridSize="10" grid="1"><root><mxCell id="0"/><mxCell id="1" parent="0"/>)xml"
    R"xml(<object label="%name% (%role%)" name="Ada" role="Engineer" placeholders="1" tags="team core" tooltip="Ada leads &lt;b&gt;core&lt;/b&gt;" link="https://example.com/ada" id="a"><mxCell style="rounded=1;html=1;" vertex="1" parent="1"><mxGeometry x="60" y="60" width="160" height="80" as="geometry"/></mxCell></object>)xml"
    R"xml(<mxCell id="b" value="Plain" style="ellipse;" vertex="1" parent="1"><mxGeometry x="300" y="60" width="100" height="80" as="geometry"/></mxCell>)xml"
    R"xml(<mxCell id="c" value="" style="shape=image;image=https://example.com/x.png;" vertex="1" parent="1"><mxGeometry x="300" y="200" width="60" height="60" as="geometry"/></mxCell>)xml"
    R"xml(<mxCell id="f" value="Lock" style="rounded=0;locked=1;" vertex="1" parent="1"><mxGeometry x="40" y="300" width="60" height="30" as="geometry"/></mxCell>)xml"
    R"xml(<mxCell id="l" value="Label" style="shape=label;image=https://example.com/l.png;" vertex="1" parent="1"><mxGeometry x="140" y="300" width="100" height="40" as="geometry"/></mxCell>)xml"
    R"xml(<mxCell id="e" value="Disc" style="ellipse;image=https://example.com/e.png;" vertex="1" parent="1"><mxGeometry x="300" y="320" width="60" height="60" as="geometry"/></mxCell>)xml"
    R"xml(</root></mxGraphModel></diagram><diagram id="p2" name="Second"><mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/></root></mxGraphModel></diagram></mxfile>)xml";

const core::Cell& cell(const DocumentSession& session, const char* cellId)
{
    return *session.currentPage()->findCell(cellId);
}

QString attribute(const DocumentSession& session, const char* cellId, const char* name)
{
    const core::CellWrapper* wrapper = cell(session, cellId).wrapper();
    return wrapper != nullptr && wrapper->attributes.has(name) ? QString::fromStdString(wrapper->attributes.value(name))
                                                               : QStringLiteral("<none>");
}

std::vector<std::string> wrapperNames(const DocumentSession& session, const char* cellId)
{
    std::vector<std::string> names;
    for (const core::Attribute& a : cell(session, cellId).wrapper()->attributes)
        names.push_back(a.name);
    return names;
}

QString styleValue(const DocumentSession& session, const char* cellId, const char* key)
{
    return QString::fromStdString(cell(session, cellId).style().value(key, "<none>"));
}

QByteArray pngBytes(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// Runs `fn` on the next modal widget of type T (a dialog opened by exec() from an action), then closes it
// with `result`.
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

} // namespace

class TstDataActions : public QObject {
    Q_OBJECT

private slots:
    // --- Edit Data: the sheet ---------------------------------------------------------
    void sheetOfOneCellListsItsPropertiesByName()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        session.selection().set({"a"});
        const actions::DataSheet sheet = actions::readDataSheet(session);
        QVERIFY(!sheet.isDiagram);
        QCOMPARE(QString::fromStdString(sheet.id), QStringLiteral("a"));
        // label, placeholders and tags are not rows; the rest is sorted by name.
        QStringList names;
        for (const actions::DataRow& row : sheet.rows)
            names << QString::fromStdString(row.name);
        QCOMPARE(names, (QStringList{"link", "name", "role", "tooltip"}));
        QCOMPARE(QString::fromStdString(sheet.rows[1].value), QStringLiteral("Ada"));
        QCOMPARE(QString::fromStdString(sheet.tags), QStringLiteral("team core"));
        QVERIFY(sheet.placeholdersAvailable);
        QCOMPARE(sheet.placeholders, Qt::Checked);
    }

    void sheetOfSeveralCellsShowsTheUnionAndWhatDiffers()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        actions::DataChange change;
        change.written = {{"role", "Engineer", false}, {"team", "x", false}};
        session.selection().set({"b"});
        const actions::DataSheet second = actions::readDataSheet(session);
        QVERIFY(second.rows.empty());
        QVERIFY(actions::editData(session, second, change)); // b: role=Engineer, team=x

        session.selection().set({"a", "b"});
        const actions::DataSheet sheet = actions::readDataSheet(session);
        QVERIFY(sheet.id.empty());
        QCOMPARE(sheet.cells.size(), std::size_t(2));
        QStringList names;
        for (const actions::DataRow& row : sheet.rows)
            names << QString::fromStdString(row.name) + (row.mixed ? QStringLiteral("*") : QString());
        // role is the same on both, the others are on one cell only (so they differ).
        QCOMPARE(names, (QStringList{"link*", "name*", "role", "team*", "tooltip*"}));
        QCOMPARE(sheet.placeholders, Qt::PartiallyChecked);
        QVERIFY(sheet.tagsMixed);
    }

    void noSelectionEditsTheDiagramsOwnData()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        const QByteArray before = test::savedBytes(session);
        const actions::DataSheet sheet = actions::readDataSheet(session);
        QVERIFY(sheet.isDiagram);
        QVERIFY(sheet.id.empty());
        QVERIFY(!sheet.placeholdersAvailable);
        QCOMPARE(sheet.cells, (std::vector<std::string>{"0"}));

        actions::DataChange change;
        change.written = {{"version", "3", false}};
        QVERIFY(actions::editData(session, sheet, change));
        QCOMPARE(attribute(session, "0", "version"), QStringLiteral("3"));
        // The root cell is an <object> now and survives a save and load.
        const QByteArray saved = test::savedBytes(session);
        QVERIFY(saved.contains("<object version=\"3\" id=\"0\">") || saved.contains("<object label=\"\" version=\"3\" id=\"0\">"));
        DocumentSession reloaded(test::documentFromXml(saved.toStdString()));
        QCOMPARE(attribute(reloaded, "0", "version"), QStringLiteral("3"));
        QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), before);
    }

    // --- Edit Data: the edit --------------------------------------------------------------
    void firstPropertyWrapsAPlainCellAsDrawioDoes()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"b"});
        actions::DataChange change;
        change.written = {{"owner", "Ada", false}, {"cost", "3", false}};
        change.tags = "  one   two ";
        QVERIFY(actions::editData(session, actions::readDataSheet(session), change));
        QCOMPARE(session.undoText(), QStringLiteral("Edit Data"));
        QCOMPARE(QString::fromStdString(cell(session, "b").value()), QStringLiteral("Plain"));
        QCOMPARE(wrapperNames(session, "b"), (std::vector<std::string>{"label", "owner", "cost", "tags", "id"}));
        QCOMPARE(cell(session, "b").wrapper()->tag, std::string("object"));
        QCOMPARE(attribute(session, "b", "tags"), QStringLiteral("one two"));
        QVERIFY(test::savedBytes(session).contains("<object label=\"Plain\" owner=\"Ada\" cost=\"3\" tags=\"one two\" id=\"b\">"));
        QVERIFY(session.undo()); // one step
        QCOMPARE(test::savedBytes(session), before);
        QVERIFY(session.redo());
        QCOMPARE(attribute(session, "b", "owner"), QStringLiteral("Ada"));
    }

    void deletingEveryPropertyKeepsTheWrapper()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        session.selection().set({"a"});
        actions::DataChange change;
        change.removed = {"link", "name", "role", "tooltip"};
        change.tags = "";
        change.placeholders = false;
        QVERIFY(actions::editData(session, actions::readDataSheet(session), change));
        QVERIFY(cell(session, "a").hasWrapper());
        QCOMPARE(wrapperNames(session, "a"), (std::vector<std::string>{"label", "id"}));
        QCOMPARE(QString::fromStdString(cell(session, "a").value()), QStringLiteral("%name% (%role%)"));
    }

    void anUnchangedSheetMakesNoEdit()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        session.selection().set({"a"});
        QVERIFY(!actions::editData(session, actions::readDataSheet(session), {}));
        QVERIFY(!session.canUndo());
    }

    void placeholdersCheckBoxChangesTheLabelOnTheCanvas()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        auto canvas = test::makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();
        QCOMPARE(QString::fromStdString(scene.state("a")->labelText.lines.front()), QStringLiteral("Ada (Engineer)"));

        session.selection().set({"a"});
        actions::DataChange off;
        off.placeholders = false;
        QVERIFY(actions::editData(session, actions::readDataSheet(session), off));
        QCOMPARE(QString::fromStdString(scene.state("a")->labelText.lines.front()),
                 QStringLiteral("%name% (%role%)"));

        // A property and Placeholders on a plain cell: the label picks the value up.
        QVERIFY(session.edit(QStringLiteral("Label"), [](commands::Transaction& tx) {
                    return commands::ops::setLabel(tx, 0, "b", "Hi %who%");
                }).ok);
        QCOMPARE(QString::fromStdString(scene.state("b")->labelText.lines.front()), QStringLiteral("Hi %who%"));
        session.selection().set({"b"});
        actions::DataChange on;
        on.written = {{"who", "there", false}};
        on.placeholders = true;
        QVERIFY(actions::editData(session, actions::readDataSheet(session), on));
        QCOMPARE(QString::fromStdString(scene.state("b")->labelText.lines.front()), QStringLiteral("Hi there"));
        QCOMPARE(QString::fromStdString(cell(session, "b").value()), QStringLiteral("Hi %who%")); // the label is untouched
    }

    // --- Edit Data: the dialog --------------------------------------------------------------
    void dialogReportsOnlyWhatTheUserChanged()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        session.selection().set({"a"});
        EditDataDialog dialog(actions::readDataSheet(session));
        QVERIFY(dialog.change().written.empty() && dialog.change().removed.empty());
        QVERIFY(!dialog.change().placeholders && !dialog.change().tags);

        dialog.findChild<QPlainTextEdit*>("data.value.role")->setPlainText("Architect\nLead");
        dialog.findChild<QToolButton*>("data.delete.link")->click();
        QVERIFY(dialog.addProperty(QStringLiteral("team")));
        dialog.findChild<QPlainTextEdit*>("data.value.team")->setPlainText("core");
        dialog.findChild<QLineEdit*>("data.tags")->setText("a  b");
        dialog.findChild<QCheckBox*>("data.placeholders")->click();

        const actions::DataChange change = dialog.change();
        QCOMPARE(change.removed, (std::vector<std::string>{"link"}));
        QCOMPARE(change.written.size(), std::size_t(2));
        QCOMPARE(QString::fromStdString(change.written[0].name), QStringLiteral("role"));
        QCOMPARE(QString::fromStdString(change.written[0].value), QStringLiteral("Architect\nLead")); // multi-line
        QCOMPARE(QString::fromStdString(change.written[1].name), QStringLiteral("team"));
        QCOMPARE(change.placeholders, std::optional<bool>(false));
        QCOMPARE(change.tags, std::optional<std::string>("a b")); // normalised

        QVERIFY(actions::editData(session, actions::readDataSheet(session), change));
        QCOMPARE(attribute(session, "a", "role"), QStringLiteral("Architect\nLead"));
        QCOMPARE(attribute(session, "a", "link"), QStringLiteral("<none>"));
        QCOMPARE(attribute(session, "a", "team"), QStringLiteral("core"));
        QCOMPARE(attribute(session, "a", "placeholders"), QStringLiteral("<none>"));
        QCOMPARE(attribute(session, "a", "tags"), QStringLiteral("a b"));
        QCOMPARE(wrapperNames(session, "a").back(), std::string("id"));
    }

    void addPropertyRefusesBadAndReservedNames()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        session.selection().set({"b"});
        EditDataDialog dialog(actions::readDataSheet(session));
        for (const char* name : {"label", "id", "placeholders", "a:b", "1x", "a b", "a=b"}) {
            QVERIFY2(!dialog.addProperty(QString::fromLatin1(name)), name);
            QVERIFY2(!dialog.errorText().isEmpty(), name);
        }
        QVERIFY(dialog.change().written.empty());
        QVERIFY(dialog.addProperty(QStringLiteral("tooltip"))); // an ordinary name
        QVERIFY(dialog.errorText().isEmpty());
        // Adding an existing name only moves the focus: one row.
        QVERIFY(dialog.addProperty(QStringLiteral("tooltip")));
        QCOMPARE(dialog.change().written.size(), std::size_t(1));
        // Tags have their own field: adding "tags" does not make a row.
        QVERIFY(dialog.addProperty(QStringLiteral("tags")));
        QCOMPARE(dialog.change().written.size(), std::size_t(1));
    }

    void aPropertyDeletedAndAddedAgainIsWrittenEmpty()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        session.selection().set({"a"});
        EditDataDialog dialog(actions::readDataSheet(session));
        dialog.findChild<QToolButton*>("data.delete.role")->click();
        QVERIFY(dialog.addProperty(QStringLiteral("role")));
        const actions::DataChange change = dialog.change();
        QCOMPARE(change.removed, (std::vector<std::string>{"role"}));
        QCOMPARE(change.written.size(), std::size_t(1));
        QVERIFY(change.written[0].value.empty());
        QVERIFY(actions::editData(session, actions::readDataSheet(session), change));
        QCOMPARE(attribute(session, "a", "role"), QString());
    }

    void mixedValuesStayUnlessTyped()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        session.selection().set({"a"});
        actions::DataChange give;
        give.written = {{"name", "Bob", false}};
        session.selection().set({"b"});
        QVERIFY(actions::editData(session, actions::readDataSheet(session), give)); // b.name = Bob
        session.selection().set({"a", "b"});
        const actions::DataSheet sheet = actions::readDataSheet(session);
        {
            EditDataDialog dialog(sheet);
            QCOMPARE(dialog.findChild<QPlainTextEdit*>("data.value.name")->placeholderText(),
                     QStringLiteral("Multiple Values"));
            QVERIFY(dialog.change().written.empty()); // left empty: each keeps its own
        }
        EditDataDialog dialog(sheet);
        dialog.findChild<QPlainTextEdit*>("data.value.name")->setPlainText("Same");
        QVERIFY(actions::editData(session, sheet, dialog.change()));
        QCOMPARE(attribute(session, "a", "name"), QStringLiteral("Same"));
        QCOMPARE(attribute(session, "b", "name"), QStringLiteral("Same"));
        QCOMPARE(attribute(session, "a", "role"), QStringLiteral("Engineer")); // untouched
    }

    // --- Edit Link ----------------------------------------------------------------------------
    void editLinkWritesTheLinkAndTargetAndRemovesThem()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"b"});
        actions::LinkEdit edit;
        edit.setLink = true;
        edit.link = "https://example.org/";
        edit.setNewWindow = true;
        edit.newWindow = true;
        QVERIFY(actions::editLink(session, actions::readLinkSheet(session), edit));
        QCOMPARE(session.undoText(), QStringLiteral("Edit Link"));
        QCOMPARE(cell(session, "b").wrapper()->tag, std::string("UserObject")); // Graph.setAttributeForCell
        QCOMPARE(attribute(session, "b", "link"), QStringLiteral("https://example.org/"));
        QCOMPARE(attribute(session, "b", "linkTarget"), QStringLiteral("_blank"));

        edit.link.clear();
        edit.newWindow = false;
        QVERIFY(actions::editLink(session, actions::readLinkSheet(session), edit));
        QCOMPARE(attribute(session, "b", "link"), QStringLiteral("<none>"));
        QCOMPARE(attribute(session, "b", "linkTarget"), QStringLiteral("<none>"));
        QVERIFY(cell(session, "b").hasWrapper()); // not unwrapped
        QVERIFY(session.undo());
        QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), before);
    }

    void removingALinkFromAPlainCellWrapsNothing()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        session.selection().set({"b"});
        actions::LinkEdit edit;
        edit.setLink = true;
        edit.setNewWindow = true;
        QVERIFY(!actions::editLink(session, actions::readLinkSheet(session), edit));
        QVERIFY(!cell(session, "b").hasWrapper());
    }

    void editLinkSkipsLockedCellsAndKeepsMixedLinks()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        session.selection().set({"a", "b", "f"});
        const actions::LinkSheet sheet = actions::readLinkSheet(session);
        QCOMPARE(sheet.cells.size(), std::size_t(2)); // f is locked
        QVERIFY(sheet.linkMixed);
        QVERIFY(sheet.link.empty());
        // The dialog: untouched, a mixed link stays; typed, it is written to all.
        EditLinkDialog dialog(sheet, {{"p1", "Page-1"}, {"p2", "Second"}});
        QVERIFY(!dialog.edit().setLink);
        dialog.findChild<QLineEdit*>("link.url")->setText("mailto:a@b.c");
        QVERIFY(dialog.edit().setLink);
        QVERIFY(actions::editLink(session, sheet, dialog.edit()));
        QCOMPARE(attribute(session, "a", "link"), QStringLiteral("mailto:a@b.c"));
        QCOMPARE(attribute(session, "b", "link"), QStringLiteral("mailto:a@b.c"));
        QCOMPARE(attribute(session, "f", "link"), QStringLiteral("<none>"));
        QCOMPARE(attribute(session, "a", "tooltip"), QStringLiteral("Ada leads <b>core</b>")); // the rest untouched
    }

    void linkDialogOffersThePagesOfTheDocument()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        session.selection().set({"b"});
        EditLinkDialog dialog(actions::readLinkSheet(session), {{"p1", "Page-1"}, {"p2", "Second"}});
        auto* pages = dialog.findChild<QComboBox*>("link.page");
        QCOMPARE(pages->count(), 2);
        dialog.findChild<QRadioButton*>("link.pageChoice")->setChecked(true);
        pages->setCurrentIndex(1);
        QVERIFY(!dialog.findChild<QCheckBox*>("link.newWindow")->isEnabled()); // a page opens in this window
        const actions::LinkEdit edit = dialog.edit();
        QVERIFY(edit.setLink);
        QCOMPARE(QString::fromStdString(edit.link), QStringLiteral("data:page/id,p2"));
        QVERIFY(actions::editLink(session, actions::readLinkSheet(session), edit));

        // Reopened, the page is chosen again.
        EditLinkDialog again(actions::readLinkSheet(session), {{"p1", "Page-1"}, {"p2", "Second"}});
        QVERIFY(again.findChild<QRadioButton*>("link.pageChoice")->isChecked());
        QCOMPARE(again.findChild<QComboBox*>("link.page")->currentIndex(), 1);
    }

    void followingAPageLinkSwitchesThePage()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        QCOMPARE(session.currentPageIndex(), std::size_t(0));
        QVERIFY(actions::followLink(session, "data:page/id,p2"));
        QCOMPARE(session.currentPageIndex(), std::size_t(1));
        QCOMPARE(actions::linkDisplayText(session, "data:page/id,p1"), QStringLiteral("Page: Page-1"));
    }

    void linksAreOpenedOnlyWhenTheyAreWebOrMail()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        QList<QUrl> opened;
        const auto open = [&](const QUrl& url) {
            opened << url;
            return true;
        };
        QVERIFY(actions::followLink(session, "https://example.com/a?b=1", open));
        QVERIFY(actions::followLink(session, "http://example.com", open));
        QVERIFY(actions::followLink(session, "mailto:a@b.c?subject=hi", open));
        QCOMPARE(opened.size(), 3);

        QSignalSpy status(&session, &DocumentSession::statusMessage);
        for (const char* blocked : {"javascript:alert(1)", "file:///etc/hosts", "relative/path.drawio", "data:page/id,missing",
                                    "ftp://example.com/x", "data:text/html,<b>x</b>"}) {
            QVERIFY2(!actions::followLink(session, blocked, open), blocked);
        }
        QCOMPARE(opened.size(), 3); // nothing else was handed to the desktop
        QCOMPARE(status.size(), 6);
        QCOMPARE(session.currentPageIndex(), std::size_t(0));
        QVERIFY(actions::resolveLink(session, "").kind == actions::LinkTarget::Kind::None);
    }

    // --- Edit Tooltip -------------------------------------------------------------------------
    void editTooltipStoresAndClearsTheText()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        const QByteArray before = test::savedBytes(session);
        session.selection().set({"b"});
        QVERIFY(actions::canEditTooltip(session));
        const std::optional<actions::TooltipSheet> sheet = actions::readTooltipSheet(session);
        QVERIFY(sheet && sheet->text.empty() && !sheet->html);
        QVERIFY(actions::editTooltip(session, "b", "Line one\nLine two"));
        QCOMPARE(attribute(session, "b", "tooltip"), QStringLiteral("Line one\nLine two"));
        QCOMPARE(session.undoText(), QStringLiteral("Edit Tooltip"));
        QVERIFY(actions::editTooltip(session, "b", ""));
        QCOMPARE(attribute(session, "b", "tooltip"), QStringLiteral("<none>"));
        QVERIFY(session.undo());
        QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), before);

        session.selection().set({"a"});
        QVERIFY(actions::readTooltipSheet(session)->html); // a's label is HTML
        session.selection().set({"a", "b"});
        QVERIFY(!actions::canEditTooltip(session)); // exactly one cell
        session.selection().set({"f"});
        QVERIFY(!actions::canEditTooltip(session)); // locked
    }

    void tooltipDialogShowsTheCurrentText()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        session.selection().set({"a"});
        EditTooltipDialog dialog(*actions::readTooltipSheet(session));
        QCOMPARE(QString::fromStdString(dialog.text()), QStringLiteral("Ada leads <b>core</b>"));
        QVERIFY(dialog.findChild<QLabel*>("tooltip.hint") != nullptr); // an HTML label: HTML is allowed
    }

    void theCanvasShowsACellsTooltipOnHover()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        auto canvas = test::makeCanvas(session);
        // a has a tooltip (HTML, with the link first); b is plain: none.
        const QString htmlTip = canvas->tooltipAt(QPointF(80, 100));
        QVERIFY2(htmlTip.contains("<b>core</b>"), qPrintable(htmlTip));
        QVERIFY2(htmlTip.contains("https://example.com/ada"), qPrintable(htmlTip));
        QVERIFY(canvas->tooltipAt(QPointF(350, 100)).isEmpty());
        QVERIFY(canvas->tooltipAt(QPointF(600, 500)).isEmpty()); // blank canvas

        // The real event: QHelpEvent on the viewport shows a QToolTip with the text.
        const QPoint pos = canvas->mapFromScene(QPointF(80, 100));
        QHelpEvent help(QEvent::ToolTip, pos, canvas->viewport()->mapToGlobal(pos));
        QApplication::sendEvent(canvas->viewport(), &help);
        QVERIFY(QToolTip::isVisible());
        QVERIFY2(QToolTip::text().contains("core"), qPrintable(QToolTip::text()));
        QToolTip::hideText();

        QHelpEvent blank(QEvent::ToolTip, canvas->mapFromScene(QPointF(600, 500)),
                         canvas->viewport()->mapToGlobal(canvas->mapFromScene(QPointF(600, 500))));
        QApplication::sendEvent(canvas->viewport(), &blank);
        QTRY_VERIFY_WITH_TIMEOUT(!QToolTip::isVisible(), 2000); // hides after a short delay
    }

    void tooltipTextRules()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        const core::Page& page = *session.currentPage();
        const render::PlaceholderVariables vars;
        // Plain text keeps its line breaks and is escaped; HTML is sanitised.
        DocumentSession edit(test::documentFromXml(kDataDiagram));
        edit.selection().set({"b"});
        QVERIFY(actions::editTooltip(edit, "b", "a < b\nsecond"));
        const QString plain = cellTooltip(*edit.currentPage(), *edit.currentPage()->findCell("b"), vars);
        QVERIFY2(plain.contains("a &lt; b<br>second"), qPrintable(plain));
        QVERIFY(actions::editTooltip(edit, "b", "<b>x</b><script>alert(1)</script>"));
        const QString html = cellTooltip(*edit.currentPage(), *edit.currentPage()->findCell("b"), vars);
        QVERIFY2(html.contains("<b>x</b>") && !html.contains("script"), qPrintable(html));
        // Without a tooltip the properties are listed; built-ins and the link are not among them.
        QVERIFY(actions::editTooltip(edit, "b", ""));
        actions::DataChange change;
        change.written = {{"zeta", "2", false}, {"alpha", "1 < 2", false}};
        edit.selection().set({"b"});
        QVERIFY(actions::editData(edit, actions::readDataSheet(edit), change));
        const QString lines = cellTooltip(*edit.currentPage(), *edit.currentPage()->findCell("b"), vars);
        QVERIFY2(lines.indexOf("alpha") < lines.indexOf("zeta"), qPrintable(lines));
        QVERIFY2(lines.contains("<b>alpha:</b> 1 &lt; 2"), qPrintable(lines));
        QVERIFY(!lines.contains("label"));
        // %placeholders% in a tooltip are replaced when the cell uses them.
        const core::Cell& a = *page.findCell("a");
        QVERIFY(render::cellUsesPlaceholders(a));
    }

    // --- Edit Image ---------------------------------------------------------------------------
    void embeddedImagesUseDrawiosFormatAndReadBack()
    {
        const QByteArray png = pngBytes(7, 5);
        QString error;
        const std::optional<EmbeddedImage> image = embedImageBytes(png, &error);
        QVERIFY2(image, qPrintable(error));
        // draw.io's form: no ";base64", because ';' separates style keys.
        QVERIFY(QString::fromStdString(image->value).startsWith("data:image/png,"));
        QVERIFY(!QString::fromStdString(image->value).contains(';'));
        QCOMPARE(image->size, QSize(7, 5));
        const std::optional<render::ImageData> back = render::resolveImage(image->value, render::ImageResolveContext{});
        QVERIFY(back);
        QCOMPARE(back->bytes, png);

        const std::optional<EmbeddedImage> svg =
            embedImageBytes(QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"30\" height=\"20\"/>"));
        QVERIFY(svg);
        QVERIFY(QString::fromStdString(svg->value).startsWith("data:image/svg+xml,"));
        QCOMPARE(svg->size, QSize(30, 20));
        QCOMPARE(naturalImageSize(svg->value, QString()), QSize(30, 20));

        QVERIFY(!embedImageBytes(QByteArray("not an image"), &error));
        QVERIFY(!error.isEmpty());
    }

    void aTooLargeImageIsRefusedWithAClearMessage()
    {
        QString error;
        const QByteArray big(static_cast<qsizetype>(maxEmbeddedImageBytes()) + 1, 'x');
        QVERIFY(!embedImageBytes(big, &error));
        QVERIFY2(error.contains("more than"), qPrintable(error));

        QTemporaryDir dir;
        QFile file(dir.filePath("big.png"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write(big) == big.size());
        file.close();
        QVERIFY(!embedImageFile(file.fileName(), &error));
        QVERIFY2(error.contains("more than"), qPrintable(error));
    }

    void editImageSetsTheImageTheShapeAndTheSize()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        const QByteArray before = test::savedBytes(session);
        const std::optional<EmbeddedImage> image = embedImageBytes(pngBytes(48, 32));
        QVERIFY(image);

        session.selection().set({"b"}); // a plain ellipse has no image
        QVERIFY(actions::canEditImage(session) == false);
        session.selection().set({"b", "c", "l", "e"});
        QVERIFY(actions::canEditImage(session)); // c, l and e qualify
        QCOMPARE(actions::readImageSheet(session).cells, (std::vector<std::string>{"c", "l", "e"}));

        actions::ImageEdit edit;
        edit.image = image->value;
        edit.size = std::make_pair(48.0, 32.0);
        QVERIFY(actions::editImage(session, edit));
        QCOMPARE(session.undoText(), QStringLiteral("Edit Image"));
        QCOMPARE(QString::fromStdString(cell(session, "c").style().value("image", "")), QString::fromStdString(image->value));
        QCOMPARE(styleValue(session, "c", "shape"), QStringLiteral("image"));
        QCOMPARE(styleValue(session, "l", "shape"), QStringLiteral("label")); // a label shape stays one
        QCOMPARE(styleValue(session, "e", "shape"), QStringLiteral("image")); // an ellipse becomes an image shape (Actions.js)
        QCOMPARE(styleValue(session, "b", "image"), QStringLiteral("<none>")); // not selected-capable
        QCOMPARE(cell(session, "c").geometry()->width(), 48.0);
        QCOMPARE(cell(session, "l").geometry()->height(), 32.0);
        QVERIFY(session.undo()); // one step for all of it
        QCOMPARE(test::savedBytes(session), before);
    }

    void clearingTheImageRemovesItAndTheImageShape()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        session.selection().set({"c"});
        actions::ImageEdit edit;
        edit.image = std::string();
        QVERIFY(actions::editImage(session, edit));
        QCOMPARE(styleValue(session, "c", "image"), QStringLiteral("<none>"));
        QCOMPARE(styleValue(session, "c", "shape"), QStringLiteral("<none>"));
    }

    void anEmbeddedImageIsStoredOnceAndSharedByUndo()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        session.selection().set({"c"});
        const std::optional<EmbeddedImage> image = embedImageBytes(pngBytes(600, 400));
        QVERIFY(image);
        actions::ImageEdit edit;
        edit.image = image->value;
        QVERIFY(actions::editImage(session, edit));
        const core::Style style = cell(session, "c").style();
        QVERIFY(style.findValue("image") != nullptr);
        // The same buffer is read back by the sheet, not a second copy of the payload.
        QCOMPARE(actions::readImageSheet(session).image.size(), image->value.size());
        QVERIFY(session.undo());
        QVERIFY(session.redo());
        const core::Style redone = cell(session, "c").style();
        QVERIFY(redone.findValue("image") != nullptr);
        QCOMPARE(std::string(*redone.findValue("image")).size(), image->value.size());
    }

    void imageDialogEmbedsAFileAndOffersTheOriginalSize()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        session.selection().set({"c"});
        QTemporaryDir dir;
        QFile file(dir.filePath("pic.png"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(pngBytes(24, 12));
        file.close();

        EditImageDialog dialog(actions::readImageSheet(session), dir.path());
        auto* url = dialog.findChild<QLineEdit*>("image.url");
        QCOMPARE(url->text(), QStringLiteral("https://example.com/x.png"));
        auto* reset = dialog.findChild<QCheckBox*>("image.resetSize");
        QVERIFY(!reset->isEnabled()); // an http image is never fetched: its size is unknown
        QVERIFY(!dialog.edit().image); // untouched: nothing changes

        QVERIFY(dialog.useFile(file.fileName()));
        QVERIFY(reset->isEnabled());
        reset->setChecked(true);
        const actions::ImageEdit edit = dialog.edit();
        QVERIFY(edit.image && QString::fromStdString(*edit.image).startsWith("data:image/png,"));
        QCOMPARE(edit.size, (std::optional<std::pair<double, double>>(std::make_pair(24.0, 12.0))));
        QVERIFY(actions::editImage(session, edit));
        QCOMPARE(cell(session, "c").geometry()->width(), 24.0);

        // Reopened: the embedded image is a summary, not a megabyte of text in the field.
        EditImageDialog reopened(actions::readImageSheet(session), dir.path());
        QVERIFY(reopened.findChild<QLineEdit*>("image.url")->text().size() < 200);

        dialog.removeImage();
        QCOMPARE(dialog.edit().image, std::optional<std::string>(std::string()));
        QVERIFY(!dialog.useFile(dir.filePath("missing.png")));
        QVERIFY(!dialog.errorText().isEmpty());
    }

    void imageDialogRefusesATooLargeFile()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        session.selection().set({"c"});
        QTemporaryDir dir;
        QFile file(dir.filePath("huge.png"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(static_cast<qsizetype>(maxEmbeddedImageBytes()) + 1, 'x'));
        file.close();
        EditImageDialog dialog(actions::readImageSheet(session), dir.path());
        QVERIFY(!dialog.useFile(file.fileName()));
        QVERIFY(dialog.errorText().contains("more than"));
        QVERIFY(!dialog.edit().image); // the dialog keeps what it had
    }

    // --- the main window --------------------------------------------------------------------
    void actionsFollowTheSelection()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDataDiagram));
        const auto enabled = [&](const char* actionId) { return window.actions()[actionId]->isEnabled(); };
        // Nothing selected: Edit Data edits the diagram, the others wait for a cell.
        QVERIFY(enabled(id::kEditData));
        QVERIFY(!enabled(id::kEditLink) && !enabled(id::kEditTooltip) && !enabled(id::kEditImage) && !enabled(id::kOpenLink));
        window.session().selection().set({"a"});
        QVERIFY(enabled(id::kEditLink) && enabled(id::kEditTooltip) && enabled(id::kOpenLink));
        QVERIFY(!enabled(id::kEditImage)); // no image on this shape
        window.session().selection().set({"a", "b"});
        QVERIFY(enabled(id::kEditLink) && !enabled(id::kEditTooltip) && !enabled(id::kOpenLink));
        window.session().selection().set({"c"});
        QVERIFY(enabled(id::kEditImage));
        window.session().selection().set({"f"}); // locked
        QVERIFY(!enabled(id::kEditLink) && !enabled(id::kEditTooltip) && enabled(id::kEditData));
        QCOMPARE(window.actions()[id::kEditData]->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_M));
        QCOMPARE(window.actions()[id::kEditData]->text(), QStringLiteral("Edit Data\u2026"));
    }

    void editDataThroughTheWindowIsOneUndoStep()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDataDiagram));
        window.session().selection().set({"b"});
        const QByteArray before = test::savedBytes(window.session());
        withModalDialog<EditDataDialog>([](EditDataDialog& dialog) {
            QVERIFY(dialog.addProperty(QStringLiteral("owner")));
            dialog.findChild<QPlainTextEdit*>("data.value.owner")->setPlainText("Ada");
        });
        window.actions()[id::kEditData]->trigger();
        QCOMPARE(attribute(window.session(), "b", "owner"), QStringLiteral("Ada"));
        QVERIFY(window.session().undo());
        QCOMPARE(test::savedBytes(window.session()), before);
        QVERIFY(!window.session().canUndo());
    }

    void cancellingADialogChangesNothing()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDataDiagram));
        window.session().selection().set({"b"});
        const QByteArray before = test::savedBytes(window.session());
        withModalDialog<EditDataDialog>(
            [](EditDataDialog& dialog) { dialog.addProperty(QStringLiteral("owner")); }, QDialog::Rejected);
        window.actions()[id::kEditData]->trigger();
        withModalDialog<EditLinkDialog>(
            [](EditLinkDialog& dialog) { dialog.findChild<QLineEdit*>("link.url")->setText("https://x.y"); },
            QDialog::Rejected);
        window.actions()[id::kEditLink]->trigger();
        withModalDialog<EditTooltipDialog>(
            [](EditTooltipDialog& dialog) { dialog.findChild<QPlainTextEdit*>("tooltip.text")->setPlainText("t"); },
            QDialog::Rejected);
        window.actions()[id::kEditTooltip]->trigger();
        QCOMPARE(test::savedBytes(window.session()), before);
        QVERIFY(!window.session().canUndo());
    }

    void editLinkAndTooltipThroughTheWindow()
    {
        MainWindow window;
        window.session().resetDocument(test::documentFromXml(kDataDiagram));
        window.session().selection().set({"b"});
        withModalDialog<EditLinkDialog>([](EditLinkDialog& dialog) {
            dialog.findChild<QRadioButton*>("link.pageChoice")->setChecked(true);
            dialog.findChild<QComboBox*>("link.page")->setCurrentIndex(1);
        });
        window.actions()[id::kEditLink]->trigger();
        QCOMPARE(attribute(window.session(), "b", "link"), QStringLiteral("data:page/id,p2"));
        withModalDialog<EditTooltipDialog>(
            [](EditTooltipDialog& dialog) { dialog.findChild<QPlainTextEdit*>("tooltip.text")->setPlainText("Hello"); });
        window.actions()[id::kEditTooltip]->trigger();
        QCOMPARE(attribute(window.session(), "b", "tooltip"), QStringLiteral("Hello"));

        // Open Link follows the page link: the second page is shown.
        QVERIFY(window.actions()[id::kOpenLink]->isEnabled());
        window.actions()[id::kOpenLink]->trigger();
        QCOMPARE(window.session().currentPageIndex(), std::size_t(1));
    }

    void theLinkIconOfASelectedCellFollowsItsLink()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        auto canvas = test::makeCanvas(session);
        session.selection().set({"a"});
        // The icon sits in the top-right corner: 16 px, inset 4 px (link_badge.cpp).
        actions::LinkEdit toPage;
        toPage.setLink = true;
        toPage.link = "data:page/id,p2";
        QVERIFY(actions::editLink(session, actions::readLinkSheet(session), toPage));
        session.selection().set({"a"});
        const QPointF icon(60 + 160 - 4 - 8, 60 + 4 + 8);
        test::move(*canvas, icon, Qt::NoModifier, Qt::NoButton);
        QCOMPARE(canvas->interaction().cursor(), Qt::PointingHandCursor);
        test::click(*canvas, icon);
        QCOMPARE(session.currentPageIndex(), std::size_t(1)); // the page link was followed
        QVERIFY(session.selection().isEmpty() || session.currentPageIndex() == 1);
    }

    void theIconIsOnlyShownOverLinkedCells()
    {
        DocumentSession session(test::documentFromXml(kDataDiagram));
        auto canvas = test::makeCanvas(session);
        // b has no link: no icon, the pointer stays an arrow in its corner.
        test::move(*canvas, QPointF(300 + 100 - 12, 60 + 12), Qt::NoModifier, Qt::NoButton);
        QVERIFY(canvas->interaction().cursor() != Qt::PointingHandCursor);
    }
};

SHELL_TEST_MAIN(TstDataActions)
#include "tst_data_actions.moc"
