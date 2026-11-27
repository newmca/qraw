// Diagrams embedded in PNG, SVG and PDF files: sniffing, extraction, writing
// and the readAny entry points. Uses QtCore and zlib only.

#include "qraw/format/compression.h"
#include "qraw/format/embed.h"

#include <QByteArray>
#include <QString>
#include <QTest>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace qraw;
using format::ContainerKind;

namespace {

const char* const kPngBase64 =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==";

const std::string kDiagram = "<mxfile><diagram id=\"a\" name=\"P %&+ \xC3\xA9 \xE5\xAD\x97\">x</diagram></mxfile>";

const std::string kModel =
    "<mxfile><diagram id=\"a\" name=\"Page\"><mxGraphModel><root><mxCell id=\"0\"/>"
    "<mxCell id=\"1\" parent=\"0\"/></root></mxGraphModel></diagram></mxfile>";

QByteArray bytesOf(std::string_view text) { return QByteArray(text.data(), static_cast<qsizetype>(text.size())); }

std::string stringOf(const QByteArray& bytes) { return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())); }

QByteArray basePng() { return QByteArray::fromBase64(kPngBase64); }

std::uint32_t crc32Of(std::string_view data)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const char byte : data) {
        crc ^= static_cast<unsigned char>(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return crc ^ 0xFFFFFFFFu;
}

void appendU32(std::string& out, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out += static_cast<char>((value >> shift) & 0xFF);
}

std::string makeChunk(std::string_view type, std::string_view data)
{
    std::string out;
    appendU32(out, static_cast<std::uint32_t>(data.size()));
    out += type;
    out += data;
    appendU32(out, crc32Of(std::string(type) + std::string(data)));
    return out;
}

// Inserts `chunk` right before the first IDAT chunk of a PNG.
std::string withChunkBeforeIdat(const QByteArray& png, const std::string& chunk)
{
    const std::string bytes = stringOf(png);
    const std::size_t idat = bytes.find("IDAT") - 4;
    return bytes.substr(0, idat) + chunk + bytes.substr(idat);
}

struct Chunk {
    std::string type;
    std::string data;
    std::uint32_t storedCrc = 0;
};

// Splits a PNG into chunks (no bounds checks needed for test output).
std::vector<Chunk> chunksOf(const QByteArray& png)
{
    const std::string bytes = stringOf(png);
    std::vector<Chunk> chunks;
    std::size_t pos = 8;
    while (pos + 12 <= bytes.size()) {
        const auto at = [&](std::size_t i) { return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[pos + i])); };
        const std::uint32_t length = (at(0) << 24) | (at(1) << 16) | (at(2) << 8) | at(3);
        Chunk chunk;
        chunk.type = bytes.substr(pos + 4, 4);
        chunk.data = bytes.substr(pos + 8, length);
        chunk.storedCrc = (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[pos + 8 + length])) << 24) |
                          (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[pos + 9 + length])) << 16) |
                          (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[pos + 10 + length])) << 8) |
                          static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[pos + 11 + length]));
        chunks.push_back(chunk);
        pos += 12 + length;
    }
    return chunks;
}

std::string padTen(std::size_t value)
{
    std::string digits = std::to_string(value);
    return std::string(10 - digits.size(), '0') + digits;
}

// A small PDF with a correct classic xref table; object 4 is the Info dictionary.
std::string minimalPdf()
{
    const std::vector<std::string> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] >>",
        "<< /Title (Old title) /Producer (test) >>",
    };
    std::string pdf = "%PDF-1.4\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const std::size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (const std::size_t offset : offsets)
        pdf += padTen(offset) + " 00000 n \n";
    pdf += "trailer\n<< /Size 5 /Root 1 0 R /Info 4 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return pdf;
}

} // namespace

class EmbedTest : public QObject
{
    Q_OBJECT

private slots:
    void sniffEachContainer()
    {
        QVERIFY(format::sniffContainer(basePng()) == ContainerKind::Png);
        QVERIFY(format::sniffContainer(bytesOf("%PDF-1.7\n")) == ContainerKind::Pdf);
        QVERIFY(format::sniffContainer(bytesOf("<mxfile/>")) == ContainerKind::Xml);
        QVERIFY(format::sniffContainer(bytesOf("<?xml version=\"1.0\"?>\n<!-- c -->\n<svg/>")) == ContainerKind::Svg);
        QVERIFY(format::sniffContainer(bytesOf("hello")) == ContainerKind::Unknown);
        QVERIFY(format::sniffContainer(QByteArray()) == ContainerKind::Unknown);
    }

    void sniffSvgWithDeclarationCommentAndDoctype()
    {
        const std::string svg = "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                "<!-- generated -->\n"
                                "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" [ <!ENTITY e \"x>y\"> ]>\n"
                                "<svg:svg xmlns:svg=\"http://www.w3.org/2000/svg\"/>";
        QVERIFY(format::sniffContainer(bytesOf(svg)) == ContainerKind::Svg);
        QVERIFY(format::sniffContainer(bytesOf("<!DOCTYPE mxfile [ <!ENTITY a \"b\"> ]><mxfile/>")) == ContainerKind::Xml);
    }

    void pngRoundTripKeepsChunkOrderAndCrc()
    {
        const auto embedded = format::embedInPng(basePng(), kDiagram);
        QVERIFY(embedded.has_value());

        const std::vector<Chunk> chunks = chunksOf(*embedded);
        std::size_t diagramIndex = chunks.size();
        for (std::size_t i = 0; i < chunks.size(); ++i) {
            if (chunks[i].type == "tEXt" && chunks[i].data.rfind("mxfile", 0) == 0)
                diagramIndex = i;
        }
        QVERIFY(diagramIndex < chunks.size());
        QVERIFY(chunks[diagramIndex + 1].type == "IDAT");
        QCOMPARE(crc32Of(chunks[diagramIndex].type + chunks[diagramIndex].data), chunks[diagramIndex].storedCrc);

        const auto extracted = format::extractFromPng(*embedded);
        QVERIFY(extracted.has_value());
        QCOMPARE(QString::fromStdString(*extracted), QString::fromStdString(kDiagram));
    }

    void embedTwiceLeavesOneDiagramChunk()
    {
        const auto once = format::embedInPng(basePng(), kDiagram);
        QVERIFY(once.has_value());
        const auto twice = format::embedInPng(*once, kModel);
        QVERIFY(twice.has_value());

        int count = 0;
        for (const Chunk& chunk : chunksOf(*twice))
            count += (chunk.type == "tEXt" && chunk.data.rfind("mxfile", 0) == 0) ? 1 : 0;
        QCOMPARE(count, 1);
        QCOMPARE(QString::fromStdString(*format::extractFromPng(*twice)), QString::fromStdString(kModel));
    }

    void embedRejectsNonPng()
    {
        QVERIFY(!format::embedInPng(bytesOf("not a png"), kDiagram).has_value());
    }

    void zTxtDiagramIsInflatedAndDecoded()
    {
        const std::string text = "<mxfile a=\"b\">x+y</mxfile>";
        const std::string payload = std::string("mxGraphModel") + '\0' + '\0' + format::deflateBytes(text, 15);
        const std::string png = withChunkBeforeIdat(basePng(), makeChunk("zTXt", payload));

        const auto extracted = format::extractFromPng(bytesOf(png));
        QVERIFY(extracted.has_value());
        QCOMPARE(QString::fromStdString(*extracted), QStringLiteral("<mxfile a=\"b\">x y</mxfile>"));
    }

    void svgPlainContent()
    {
        const auto extracted = format::extractFromSvg(
            bytesOf("<svg xmlns=\"http://www.w3.org/2000/svg\" content=\"&lt;mxfile&gt;&lt;/mxfile&gt;\"/>"));
        QVERIFY(extracted.has_value());
        QCOMPARE(QString::fromStdString(*extracted), QStringLiteral("<mxfile></mxfile>"));
    }

    void svgUriEncodedContent()
    {
        const std::string svg = "<svg content=\"" + format::encodeUriComponent(kDiagram) + "\"/>";
        const auto extracted = format::extractFromSvg(bytesOf(svg));
        QVERIFY(extracted.has_value());
        QCOMPARE(QString::fromStdString(*extracted), QString::fromStdString(kDiagram));
    }

    void svgLegacyBase64Content()
    {
        // escape("<mxfile>é</mxfile>") as draw.io wrote it, then base64.
        const QByteArray escaped = QByteArrayLiteral("%3Cmxfile%3E%u00E9%3C/mxfile%3E");
        const std::string svg = "<svg content=\"" + std::string(escaped.toBase64().constData()) + "\"/>";
        const auto extracted = format::extractFromSvg(bytesOf(svg));
        QVERIFY(extracted.has_value());
        QCOMPARE(QString::fromUtf8(extracted->data(), static_cast<qsizetype>(extracted->size())),
                 QStringLiteral("<mxfile>é</mxfile>"));
    }

    void svgWithoutContentHasNoDiagram()
    {
        QVERIFY(!format::extractFromSvg(bytesOf("<svg/>")).has_value());
    }

    void pdfEmbedExtractAndKeepInfo()
    {
        const QByteArray pdf = bytesOf(minimalPdf());
        const auto embedded = format::embedInPdf(pdf, kDiagram);
        QVERIFY(embedded.has_value());

        const std::string out = stringOf(*embedded);
        QVERIFY(out.find("/Title (Old title)") != std::string::npos);
        QVERIFY(out.ends_with("%%EOF\n"));

        const auto extracted = format::extractFromPdf(*embedded);
        QVERIFY(extracted.has_value());
        QCOMPARE(QString::fromStdString(*extracted), QString::fromStdString(kDiagram));
    }

    // A diagram saved with an XML declaration (every file that had one) must
    // still be found in the PDF Subject.
    void pdfXmlWithDeclarationRoundTrips()
    {
        const std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<mxfile><diagram id=\"d\"/></mxfile>\n";
        const auto embedded = format::embedInPdf(bytesOf(minimalPdf()), xml);
        QVERIFY(embedded.has_value());
        const auto extracted = format::extractFromPdf(*embedded);
        QVERIFY(extracted.has_value());
        QCOMPARE(*extracted, xml);
    }

    void pdfXmlWithParenthesesRoundTrips()
    {
        const std::string xml = "<mxfile><diagram name=\"a(b)c\\d\">x</diagram></mxfile>";
        const auto embedded = format::embedInPdf(bytesOf(minimalPdf()), xml);
        QVERIFY(embedded.has_value());
        const auto extracted = format::extractFromPdf(*embedded);
        QVERIFY(extracted.has_value());
        QCOMPARE(QString::fromStdString(*extracted), QString::fromStdString(xml));
    }

    void pdfWithoutTrailerIsRejected()
    {
        QVERIFY(!format::embedInPdf(bytesOf("%PDF-1.4\n%%EOF\n"), kDiagram).has_value());
    }

    void readAnyDocumentReadsEmbeddedPng()
    {
        const auto embedded = format::embedInPng(basePng(), kModel);
        QVERIFY(embedded.has_value());
        const format::ReadResult result = format::readAnyDocument(*embedded);
        QVERIFY(result.ok());
        QCOMPARE(result.document->pageCount(), std::size_t(1));
    }

    void readAnyDocumentReportsPngWithoutDiagram()
    {
        const format::ReadResult result = format::readAnyDocument(basePng());
        QVERIFY(!result.ok());
        QVERIFY(result.hasErrors());
        QCOMPARE(QString::fromStdString(result.diagnostics.front().message), QStringLiteral("no embedded diagram"));
    }
};

QTEST_GUILESS_MAIN(EmbedTest)
#include "tst_embed.moc"
