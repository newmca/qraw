#include "qraw/format/codec.h"
#include "qraw/format/compression.h"
#include "qraw/format/semantic.h"

#include <QTest>

using namespace qraw;
using format::PageEncodingPolicy;

namespace {

const char* const kUnknownConstructs = R"(<?xml version="1.0" encoding="UTF-8"?>
<!-- leading comment -->
<mxfile host="Electron" futureFlag="yes" xmlns:ext="urn:example">
  <ext:settings theme="dark"><ext:item>kept</ext:item></ext:settings>
  <diagram id="p1" name="Page-1" ext:tag="t">
    <mxGraphModel dx="800" grid="1" gridSize="10" unknownModelAttr="z">
      <root>
        <mxCell id="0" />
        <mxCell id="1" parent="0" />
        <futureEntry kind="between-cells" />
        <UserObject label="&lt;b&gt;Server&lt;/b&gt;" owner="ops" ext:rack="r1" id="2">
          <mxCell style="shape=cylinder3;html=1;" vertex="1" parent="1" futureCellAttr="q">
            <mxGeometry x="10.50" y="20" width="80" height="60" as="geometry">
              <mxRectangle width="80" height="20" as="alternateBounds" />
            </mxGeometry>
          </mxCell>
        </UserObject>
        <mxCell id="3" value="" style="endArrow=classic;" edge="1" parent="1" source="2" target="2">
          <metadata note="before-geometry" />
          <mxGeometry relative="1" as="geometry">
            <Array as="points"><mxPoint x="5" y="6" /></Array>
          </mxGeometry>
        </mxCell>
      </root>
      <extension><![CDATA[raw <data>]]></extension>
    </mxGraphModel>
  </diagram>
</mxfile>
<!-- trailing comment -->
)";

format::ReadResult read(const QByteArray& bytes)
{
    auto result = format::readDocument(bytes);
    if (!result.ok() && !result.diagnostics.empty())
        qWarning("read failed: %s", result.diagnostics.front().message.c_str());
    return result;
}

QByteArray compressedFile()
{
    const std::string model = R"(<mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/>)"
                              R"(<mxCell id="2" value="A" vertex="1" parent="1"><mxGeometry width="10" height="10" )"
                              R"(as="geometry"/></mxCell></root></mxGraphModel>)";
    return QByteArray(R"(<mxfile compressed="true"><diagram id="c" name="C">)")
        + QByteArray::fromStdString(format::compressDiagram(model)) + "</diagram></mxfile>";
}

} // namespace

class TestCodec : public QObject {
    Q_OBJECT

private slots:
    void preservesUnknownConstructs()
    {
        const QByteArray source(kUnknownConstructs);
        auto result = read(source);
        QVERIFY(result.ok());
        QVERIFY(!result.hasErrors());

        const auto& page = *result.document->page(0);
        QCOMPARE(page.cellCount(), std::size_t(4));
        QCOMPARE(page.rootEntries().size(), std::size_t(5)); // + <futureEntry>
        const auto* server = page.findCell("2");
        QVERIFY(server && server->hasWrapper());
        QCOMPARE(server->value(), std::string("<b>Server</b>"));
        QCOMPARE(server->wrapper()->tag, std::string("UserObject"));
        QCOMPARE(server->geometry()->x(), 10.5);
        QCOMPARE(page.findCell("3")->geometry()->points().size(), std::size_t(1));

        const QByteArray saved = format::writeDocument(*result.document);
        std::string difference;
        QVERIFY2(format::semanticEqual(source, saved, &difference), difference.c_str());
        QVERIFY(saved.startsWith("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!-- leading comment -->"));
        QVERIFY(saved.contains("x=\"10.50\""));          // untouched numbers keep their text
        QVERIFY(saved.indexOf("<metadata") < saved.indexOf("<mxGeometry relative")); // child order kept
        QVERIFY(saved.contains("<![CDATA[raw <data>]]>"));

        // Byte stability.
        auto again = read(saved);
        QVERIFY(again.ok());
        QCOMPARE(format::writeDocument(*again.document), saved);
    }

    void reproducesPageEncoding()
    {
        auto result = read(compressedFile());
        QVERIFY(result.ok());
        QCOMPARE(result.document->page(0)->encoding(), core::PageEncoding::Compressed);
        QCOMPARE(result.document->page(0)->cellCount(), std::size_t(3));

        const QByteArray preserved = format::writeDocument(*result.document);
        QVERIFY(!preserved.contains("<mxGraphModel"));
        QVERIFY(format::semanticEqual(compressedFile(), preserved));

        const QByteArray inlined = format::writeDocument(*result.document, {PageEncodingPolicy::Uncompressed});
        QVERIFY(inlined.contains("<mxGraphModel>"));
        QVERIFY(inlined.contains("compressed=\"false\""));
        QVERIFY(format::semanticEqual(compressedFile(), QByteArray(inlined).replace("compressed=\"false\"", "compressed=\"true\"")));

        auto inlineDoc = read(QByteArray(kUnknownConstructs));
        const QByteArray compressed = format::writeDocument(*inlineDoc.document, {PageEncodingPolicy::Compressed});
        QVERIFY(!compressed.contains("<mxCell"));
        QVERIFY(format::semanticEqual(QByteArray(kUnknownConstructs), compressed));
    }

    void keepsBareModelFiles()
    {
        const QByteArray source = R"(<mxGraphModel grid="0"><root><mxCell id="0"/></root></mxGraphModel>)";
        auto result = read(source);
        QVERIFY(result.ok());
        QCOMPARE(result.document->kind(), core::DocumentKind::BareModel);
        QVERIFY(!result.document->page(0)->gridEnabled());
        const QByteArray saved = format::writeDocument(*result.document);
        QVERIFY(saved.startsWith("<mxGraphModel grid=\"0\">"));
        QVERIFY(format::semanticEqual(source, saved));
    }

    void htmlLabelsAndEntitiesAreStable()
    {
        const QByteArray source = R"(<mxGraphModel><root><mxCell id="0"/><mxCell id="1" )"
                                  R"(value="&lt;div&gt;a &amp;amp; b&lt;/div&gt;&#xa;line2&#9;&quot;q&quot; &#10;" )"
                                  R"(parent="0"/></root></mxGraphModel>)";
        auto result = read(source);
        QVERIFY(result.ok());
        QCOMPARE(result.document->page(0)->findCell("1")->value(),
                 std::string("<div>a &amp; b</div>\nline2\t\"q\" \n"));
        const QByteArray saved = format::writeDocument(*result.document);
        QVERIFY(saved.contains(R"(value="&lt;div&gt;a &amp;amp; b&lt;/div&gt;&#xa;line2&#x9;&quot;q&quot; &#xa;")"));
        auto again = read(saved);
        QCOMPARE(again.document->page(0)->findCell("1")->value(), result.document->page(0)->findCell("1")->value());
    }

    void newCellsUseCanonicalAttributeOrder()
    {
        auto result = read(R"(<mxfile><diagram id="d" name="D"><mxGraphModel><root><mxCell id="0"/>)"
                           R"(<mxCell id="1" parent="0"/></root></mxGraphModel></diagram></mxfile>)");
        QVERIFY(result.ok());
        auto& page = *result.document->page(0);
        QVERIFY(page.addCell(core::Cell::makeVertex("v", "1", "Hi", "rounded=1;", {10, 20, 30, 40})));
        QVERIFY(page.addCell(core::Cell::makeEdge("e", "1", "", "", "v", "v")));
        const QByteArray saved = format::writeDocument(*result.document);
        QVERIFY(saved.contains(R"(<mxCell id="v" value="Hi" style="rounded=1;" vertex="1" parent="1">)"));
        QVERIFY(saved.contains(R"(<mxGeometry x="10" y="20" width="30" height="40" as="geometry" />)"));
        QVERIFY(saved.contains(R"(<mxCell id="e" value="" edge="1" parent="1" source="v" target="v">)"));
    }

    void reportsMalformedXmlWithPosition()
    {
        auto result = format::readDocument("<mxfile>\n  <diagram>\n    <mxGraphModel>\n  </diagram>\n</mxfile>");
        QVERIFY(!result.ok());
        QCOMPARE(result.diagnostics.size(), std::size_t(1));
        QCOMPARE(result.diagnostics[0].severity, core::Diagnostic::Severity::Error);
        QCOMPARE(result.diagnostics[0].line, 4);
        QVERIFY(result.diagnostics[0].column > 0);

        QVERIFY(!format::readDocument("<svg/>").ok());
    }

    void keepsUndecodablePayloadVerbatim()
    {
        const QByteArray source = R"(<mxfile><diagram id="x" name="X">not-a-valid-payload</diagram></mxfile>)";
        auto result = read(source);
        QVERIFY(result.ok());
        QCOMPARE(result.document->page(0)->encoding(), core::PageEncoding::Opaque);
        QVERIFY(!result.diagnostics.empty());
        QVERIFY(format::writeDocument(*result.document).contains(">not-a-valid-payload</diagram>"));
    }

    void semanticEqualDetectsDifferences()
    {
        std::string difference;
        QVERIFY(format::semanticEqual(QByteArray("<a x=\"1\" y=\"2\">\n  <b/>\n</a>"), QByteArray(R"(<a y="2" x="1"><b/></a>)")));
        QVERIFY(!format::semanticEqual(QByteArray(R"(<a><b/><c/></a>)"), QByteArray(R"(<a><c/><b/></a>)"), &difference));
        QVERIFY(!difference.empty());
        QVERIFY(!format::semanticEqual(QByteArray(R"(<a v="1"/>)"), QByteArray(R"(<a v="2"/>)")));
    }

    // --- regressions from the format review ---------------------------------

    void rejectsExcessiveNesting()
    {
        // Used to overflow the stack in the recursive XmlNode copy.
        const int depth = 20000;
        QByteArray source = R"(<mxfile><diagram id="d"><mxGraphModel><root><mxCell id="0"/>)";
        source += QByteArray("<a>").repeated(depth) + QByteArray("</a>").repeated(depth);
        source += "</root></mxGraphModel></diagram></mxfile>";
        const auto result = format::readDocument(source);
        QVERIFY(!result.ok());
        QVERIFY(result.diagnostics.back().message.find("nested deeper") != std::string::npos);
    }

    void keepsUnexpandedEntityReferences()
    {
        const QByteArray source = "<?xml version=\"1.0\"?>\n<!DOCTYPE mxfile SYSTEM \"ext.dtd\" [<!ENTITY me \"MyEnt\">]>\n"
                                  "<mxfile><diagram id=\"d\"><mxGraphModel><root><mxCell id=\"0\"/>"
                                  "<foo>a &me; b &undeclared; c</foo></root></mxGraphModel></diagram></mxfile>";
        const auto result = read(source);
        QVERIFY(result.ok());
        const QByteArray saved = format::writeDocument(*result.document);
        QVERIFY(saved.contains("<foo>a MyEnt b &undeclared; c</foo>"));
        QCOMPARE(format::writeDocument(*read(saved).document), saved);
    }

    void warnsAboutUnexpandedEntityInAttribute()
    {
        const QByteArray source = "<!DOCTYPE mxfile SYSTEM \"ext.dtd\">\n<mxfile><diagram id=\"d\"><mxGraphModel><root>"
                                  "<mxCell id=\"0\" value=\"a &ext; b\"/></root></mxGraphModel></diagram></mxfile>";
        const auto result = read(source);
        QVERIFY(result.ok());
        QVERIFY(std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const core::Diagnostic& d) {
            return d.message.find("entity reference") != std::string::npos;
        }));
    }

    void doctypeIsByteStableAcrossSaves()
    {
        const QByteArray source = "<?xml version=\"1.0\"?>\n<!DOCTYPE mxfile>\n<mxfile><diagram id=\"d\"/></mxfile>";
        const QByteArray first = format::writeDocument(*read(source).document);
        QCOMPARE(format::writeDocument(*read(first).document), first);
        QVERIFY(first.contains("?>\n<!DOCTYPE mxfile>\n<mxfile"));
    }

    void compressedPageWithCommentReloadsWithItsCells()
    {
        const QByteArray source = R"(<mxfile><diagram id="d"><!--note--><mxGraphModel><root><mxCell id="0"/>)"
                                  R"(<mxCell id="1" parent="0"/></root></mxGraphModel></diagram></mxfile>)";
        const auto original = read(source);
        const QByteArray compressed = format::writeDocument(*original.document, {PageEncodingPolicy::Compressed});
        QVERIFY(!compressed.contains("<mxCell"));
        const auto reloaded = read(compressed);
        QVERIFY(reloaded.ok());
        QCOMPARE(reloaded.document->page(0)->encoding(), core::PageEncoding::Compressed);
        QCOMPARE(reloaded.document->page(0)->cellCount(), std::size_t(2));
        QVERIFY(compressed.contains("<!--note-->"));
        QCOMPARE(format::writeDocument(*reloaded.document), compressed);
    }

    void doesNotCompressPagesThatWouldReadBackDifferently()
    {
        // A second <mxGraphModel> next to the payload would win on reload.
        const QByteArray source = R"(<mxfile><diagram id="d"><mxGraphModel><root><mxCell id="0"/></root></mxGraphModel>)"
                                  R"(<mxGraphModel><root><mxCell id="9"/></root></mxGraphModel></diagram></mxfile>)";
        const auto original = read(source);
        const QByteArray saved = format::writeDocument(*original.document, {PageEncodingPolicy::Compressed});
        const auto reloaded = read(saved);
        QCOMPARE(reloaded.document->page(0)->encoding(), core::PageEncoding::Inline);
        QVERIFY(reloaded.document->page(0)->findCell("0"));
    }

    void keepsPrologAndEpilogInsideCompressedPayload()
    {
        const std::string payload = R"(<?xml version="1.0"?><!-- keep me --><mxGraphModel><root><mxCell id="0"/>)"
                                    R"(</root></mxGraphModel><!-- tail -->)";
        const QByteArray source = QByteArray(R"(<mxfile><diagram id="d">)")
            + QByteArray::fromStdString(format::compressDiagram(payload)) + "</diagram></mxfile>";
        const auto doc = read(source);
        QVERIFY(doc.ok());
        const auto& frame = doc.document->page(0)->payloadFrame();
        QVERIFY(frame.declaration.present);
        QCOMPARE(frame.prolog.size(), std::size_t(1));
        QCOMPARE(frame.epilog.size(), std::size_t(1));

        const QByteArray saved = format::writeDocument(*doc.document);
        QVERIFY(format::semanticEqual(source, saved));
        QVERIFY(!format::semanticEqual(source, R"(<mxfile><diagram id="d"><mxGraphModel><root><mxCell id="0"/></root>)"
                                               R"(</mxGraphModel></diagram></mxfile>)"));

        // Written inline, the comments stay around the model.
        const QByteArray inlined = format::writeDocument(*doc.document, {PageEncodingPolicy::Uncompressed});
        QVERIFY(inlined.contains("<!-- keep me -->"));
        QVERIFY(inlined.contains("<!-- tail -->"));
    }

    void keepsSignificantWhitespaceInMixedContent()
    {
        const QByteArray source = R"(<mxfile><diagram id="d"><mxGraphModel><root><mxCell id="0"/>)"
                                  R"(<html><b>Hello</b> <i>World</i></html>)"
                                  R"(<t xml:space="preserve">  <x/>  </t>)"
                                  "<ind>\n  <x/>\n</ind>"
                                  R"(</root></mxGraphModel></diagram></mxfile>)";
        const QByteArray saved = format::writeDocument(*read(source).document);
        QVERIFY(saved.contains("<html><b>Hello</b> <i>World</i></html>"));
        QVERIFY(saved.contains("<t xml:space=\"preserve\">  <x />  </t>"));
        QVERIFY(saved.contains("<ind>\n"));          // plain indentation is re-generated, not kept as text
        QCOMPARE(format::writeDocument(*read(saved).document), saved);
    }

    void semanticEqualSeesPrologEpilogAndMixedWhitespace()
    {
        QVERIFY(!format::semanticEqual(QByteArray("<!-- lost --><?pi x?><mxfile/>"), QByteArray("<mxfile/>")));
        QVERIFY(!format::semanticEqual(QByteArray("<mxfile/><!-- lost -->"), QByteArray("<mxfile/>")));
        QVERIFY(!format::semanticEqual(QByteArray("<!DOCTYPE mxfile><mxfile/>"), QByteArray("<mxfile/>")));
        QVERIFY(!format::semanticEqual(QByteArray("<?xml version=\"1.0\"?><mxfile/>"), QByteArray("<mxfile/>")));
        QVERIFY(!format::semanticEqual(QByteArray("<x><b>a</b> <i>b</i>c</x>"), QByteArray("<x><b>a</b><i>b</i>c</x>")));
    }
};

QTEST_GUILESS_MAIN(TestCodec)
#include "tst_codec.moc"
