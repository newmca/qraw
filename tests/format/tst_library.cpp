#include "qraw/format/codec.h"
#include "qraw/format/compression.h"
#include "qraw/format/library.h"

#include <QTest>

using namespace qraw;
using namespace qraw::format;

namespace {

const std::string kModel =
    "<mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/><mxCell id=\"2\" value=\"A &amp; B\" "
    "style=\"rounded=1;\" vertex=\"1\" parent=\"1\"><mxGeometry width=\"80\" height=\"40\" as=\"geometry\"/></mxCell>"
    "</root></mxGraphModel>";

std::string libraryWith(const std::string& json)
{
    return "<mxlibrary>" + json + "</mxlibrary>";
}

} // namespace

class TestLibrary : public QObject {
    Q_OBJECT

private slots:
    void parsesPlainCompressedAndImageEntries()
    {
        const std::string compressed = compressDiagram(kModel);
        const std::string json = "[{\"xml\":\"" + std::string("&lt;mxGraphModel/&gt;") + "\",\"w\":10,\"h\":20,\"title\":\"Plain\"},"
            "{\"xml\":\"" + compressed + "\",\"w\":80,\"h\":40,\"title\":\"Packed\",\"aspect\":\"fixed\"},"
            "{\"data\":\"data:image/png;base64,AAAA\",\"w\":16,\"h\":16,\"title\":\"Img\",\"aspect\":\"fixed\"}]";
        std::vector<core::Diagnostic> diagnostics;
        const auto library = parseLibrary(libraryWith(json), &diagnostics);
        QVERIFY(library);
        QVERIFY(diagnostics.empty());
        QCOMPARE(library->entries.size(), std::size_t(3));
        QCOMPARE(library->entries[0].xml, std::string("<mxGraphModel/>"));
        QCOMPARE(library->entries[1].xml, kModel);
        QCOMPARE(library->entries[1].aspect, std::string("fixed"));
        QCOMPARE(library->entries[2].data, std::string("data:image/png;base64,AAAA"));
        QCOMPARE(library->entries[2].w, 16.0);
        QCOMPARE(convertDataUri(library->entries[2].data), std::string("data:image/png,AAAA"));
    }

    void serializeParseRoundTrip()
    {
        Library library;
        library.entries.push_back({"Rounded <b>", kModel, {}, 80, 40, ""});
        library.entries.push_back({"Img \"q\"", {}, "data:image/png;base64,AAAA", 16.5, 16, "fixed"});
        const std::string bytes = serializeLibrary(library);
        QVERIFY(isLibraryXml(bytes));
        const auto again = parseLibrary(bytes);
        QVERIFY(again);
        QCOMPARE(again->entries.size(), std::size_t(2));
        QCOMPARE(again->entries[0].title, std::string("Rounded <b>"));
        QCOMPARE(again->entries[0].xml, kModel);
        QCOMPARE(again->entries[1].h, 16.0);
        QCOMPARE(again->entries[1].w, 16.5);
        QCOMPARE(again->entries[1].title, std::string("Img \"q\""));
        QCOMPARE(serializeLibrary(*again), bytes); // deterministic
        QCOMPARE(serializeLibrary({}), emptyLibraryXml() + "\n");
    }

    void malformedInputIsReported()
    {
        std::vector<core::Diagnostic> diagnostics;
        QVERIFY(!parseLibrary(libraryWith("[{\"xml\":"), &diagnostics));
        QVERIFY(!diagnostics.empty());
        QCOMPARE(diagnostics.front().severity, core::Diagnostic::Severity::Error);

        diagnostics.clear();
        QVERIFY(!parseLibrary(libraryWith("{}"), &diagnostics));
        QVERIFY(!diagnostics.empty());

        diagnostics.clear();
        QVERIFY(!parseLibrary("<mxfile/>", &diagnostics));
        QVERIFY(!diagnostics.empty());

        // A bad entry is skipped with a warning; the others load.
        diagnostics.clear();
        const auto library = parseLibrary(libraryWith("[{\"w\":1},{\"xml\":\"&lt;a/&gt;\",\"w\":1,\"h\":1}]"), &diagnostics);
        QVERIFY(library);
        QCOMPARE(library->entries.size(), std::size_t(1));
        QCOMPARE(diagnostics.size(), std::size_t(1));
    }

    void documentReaderRoutesLibraries()
    {
        const std::string bytes = libraryWith("[]");
        const ReadResult result = readDocument(QByteArrayView(bytes.data(), static_cast<qsizetype>(bytes.size())));
        QVERIFY(!result.ok());
        QVERIFY(!result.diagnostics.empty());
        QVERIFY(isLibraryDiagnostic(result.diagnostics.front()));
    }
};

QTEST_APPLESS_MAIN(TestLibrary)
#include "tst_library.moc"
