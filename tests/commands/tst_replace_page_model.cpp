// ops::replacePageModel (Extras > Edit Diagram): the page's model text goes
// through format::pageModelText / parsePageModelText and back as one edit.
// Unchanged text gives the identical file, a change is applied, undo restores
// the bytes, malformed text is an error that changes nothing.
#include "corpus_harness.h"

#include "qraw/commands/editor.h"
#include "qraw/commands/ops_pages.h"
#include "qraw/core/cell.h"
#include "qraw/format/codec.h"

#include <QTest>

#include <string>

using namespace qraw;
using namespace qraw::commands;

namespace {

constexpr char kDiagram[] =
    R"(<mxfile host="x"><diagram id="p1" name="One"><mxGraphModel dx="800" grid="1" gridSize="10" page="1" pageWidth="827" pageHeight="1169"><root><mxCell id="0"/><mxCell id="1" parent="0"/><mxCell id="a" value="A &amp; B&#xa;two" style="rounded=0;" vertex="1" parent="1"><mxGeometry x="40" y="40" width="120" height="60" as="geometry"/></mxCell><UserObject label="Obj" link="https://example.com" id="o"><mxCell style="" vertex="1" parent="1"><mxGeometry x="200" y="40" width="80" height="40" as="geometry"/></mxCell></UserObject><!-- note --></root></mxGraphModel></diagram><diagram id="p2" name="Two"><mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/></root></mxGraphModel></diagram></mxfile>)";

core::Document load(const char* xml)
{
    auto read = format::readDocument(QByteArrayView(xml));
    Q_ASSERT(read.ok());
    return std::move(*read.document);
}

// The text the dialog shows for page 0, parsed again.
format::ModelTextResult reparse(const std::string& text)
{
    return format::parsePageModelText(QByteArrayView(text.data(), static_cast<qsizetype>(text.size())));
}

} // namespace

class TestReplacePageModel : public QObject {
    Q_OBJECT

private slots:
    void unchangedTextGivesIdenticalBytes()
    {
        core::Document doc = load(kDiagram);
        const QByteArray before = format::writeDocument(doc);
        format::ModelTextResult parsed = reparse(format::pageModelText(*doc.page(0)));
        QVERIFY(parsed.page);
        Transaction tx(doc);
        QVERIFY(ops::replacePageModel(tx, 0, std::move(*parsed.page)).ok);
        QCOMPARE(format::writeDocument(doc), before);
        tx.rollback();
        QCOMPARE(format::writeDocument(doc), before);
    }

    void unchangedTextRoundTripsEveryCorpusPage()
    {
        int pages = 0;
        for (const QString& path : testing::corpusFiles()) {
            auto doc = testing::loadFile(path);
            // A DTD-declared entity lives in the document's DOCTYPE, which the page text does
            // not have: such text is only parsable when it is unchanged (the dialog skips it).
            if (!doc || !doc->prolog().empty())
                continue;
            const QByteArray before = format::writeDocument(*doc);
            for (std::size_t i = 0; i < doc->pageCount(); ++i) {
                const core::Page* page = doc->page(i);
                if (page->encoding() == core::PageEncoding::Opaque || page->cellCount() == 0)
                    continue;
                format::ModelTextResult parsed = reparse(format::pageModelText(*page));
                QVERIFY2(parsed.page, qPrintable(path));
                Transaction tx(*doc);
                QVERIFY2(ops::replacePageModel(tx, i, std::move(*parsed.page)).ok, qPrintable(path));
                ++pages;
            }
            QCOMPARE(format::writeDocument(*doc), before);
        }
        QVERIFY(pages > 20);
    }

    void changedTextIsAppliedAsOneUndoStep()
    {
        Editor editor(load(kDiagram));
        const QByteArray before = format::writeDocument(editor.document());
        std::string text = format::pageModelText(*editor.document().page(0));
        const std::size_t at = text.find("A &amp; B");
        QVERIFY(at != std::string::npos);
        text.replace(at, 9, "Changed");
        format::ModelTextResult parsed = reparse(text);
        QVERIFY(parsed.page);

        QVERIFY(editor.edit("Edit Diagram", [&](Transaction& tx) {
                          return ops::replacePageModel(tx, 0, std::move(*parsed.page));
                      }).ok);
        QCOMPARE(editor.document().page(0)->findCell("a")->value(), std::string("Changed\ntwo"));
        QCOMPARE(editor.document().page(0)->id(), std::string("p1")); // <diagram> attributes stay
        QCOMPARE(editor.document().page(0)->name(), std::string("One"));
        QCOMPARE(editor.document().pageCount(), std::size_t{2});
        const QByteArray after = format::writeDocument(editor.document());
        QVERIFY(after != before);

        QVERIFY(editor.undo());
        QCOMPARE(format::writeDocument(editor.document()), before);
        QVERIFY(!editor.undo()); // one step
        QVERIFY(editor.redo());
        QCOMPARE(format::writeDocument(editor.document()), after);
    }

    void addedCellsAndCommentsInTheTextAreKept()
    {
        core::Document doc = load(kDiagram);
        std::string text = format::pageModelText(*doc.page(0));
        const std::size_t at = text.find("</root>");
        QVERIFY(at != std::string::npos);
        text.insert(at, R"(<mxCell id="n" value="New" vertex="1" parent="1"><mxGeometry width="10" height="10" as="geometry"/></mxCell>)");
        format::ModelTextResult parsed = reparse(text);
        QVERIFY(parsed.page);
        Transaction tx(doc);
        QVERIFY(ops::replacePageModel(tx, 0, std::move(*parsed.page)).ok);
        QVERIFY(doc.page(0)->findCell("n") != nullptr);
        QVERIFY(doc.page(0)->findCell("o") != nullptr); // wrapped cell kept
    }

    void malformedTextIsAnErrorWithPosition()
    {
        core::Document doc = load(kDiagram);
        const QByteArray before = format::writeDocument(doc);
        std::string text = format::pageModelText(*doc.page(0));
        text.replace(text.find("</root>"), 7, "</rot>");
        const format::ModelTextResult parsed = reparse(text);
        QVERIFY(!parsed.page);
        QCOMPARE(parsed.error.severity, core::Diagnostic::Severity::Error);
        QVERIFY(parsed.error.line > 0);
        QVERIFY(parsed.error.column > 0);
        QCOMPARE(format::writeDocument(doc), before); // nothing was applied
    }

    void notAModelIsAnError()
    {
        const format::ModelTextResult svg = reparse("<svg/>");
        QVERIFY(!svg.page);
        QVERIFY(!svg.error.message.empty());
        QVERIFY(!reparse("").page);
        QVERIFY(!reparse("not xml").page);
    }

    void anMxfileTakesItsFirstPage()
    {
        const format::ModelTextResult parsed = reparse(kDiagram);
        QVERIFY(parsed.page);
        QVERIFY(parsed.page->findCell("a") != nullptr);
    }

    void aModelWithoutCellsIsRefused()
    {
        core::Document doc = load(kDiagram);
        const QByteArray before = format::writeDocument(doc);
        format::ModelTextResult parsed = reparse("<mxGraphModel><root/></mxGraphModel>");
        QVERIFY(parsed.page);
        Transaction tx(doc);
        QVERIFY(!ops::replacePageModel(tx, 0, std::move(*parsed.page)).ok);
        tx.rollback();
        QCOMPARE(format::writeDocument(doc), before);
    }

    void aMissingPageIsRefused()
    {
        core::Document doc = load(kDiagram);
        format::ModelTextResult parsed = reparse(format::pageModelText(*doc.page(0)));
        Transaction tx(doc);
        QVERIFY(!ops::replacePageModel(tx, 5, std::move(*parsed.page)).ok);
    }

    void aCompressedPageStaysCompressed()
    {
        core::Document doc = load(kDiagram);
        QVERIFY(doc.page(0)->encoding() == core::PageEncoding::Inline);
        const QByteArray compressed = format::writeDocument(doc, {format::PageEncodingPolicy::Compressed});
        core::Document packed = load(compressed.constData());
        QVERIFY(packed.page(0)->encoding() == core::PageEncoding::Compressed);
        const QByteArray before = format::writeDocument(packed);
        format::ModelTextResult parsed = reparse(format::pageModelText(*packed.page(0)));
        QVERIFY(parsed.page);
        Transaction tx(packed);
        QVERIFY(ops::replacePageModel(tx, 0, std::move(*parsed.page)).ok);
        QCOMPARE(format::writeDocument(packed), before);
        QVERIFY(packed.page(0)->encoding() == core::PageEncoding::Compressed);
    }
};

QTEST_APPLESS_MAIN(TestReplacePageModel)
#include "tst_replace_page_model.moc"
