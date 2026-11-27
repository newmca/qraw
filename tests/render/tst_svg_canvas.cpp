// SvgCanvas: exact element output, state handling, labels, document shell.


#include "qraw/format/xml_io.h"
#include "qraw/render/svg_canvas.h"

#include <QTest>

using namespace qraw::render;

namespace {

SvgDocumentOptions options(double w = 100, double h = 50)
{
    SvgDocumentOptions o;
    o.width = w;
    o.height = h;
    return o;
}

bool contains(const std::string& haystack, const std::string& needle) { return haystack.find(needle) != std::string::npos; }

qsizetype count(const std::string& haystack, const std::string& needle)
{
    qsizetype n = 0;
    for (auto at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1))
        ++n;
    return n;
}

// The element line (one per line) containing `needle`.
std::string lineWith(const std::string& svg, const std::string& needle)
{
    const auto at = svg.find(needle);
    if (at == std::string::npos)
        return {};
    const auto begin = svg.rfind('\n', at);
    const auto end = svg.find('\n', at);
    return svg.substr(begin == std::string::npos ? 0 : begin + 1, end - (begin == std::string::npos ? 0 : begin + 1));
}

} // namespace

class TstSvgCanvas : public QObject {
    Q_OBJECT
private slots:
    void rectFillStroke()
    {
        SvgCanvas svg(options());
        Canvas& c = svg;
        c.setFillColor(QColor(255, 0, 0));
        c.setStrokeColor(QColor(0, 0, 255));
        c.begin();
        c.rect(10, 20, 30, 40);
        c.fillAndStroke();
        QVERIFY(contains(svg.toSvg(), "<rect x=\"10\" y=\"20\" width=\"30\" height=\"40\" fill=\"#ff0000\" "
                                      "stroke=\"#0000ff\" pointer-events=\"all\"/>\n"));
    }

    void primitivesAndPathData()
    {
        SvgCanvas svg(options());
        Canvas& c = svg;
        c.setFillColor(QColor(0, 0, 0));
        c.begin();
        c.roundrect(0, 0, 20, 10, 3, 2);
        c.fill();
        c.begin();
        c.ellipse(0, 0, 20, 10);
        c.fill();
        c.begin();
        c.moveTo(1, 2);
        c.lineTo(3, 4);
        c.quadTo(5, 6, 7, 8);
        c.curveTo(1, 1, 2, 2, 3, 3);
        c.close();
        c.fill();
        c.begin();
        c.rect(0, 0, 5, 5);
        c.lineTo(9, 9); // mixed ops: no longer a primitive
        c.fill();
        const std::string out = svg.toSvg();
        QVERIFY(contains(out, "<rect x=\"0\" y=\"0\" width=\"20\" height=\"10\" rx=\"3\" ry=\"2\" fill=\"#000000\""));
        QVERIFY(contains(out, "<ellipse cx=\"10\" cy=\"5\" rx=\"10\" ry=\"5\" fill=\"#000000\""));
        QVERIFY(contains(out, "<path d=\"M 1 2 L 3 4 Q 5 6 7 8 C 1 1 2 2 3 3 Z\" fill=\"#000000\""));
        QCOMPARE(count(out, "<path"), 2);
    }

    void arcBecomesCurves()
    {
        SvgCanvas svg(options());
        Canvas& c = svg;
        c.setStrokeColor(QColor(0, 0, 0));
        c.begin();
        c.moveTo(0, 0);
        c.arcTo(10, 10, 0, false, true, 10, 10);
        c.stroke();
        const std::string line = lineWith(svg.toSvg(), "<path");
        QVERIFY(contains(line, "d=\"M 0 0 C "));
        QVERIFY(contains(line, " 10 10\""));
    }

    void numbersRoundedToTwoDecimals()
    {
        SvgCanvas svg(options());
        Canvas& c = svg;
        c.setStrokeColor(QColor(0, 0, 0));
        c.setStrokeWidth(2.345);
        c.begin();
        c.moveTo(1.006, 2.999);
        c.lineTo(-0.001, 3.10);
        c.stroke();
        const std::string out = svg.toSvg();
        QVERIFY(contains(out, "d=\"M 1.01 3 L 0 3.1\""));
        QVERIFY(contains(out, "stroke-width=\"2.35\""));
        QVERIFY(!contains(out, "-0 ") && !contains(out, "-0\""));
    }

    void translateAndScale()
    {
        SvgCanvas svg(options());
        Canvas& c = svg;
        c.setFillColor(QColor(0, 0, 0));
        c.translate(5, 5);
        c.scale(2);
        c.begin();
        c.rect(1, 1, 10, 10);
        c.fill();
        QVERIFY(contains(svg.toSvg(), "<rect x=\"12\" y=\"12\" width=\"20\" height=\"20\""));
    }

    void saveRestoreState()
    {
        SvgCanvas svg(options());
        Canvas& c = svg;
        c.setFillColor(QColor(255, 0, 0));
        c.save();
        c.setFillColor(QColor(0, 255, 0));
        c.translate(100, 100);
        c.restore();
        c.begin();
        c.rect(1, 2, 3, 4);
        c.fill();
        QVERIFY(contains(svg.toSvg(), "<rect x=\"1\" y=\"2\" width=\"3\" height=\"4\" fill=\"#ff0000\""));
    }

    void rotateAndFlip()
    {
        SvgCanvas svg(options());
        Canvas& c = svg;
        c.setFillColor(QColor(0, 0, 0));
        c.save();
        c.rotate(90, false, false, 10, 10);
        c.begin();
        c.rect(0, 0, 20, 20);
        c.fill();
        c.restore();
        c.save();
        c.rotate(0, true, false, 10, 10);
        c.begin();
        c.rect(0, 0, 20, 20);
        c.fill();
        c.restore();
        const std::string out = svg.toSvg();
        QVERIFY(contains(out, "transform=\"rotate(90,10,10)\""));
        QVERIFY(contains(out, "transform=\"translate(10,0)scale(-1,1)translate(-10,0)\""));
    }

    void dashing()
    {
        SvgCanvas svg(options());
        Canvas& c = svg;
        c.setStrokeColor(QColor(0, 0, 0));
        c.setStrokeWidth(2);
        c.setDashed(true, false);
        c.begin();
        c.moveTo(0, 0);
        c.lineTo(10, 0);
        c.stroke();
        c.setDashed(true, true);
        c.setDashPattern("4 2");
        c.begin();
        c.moveTo(0, 5);
        c.lineTo(10, 5);
        c.stroke();
        const std::string out = svg.toSvg();
        QVERIFY(contains(out, "stroke-dasharray=\"6 6\"")); // "3 3" * stroke width 2
        QVERIFY(contains(out, "stroke-dasharray=\"4 2\"")); // fixDash: absolute
    }

    void gradientIsDefinedOnceAndReused()
    {
        SvgCanvas svg(options());
        Canvas& c = svg;
        for (int i = 0; i < 2; ++i) {
            c.setGradient(QColor(255, 255, 255), QColor(0, 0, 0), {0, 0, 10, 10}, GradientDirection::South);
            c.begin();
            c.rect(0, 0, 10, 10);
            c.fill();
        }
        c.setGradient(QColor(255, 255, 255), QColor(0, 0, 0), {0, 0, 10, 10}, GradientDirection::Radial);
        c.begin();
        c.rect(0, 0, 10, 10);
        c.fill();
        const std::string out = svg.toSvg();
        QCOMPARE(count(out, "<linearGradient id=\"qraw-1\""), 1);
        QCOMPARE(count(out, "fill=\"url(#qraw-1)\""), 2);
        QVERIFY(contains(out, "<radialGradient id=\"qraw-2\""));
        QVERIFY(contains(out, "<stop offset=\"0%\" stop-color=\"#ffffff\" stop-opacity=\"1\"/>"));
    }

    void shadowPrecedesElement()
    {
        SvgCanvas svg(options());
        Canvas& c = svg;
        c.setFillColor(QColor(255, 0, 0));
        c.setShadow(true);
        c.begin();
        c.rect(0, 0, 10, 10);
        c.fill();
        const std::string out = svg.toSvg();
        const auto shadow = out.find("fill=\"#000000\"");
        const auto shape = out.find("fill=\"#ff0000\"");
        QVERIFY(shadow != std::string::npos && shape != std::string::npos);
        QVERIFY(shadow < shape);
        QVERIFY(contains(lineWith(out, "#000000"), "opacity=\"0.25\""));
        QVERIFY(contains(lineWith(out, "#000000"), "transform=\"translate(2,3)\""));
    }

    void nestedGroupsAndEscaping()
    {
        SvgCanvas svg(options());
        Canvas& c = svg;
        c.beginGroup("a\"<1>");
        c.beginGroup("b");
        c.endGroup();
        c.endGroup();
        c.beginGroup("unbalanced");
        const std::string out = svg.toSvg();
        QVERIFY(contains(out, "<g data-cell-id=\"a&quot;&lt;1&gt;\">\n<g data-cell-id=\"b\">\n</g>\n</g>\n"));
        QCOMPARE(count(out, "<g "), count(out, "</g>"));
    }

    void imageIsEmbedded()
    {
        SvgCanvas svg(options());
        Canvas& c = svg;
        c.image(1, 2, 30, 40, ImageData{QByteArray("PNGDATA"), QStringLiteral("image/png")}, false, false, false);
        c.image(0, 0, 5, 5, ImageData{}, false, false, false); // empty: nothing
        const std::string out = svg.toSvg();
        QCOMPARE(count(out, "<image"), 1);
        QVERIFY(contains(out, "xlink:href=\"data:image/png;base64,UE5HREFUQQ==\""));
        QVERIFY(contains(out, "preserveAspectRatio=\"none\""));
    }

    void htmlLabel()
    {
        SvgCanvas svg(options());
        Canvas& c = svg;
        TextBlock b;
        b.x = 50;
        b.y = 25;
        b.width = 80;
        b.height = 20;
        b.text = makeRichText("<b>Hi</b><br>there", true);
        b.wrap = true;
        b.fontStyle = 1;
        c.text(b);
        const std::string out = svg.toSvg();
        QVERIFY(contains(out, "<switch>"));
        QVERIFY(contains(out, "<foreignObject"));
        QVERIFY(contains(out, "<b>Hi</b><br/>there"));
        QVERIFY(contains(out, "white-space: normal"));
        QVERIFY(contains(out, "font-weight: bold"));
        QVERIFY(contains(out, "text-anchor=\"middle\""));
        QCOMPARE(count(out, "<tspan"), 2);
        QVERIFY(contains(out, ">Hi</tspan>") && contains(out, ">there</tspan>"));
    }

    void plainLabelEscapes()
    {
        SvgCanvas svg(options());
        Canvas& c = svg;
        TextBlock b;
        b.text = makeRichText("a<b & c", false);
        b.align = qraw::geometry::HAlign::Left;
        c.text(b);
        const std::string out = svg.toSvg();
        QVERIFY(contains(out, "a&lt;b &amp; c"));
        QVERIFY(contains(out, "white-space: pre"));
        QVERIFY(contains(out, "text-anchor=\"start\""));
        QVERIFY(!contains(out, "<b "));
    }

    void documentShell()
    {
        SvgDocumentOptions o = options(120, 60);
        o.background = QColor(255, 255, 255);
        o.content = "<mxfile a=\"1\" b='2'>\n\t<x>&amp;</x>\r</mxfile>";
        SvgCanvas svg(o);
        const std::string out = svg.toSvg();
        QVERIFY(out.starts_with("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE svg PUBLIC "));
        QVERIFY(contains(out, "version=\"1.1\" width=\"120px\" height=\"60px\" viewBox=\"-0.5 -0.5 120 60\" content=\""));
        QVERIFY(contains(out, "style=\"background-color: #ffffff;\""));
        QVERIFY(contains(out, "<defs>") || contains(out, "<defs/>"));
        QVERIFY(out.ends_with("</svg>\n"));
    }

    void contentAttributeRoundTrips()
    {
        const std::string content = "<mxfile a=\"1\" b='2'>\n\t<x>&amp; &lt;</x>\r</mxfile>";
        SvgDocumentOptions o = options();
        o.content = content;
        const std::string out = SvgCanvas(o).toSvg();
        const auto parsed = qraw::format::parseXml(out);
        QVERIFY2(parsed.document.has_value(), qPrintable(QString::fromStdString(parsed.error.message)));
        const std::string* attr = parsed.document->root.attributes.find("content");
        QVERIFY(attr);
        QCOMPARE(*attr, content);
    }

    void outputIsDeterministicAndWellFormed()
    {
        auto draw = [] {
            SvgCanvas svg(options());
            Canvas& c = svg;
            c.beginGroup("1");
            c.setGradient(QColor(1, 2, 3), QColor(4, 5, 6), {0, 0, 9, 9}, GradientDirection::East);
            c.setShadow(true);
            c.begin();
            c.rect(0, 0, 9, 9);
            c.fillAndStroke();
            TextBlock b;
            b.text = makeRichText("<i>t</i> & <u>u</u>", true);
            c.text(b);
            c.endGroup();
            return svg.toSvg();
        };
        const std::string first = draw();
        QCOMPARE(draw(), first);
        const auto parsed = qraw::format::parseXml(first);
        QVERIFY2(parsed.document.has_value(), qPrintable(QString::fromStdString(parsed.error.message)));
    }
};

QTEST_MAIN(TstSvgCanvas)
#include "tst_svg_canvas.moc"
