// Edit > Edit Shape: the stencil XML dialog and its single edit.

#include "support/test_support.h"

#include "qraw/format/compression.h"
#include "qraw/ui/edit_actions.h"
#include "qraw/ui/edit_shape_dialog.h"

#include <QPlainTextEdit>

using namespace qraw;
using namespace qraw::ui;

namespace {

const std::string kStencil =
    "<shape name=\"s\" w=\"100\" h=\"100\" aspect=\"variable\"><background><rect x=\"0\" y=\"0\" w=\"100\" h=\"100\"/>"
    "</background><foreground><fillstroke/></foreground></shape>";

core::Document documentWithStencilCell()
{
    const std::string xml =
        "<mxfile><diagram id=\"p1\" name=\"P\"><mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
        "<mxCell id=\"s\" value=\"\" style=\"shape=stencil(" + format::compressDiagram(kStencil) +
        ");fillColor=#ff0000;\" vertex=\"1\" parent=\"1\"><mxGeometry x=\"10\" y=\"10\" width=\"80\" height=\"60\" "
        "as=\"geometry\"/></mxCell><mxCell id=\"r\" value=\"\" style=\"rounded=1;\" vertex=\"1\" parent=\"1\">"
        "<mxGeometry x=\"200\" y=\"10\" width=\"80\" height=\"60\" as=\"geometry\"/></mxCell></root></mxGraphModel>"
        "</diagram></mxfile>";
    return test::documentFromXml(xml);
}

} // namespace

class TstEditShape : public QObject {
    Q_OBJECT

private slots:
    void stencilXmlOnlyForStencilVertices()
    {
        DocumentSession session(documentWithStencilCell());
        const core::Page& page = *session.currentPage();
        const auto xml = stencilXmlOf(*page.findCell("s"));
        QVERIFY(xml);
        QVERIFY(xml->find("<shape") != std::string::npos);
        QVERIFY(!stencilXmlOf(*page.findCell("r")));

        // A named, registered stencil.
        core::Cell named = *page.findCell("r");
        named.setStyleText("shape=mxgraph.flowchart.process");
        QVERIFY(stencilXmlOf(named));
    }

    void validStencilIsAppliedAsOneUndoableEdit()
    {
        DocumentSession session(documentWithStencilCell());
        const QByteArray saved = test::savedBytes(session);
        session.selection().set({"s"});

        EditShapeDialog dialog(*session.currentPage()->findCell("s"));
        QVERIFY(!dialog.editor()->toPlainText().isEmpty());
        QVERIFY(dialog.errorText().isEmpty());
        QVERIFY(!dialog.previewImage().isNull());

        const std::string edited =
            "<shape name=\"s\" w=\"100\" h=\"100\" aspect=\"variable\"><background><ellipse x=\"0\" y=\"0\" w=\"100\" "
            "h=\"100\"/></background><foreground><fillstroke/></foreground></shape>";
        dialog.editor()->setPlainText(QString::fromStdString(edited));
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        const auto value = dialog.shapeValue();
        QVERIFY(value);
        QVERIFY(value->starts_with("stencil("));

        QVERIFY(actions::setShape(session, *value));
        const std::string style = session.currentPage()->findCell("s")->styleText();
        QVERIFY(style.find("shape=" + *value) != std::string::npos);
        QVERIFY(style.find("fillColor=#ff0000") != std::string::npos);
        QCOMPARE(stencilXmlOf(*session.currentPage()->findCell("s")).value().find("<ellipse") != std::string::npos, true);

        QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), saved);
    }

    void invalidXmlKeepsTheDialogOpenAndChangesNothing()
    {
        DocumentSession session(documentWithStencilCell());
        session.selection().set({"s"});
        EditShapeDialog dialog(*session.currentPage()->findCell("s"));
        dialog.setResult(-1);
        dialog.editor()->setPlainText(QStringLiteral("<shape><background>"));
        dialog.accept();
        QVERIFY(dialog.result() != int(QDialog::Accepted));
        QVERIFY(!dialog.errorText().isEmpty());
        QVERIFY(!dialog.shapeValue());
        QVERIFY(dialog.previewImage().isNull());

        dialog.editor()->setPlainText(QStringLiteral("<foo/>"));
        dialog.updatePreview();
        QVERIFY(!dialog.errorText().isEmpty());

        QVERIFY(!session.canUndo());
    }
};

QTEST_MAIN(TstEditShape)
#include "tst_edit_shape.moc"
