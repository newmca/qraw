// Export: format names, embedded XML, sizing and the four output formats.
// Content checks that need other packages are guarded with the probes; the
// sizing, plumbing and error paths run everywhere.

#include "qraw/format/codec.h"
#include "qraw/format/embed.h"
#include "qraw/format/semantic.h"
#include "qraw/render/export.h"
#include "qraw/render/image_cache.h"


#include <QBuffer>
#include <QGuiApplication>
#include <QImage>
#include <QTest>

using namespace qraw;
using render::ExportFormat;

namespace {

// Two pages; page 0 holds one 100x50 box at (30, 40) without a stroke, so its
// bounds are exactly 100x50.
core::Document sampleDocument()
{
    const QByteArray xml =
        "<mxfile>"
        "<diagram id=\"p1\" name=\"One\"><mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
        "<mxCell id=\"a\" value=\"\" style=\"rounded=0;fillColor=#ff0000;strokeWidth=0;strokeColor=none\" vertex=\"1\" parent=\"1\">"
        "<mxGeometry x=\"30\" y=\"40\" width=\"100\" height=\"50\" as=\"geometry\"/></mxCell></root></mxGraphModel></diagram>"
        "<diagram id=\"p2\" name=\"Two\"><mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
        "<mxCell id=\"b\" value=\"\" style=\"ellipse\" vertex=\"1\" parent=\"1\">"
        "<mxGeometry x=\"0\" y=\"0\" width=\"60\" height=\"60\" as=\"geometry\"/></mxCell></root></mxGraphModel></diagram>"
        "</mxfile>";
    auto read = format::readDocument(xml);
    if (!read.ok())
        qFatal("fixture does not parse");
    return std::move(*read.document);
}

render::ExportOptions options(ExportFormat f)
{
    render::ExportOptions o;
    o.format = f;
    return o;
}

bool hasError(const render::ExportResult& r)
{
    for (const auto& d : r.diagnostics)
        if (d.severity == core::Diagnostic::Severity::Error)
            return true;
    return false;
}

QImage decode(const QByteArray& data)
{
    QImage image;
    image.loadFromData(data);
    return image;
}

// Expected pixel size from the page's own bounds: the Graph.getSvg formula.
QSize expectedSize(const core::Document& doc, std::size_t page, double scale, double border)
{
    const auto view = render::PageView::build(*doc.page(page), render::RenderContext::defaults());
    const auto b = view.bounds();
    return {static_cast<int>(std::max(1.0, std::ceil(b.width * scale) + 2 * border)),
            static_cast<int>(std::max(1.0, std::ceil(b.height * scale) + 2 * border))};
}

// A page of images with alpha, drawn smaller and larger than their own size,
// rounded, rotated, flipped, with a caption: what the image cache must not change.
core::Document imageDocument()
{
    QImage png(97, 61, QImage::Format_ARGB32);
    for (int y = 0; y < png.height(); ++y)
        for (int x = 0; x < png.width(); ++x)
            png.setPixel(x, y, qRgba(x * 255 / 97, y * 255 / 61, (x + y) * 7 & 255, 40 + (x * 3 + y * 5) % 200));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    png.save(&buffer, "PNG");
    const QByteArray uri = "data:image/png," + bytes.toBase64();
    const QByteArray svg = "data:image/svg+xml," +
                           QByteArray("<svg xmlns='http://www.w3.org/2000/svg' width='40' height='20'><rect width='40' height='20' fill='#2a6'/></svg>").toBase64();
    const auto cell = [&](const char* id, const QByteArray& extra, int x, int y, int w, int h, const QByteArray& image) {
        return "<mxCell id=\"" + QByteArray(id) + "\" value=\"" + QByteArray(id) + "\" style=\"shape=image;image=" + image +
               ";" + extra + "\" vertex=\"1\" parent=\"1\"><mxGeometry x=\"" + QByteArray::number(x) + "\" y=\"" +
               QByteArray::number(y) + "\" width=\"" + QByteArray::number(w) + "\" height=\"" + QByteArray::number(h) +
               "\" as=\"geometry\"/></mxCell>";
    };
    const QByteArray xml = "<mxfile><diagram id=\"p\" name=\"P\"><mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>" +
                           cell("small", "", 0, 0, 40, 25, uri) + cell("big", "imageAspect=0;", 60, 0, 200, 130, uri) +
                           cell("round", "rounded=1;imageBorder=#333333;", 0, 150, 60, 60, uri) +
                           cell("rot", "rotation=30;flipH=1;", 80, 160, 70, 50, uri) + cell("vector", "", 170, 160, 80, 40, svg) +
                           "</root></mxGraphModel></diagram></mxfile>";
    auto read = format::readDocument(xml);
    if (!read.ok())
        qFatal("image fixture does not parse");
    return std::move(*read.document);
}

} // namespace

class ExportTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QVERIFY(qGuiApp != nullptr); }

    void formatNames()
    {
        QCOMPARE(render::parseExportFormat("svg"), std::optional(ExportFormat::Svg));
        QCOMPARE(render::parseExportFormat("PNG"), std::optional(ExportFormat::Png));
        QCOMPARE(render::parseExportFormat("jpg"), std::optional(ExportFormat::Jpeg));
        QCOMPARE(render::parseExportFormat("jpeg"), std::optional(ExportFormat::Jpeg));
        QCOMPARE(render::parseExportFormat("pdf"), std::optional(ExportFormat::Pdf));
        QVERIFY(!render::parseExportFormat("gif").has_value());
        QCOMPARE(render::extensionFor(ExportFormat::Svg), std::string_view("svg"));
        QCOMPARE(render::extensionFor(ExportFormat::Jpeg), std::string_view("jpg"));
    }

    void embeddedXmlRoundTrips()
    {
        const auto doc = sampleDocument();
        const std::string xml = render::embeddedXml(doc);
        QVERIFY(!xml.empty());
        const auto read = format::readDocument(QByteArrayView(xml.data(), static_cast<qsizetype>(xml.size())));
        QVERIFY(read.ok());
        std::string difference;
        QVERIFY2(format::semanticEqual(doc, *read.document, &difference), difference.c_str());
    }

    void pngSizeFollowsScaleAndBorder()
    {
        const auto doc = sampleDocument();
        auto o = options(ExportFormat::Png);
        o.scale = 2;
        o.border = 10;
        const auto result = render::exportPage(doc, 0, o);
        QVERIFY(result.ok());
        QVERIFY(!hasError(result));
        const QImage image = decode(result.data);
        QCOMPARE(image.size(), expectedSize(doc, 0, 2, 10));
        QCOMPARE(image.size(), QSize(220, 120)); // 100x50 at scale 2 plus a border of 10
    }

    void transparentPngHasAlphaBackground()
    {
        const auto doc = sampleDocument();
        auto o = options(ExportFormat::Png);
        o.border = 10;
        o.transparent = true;
        const QImage image = decode(render::exportPage(doc, 0, o).data);
        QVERIFY(!image.isNull());
        QCOMPARE(qAlpha(image.pixel(0, 0)), 0);
    }

    void opaquePngDefaultsToWhite()
    {
        const auto doc = sampleDocument();
        auto o = options(ExportFormat::Png);
        o.border = 10;
        const QImage image = decode(render::exportPage(doc, 0, o).data);
        QCOMPARE(image.pixelColor(0, 0), QColor(Qt::white));
    }

    void maxPixelSizeReducesScale()
    {
        const auto doc = sampleDocument();
        auto o = options(ExportFormat::Png);
        o.scale = 10;
        o.maxPixelSize = 200;
        const auto result = render::exportPage(doc, 0, o);
        const QImage image = decode(result.data);
        QVERIFY(!image.isNull());
        QVERIFY(image.width() <= 200 && image.height() <= 200);
        bool warned = false;
        for (const auto& d : result.diagnostics)
            warned = warned || d.severity == core::Diagnostic::Severity::Warning;
        QVERIFY(warned);
    }

    void jpegDecodesOnWhite()
    {
        const auto doc = sampleDocument();
        auto o = options(ExportFormat::Jpeg);
        o.border = 10;
        o.transparent = true; // JPEG has no alpha: still white
        const auto result = render::exportPage(doc, 0, o);
        QVERIFY(result.ok());
        const QImage image = decode(result.data);
        QCOMPARE(image.size(), expectedSize(doc, 0, 1, 10));
        QVERIFY(image.pixelColor(0, 0).lightness() > 240);
    }

    void svgHasSizeAndParses()
    {
        const auto doc = sampleDocument();
        auto o = options(ExportFormat::Svg);
        o.border = 10;
        const auto result = render::exportPage(doc, 0, o);
        QVERIFY(result.ok());
        const auto parsed = format::parseXml(result.data);
        QVERIFY(parsed.document.has_value());
        const QSize size = expectedSize(doc, 0, 1, 10);
        QVERIFY(result.data.contains(QByteArray("width=\"") + QByteArray::number(size.width()) + "px\""));
        QVERIFY(result.data.contains(QByteArray("height=\"") + QByteArray::number(size.height()) + "px\""));
    }

    void svgEmbedRoundTrips()
    {
        const auto doc = sampleDocument();
        auto o = options(ExportFormat::Svg);
        o.embed = true;
        const auto result = render::exportPage(doc, 0, o);
        const auto xml = format::extractFromSvg(result.data);
        QVERIFY(xml.has_value());
        const auto read = format::readDocument(QByteArrayView(xml->data(), static_cast<qsizetype>(xml->size())));
        QVERIFY(read.ok());
        std::string difference;
        QVERIFY2(format::semanticEqual(doc, *read.document, &difference), difference.c_str());
    }

    void pngEmbedRoundTrips()
    {
        const auto doc = sampleDocument();
        auto o = options(ExportFormat::Png);
        o.embed = true;
        const auto result = render::exportPage(doc, 0, o);
        QVERIFY(result.ok());
        QVERIFY(!decode(result.data).isNull());
        const auto read = format::readAnyDocument(result.data);
        QVERIFY(read.ok());
        std::string difference;
        QVERIFY2(format::semanticEqual(doc, *read.document, &difference), difference.c_str());
    }

    void pdfStartsWithMagicAndEmbeds()
    {
        const auto doc = sampleDocument();
        const auto plain = render::exportPage(doc, 0, options(ExportFormat::Pdf));
        QVERIFY(plain.data.startsWith("%PDF"));
        auto o = options(ExportFormat::Pdf);
        o.embed = true;
        const auto embedded = render::exportPage(doc, 0, o);
        const auto xml = format::extractFromPdf(embedded.data);
        QVERIFY(xml.has_value());
        const auto read = format::readDocument(QByteArrayView(xml->data(), static_cast<qsizetype>(xml->size())));
        QVERIFY(read.ok());
    }

    void multiPagePdfHasOnePagePerPage()
    {
        const auto doc = sampleDocument();
        const auto result = render::exportPdf(doc, {}, options(ExportFormat::Pdf));
        QVERIFY(result.data.startsWith("%PDF"));
        // "/Type /Page" also prefixes "/Type /Pages": count the exact page objects.
        qsizetype pages = 0;
        for (qsizetype at = result.data.indexOf("/Type /Page"); at >= 0; at = result.data.indexOf("/Type /Page", at + 1)) {
            const char next = at + 11 < result.data.size() ? result.data.at(at + 11) : '\0';
            if (next != 's')
                ++pages;
        }
        QCOMPARE(pages, qsizetype(2));
        const std::vector<std::size_t> one{1};
        const auto single = render::exportPdf(doc, one, options(ExportFormat::Pdf));
        QVERIFY(single.data.startsWith("%PDF"));
    }

    void outOfRangePageIsAnError()
    {
        const auto doc = sampleDocument();
        for (const auto f : {ExportFormat::Svg, ExportFormat::Png, ExportFormat::Pdf}) {
            const auto result = render::exportPage(doc, 7, options(f));
            QVERIFY(result.data.isEmpty());
            QVERIFY(hasError(result));
        }
        const std::vector<std::size_t> bad{9};
        QVERIFY(hasError(render::exportPdf(doc, bad, options(ExportFormat::Pdf))));
    }

    // Export draws through the exact path: the image cache decodes once but the
    // pixels, and the embedded SVG, are what an uncached export produces.
    void imageCacheDoesNotChangeExports()
    {
        const auto doc = imageDocument();
        auto png = options(ExportFormat::Png);
        png.scale = 0.7;
        png.border = 4;
        auto svg = options(ExportFormat::Svg);
        auto big = options(ExportFormat::Png);
        big.scale = 2.5;
        for (const auto& o : {png, svg, big}) {
            render::ImageCache::instance().clear();
            render::ImageCache::instance().setEnabled(false);
            const QByteArray uncached = render::exportPage(doc, 0, o).data;
            render::ImageCache::instance().setEnabled(true);
            const QByteArray cold = render::exportPage(doc, 0, o).data;
            const QByteArray warm = render::exportPage(doc, 0, o).data;
            QVERIFY(!uncached.isEmpty());
            QVERIFY2(uncached == cold, "cache enabled, cold");
            QVERIFY2(uncached == warm, "cache enabled, warm");
        }
        // Something was drawn.
        const QImage image = decode(render::exportPage(doc, 0, png).data);
        QVERIFY(!image.isNull());
        QVERIFY(image.pixelColor(20, 14).alpha() > 0);
    }

    void cellIdsRestrictTheBounds()
    {
        const auto doc = sampleDocument();
        auto o = options(ExportFormat::Png);
        o.cellIds = {"a"};
        const QImage image = decode(render::exportPage(doc, 0, o).data);
        QCOMPARE(image.size(), expectedSize(doc, 0, 1, 0));
    }
};

QTEST_MAIN(ExportTest)
#include "tst_export.moc"
