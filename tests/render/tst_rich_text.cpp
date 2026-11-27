// Label text: lines, and the HTML sanitizer's whitelist (src/render/rich_text.cpp).

#include "qraw/format/xml_io.h"
#include "qraw/render/text.h"

#include <QTest>

#include <algorithm>

using namespace qraw::render;

namespace {

using Lines = std::vector<std::string>;

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

// The sanitizer promises a well-formed fragment: it must parse once wrapped.
bool wellFormed(const std::string& fragment)
{
    return qraw::format::parseXml("<div>" + fragment + "</div>").document.has_value();
}

} // namespace

class TstRichText : public QObject {
    Q_OBJECT
private slots:
    void tableSizeAttributes()
    {
        // The size given as style is also given as attribute (Qt reads only the attributes); attributes
        // that are there stay, and the others of the table that a browser reads are kept.
        const std::string out = sanitizeHtml(
            "<table border=\"1\" cellpadding=\"4\" style=\"width: 100% ; height: 300px ; border-collapse: collapse\">"
            "<tr><td>a</td></tr></table>");
        QVERIFY2(contains(out, "width=\"100%\""), out.c_str());
        QVERIFY2(contains(out, "height=\"300px\""), out.c_str());
        QVERIFY(contains(out, "border=\"1\""));
        QVERIFY(contains(out, "cellpadding=\"4\""));
        QVERIFY(wellFormed(out));
        QCOMPARE(sanitizeHtml(out), out); // idempotent

        const std::string own = sanitizeHtml("<table width=\"50%\" style=\"width: 100%\"><tr><td>a</td></tr></table>");
        QVERIFY2(contains(own, "width=\"50%\""), own.c_str());
        QVERIFY(!contains(own, "width=\"100%\""));
        // Not a length: dropped, and "max-width" is not "width".
        const std::string bad = sanitizeHtml("<table width=\"x\" style=\"max-width: 10px\"><tr><td>a</td></tr></table>");
        QVERIFY2(!contains(bad, "width=\""), bad.c_str());
    }

    void plainLines()
    {
        const RichText t = makeRichText("one\ntwo\n\nfour", false);
        QVERIFY(!t.html);
        QCOMPARE(t.content, std::string("one\ntwo\n\nfour"));
        QCOMPARE(t.lines, (Lines{"one", "two", "", "four"}));
    }

    void htmlLines_data()
    {
        QTest::addColumn<QString>("html");
        QTest::addColumn<QStringList>("lines");
        QTest::newRow("br") << "<b>a</b><br>b" << QStringList{"a", "b"};
        QTest::newRow("divs") << "<div>x</div><div>y</div>" << QStringList{"x", "y"};
        QTest::newRow("trailing br") << "a<br>" << QStringList{"a"};
        QTest::newRow("paragraphs") << "<p>a<p>b" << QStringList{"a", "b"};
        QTest::newRow("list") << "<ul><li>a<li>b</ul>" << QStringList{"a", "b"};
        QTest::newRow("entities") << "a &lt; b &amp;&nbsp;c &#65; &unknown;" << QStringList{QString::fromUtf8("a < b &\u00a0c A &unknown;")};
        QTest::newRow("whitespace collapses") << "a   b" << QStringList{"a b"};
        QTest::newRow("pre keeps") << "<pre>a  b\nc</pre>" << QStringList{"a  b", "c"};
    }
    void htmlLines()
    {
        QFETCH(QString, html);
        QFETCH(QStringList, lines);
        const RichText t = makeRichText(html.toStdString(), true);
        QVERIFY(t.html);
        QStringList actual;
        for (const std::string& l : t.lines)
            actual << QString::fromStdString(l);
        QCOMPARE(actual, lines);
        QVERIFY(wellFormed(t.content));
    }

    // A <br> that ends a block ends the line and adds none (FIDELITY class 4); a lone one is a blank
    // line and the second of two is too.
    void trailingBreakEndsTheBlockOnly_data()
    {
        QTest::addColumn<QString>("html");
        QTest::addColumn<QString>("content");
        QTest::newRow("paragraph") << "<p>a<br></p><p>b</p>" << "<p>a</p><p>b</p>";
        QTest::newRow("end of label") << "a<br>" << "a";
        QTest::newRow("whitespace after") << "<div>a<br>  </div>" << "<div>a</div>";
        QTest::newRow("through inline") << "<p><b>a<br></b></p>" << "<p><b>a</b></p>";
        QTest::newRow("only the last") << "<p>a<br><br></p>" << "<p>a<br/></p>";
        QTest::newRow("lone break is a blank line") << "<div><br></div>" << "<div><br/></div>";
        QTest::newRow("break between lines") << "<p>a<br>b</p>" << "<p>a<br/>b</p>";
        QTest::newRow("cell") << "<table><tr><td>a<br></td></tr></table>" << "<table><tr><td>a</td></tr></table>";
    }
    void trailingBreakEndsTheBlockOnly()
    {
        QFETCH(QString, html);
        QFETCH(QString, content);
        const RichText t = makeRichText(html.toStdString(), true);
        QCOMPARE(QString::fromStdString(t.content), content);
    }

    void dropsActiveContent()
    {
        const std::string out = sanitizeHtml("a<script>alert(1)</script><style>p{}</style><iframe src=x>t</iframe>b<svg><b>s</b></svg>");
        QCOMPARE(out, std::string("ab"));
    }

    void dropsEventHandlers()
    {
        const std::string out = sanitizeHtml("<b onclick=\"x()\" onmouseover='y()'>t</b>");
        QCOMPARE(out, std::string("<b>t</b>"));
    }

    void imageSources()
    {
        QVERIFY(!contains(sanitizeHtml("<img src=\"http://x/y.png\">"), "http"));
        const std::string kept = sanitizeHtml("<img src=\"data:image/png;base64,AAAA\" width=10>");
        QVERIFY(contains(kept, "src=\"data:image/png;base64,AAAA\""));
        QVERIFY(contains(kept, "/>"));
        QVERIFY(wellFormed(kept));
    }

    void linkSchemes()
    {
        QVERIFY(!contains(sanitizeHtml("<a href=\"javascript:alert(1)\">l</a>"), "href"));
        QVERIFY(!contains(sanitizeHtml("<a href=\" JaVaScRiPt:alert(1)\">l</a>"), "href"));
        QVERIFY(contains(sanitizeHtml("<a href=\"https://example.com/?a=1&b=2\">l</a>"), "href=\"https://example.com/?a=1&amp;b=2\""));
        QVERIFY(contains(sanitizeHtml("<a href=\"mailto:a@b.c\">l</a>"), "href=\"mailto:a@b.c\""));
        QVERIFY(contains(sanitizeHtml("<a href=\"#top\">l</a>"), "href=\"#top\""));
    }

    void styleFiltering()
    {
        const std::string out = sanitizeHtml("<span style=\"color:red;background:url(x);font-size:12px;position:fixed\">t</span>");
        QVERIFY(contains(out, "color: red"));
        QVERIFY(contains(out, "font-size: 12px"));
        QVERIFY(!contains(out, "url"));
        QVERIFY(!contains(out, "position"));
    }

    void fontTagKept()
    {
        QCOMPARE(sanitizeHtml("<font color=\"red\" face=Arial>t</font>"), std::string("<font color=\"red\" face=\"Arial\">t</font>"));
    }

    void closesAndRepairsTags()
    {
        QCOMPARE(sanitizeHtml("<b>open"), std::string("<b>open</b>"));
        QCOMPARE(sanitizeHtml("a</i>b"), std::string("ab"));
        const std::string nested = sanitizeHtml("<b><i>x</b></i>");
        QVERIFY(wellFormed(nested));
        QVERIFY(contains(nested, "x"));
    }

    void voidElementsAndEntities()
    {
        const std::string out = sanitizeHtml("a<br>b<hr>&nbsp;&copy;");
        QVERIFY(contains(out, "<br/>"));
        QVERIFY(contains(out, "<hr/>"));
        QVERIFY(contains(out, "&#160;"));
        QVERIFY(!contains(out, "&nbsp;"));
        QVERIFY(wellFormed(out));
    }

    void literalNewlinesBecomeBreaks()
    {
        const RichText t = makeRichText("a\nb", true);
        QCOMPARE(t.content, std::string("a<br/>b"));
        QCOMPARE(t.lines, (Lines{"a", "b"}));
    }

    // Found by the fuzzer: an element past the depth limit used to stay as an empty leaf, whose end tag
    // closed an ancestor when the output was sanitized again.
    void deepNestingIsDroppedAndIdempotent()
    {
        std::string deep;
        for (int i = 0; i < 300; ++i)
            deep += "<b>";
        deep += "text";
        const std::string once = sanitizeHtml(deep);
        QVERIFY(contains(once, "text"));
        QVERIFY(wellFormed(once));
        QCOMPARE(sanitizeHtml(once), once);
        QCOMPARE(std::count(once.begin(), once.end(), '<'), std::ptrdiff_t(2 * 199)); // 199 open and 199 close tags
    }

    // Found by the fuzzer: characters XML cannot carry were filtered on, then dropped by the serializer.
    void controlCharactersAreDroppedBeforeFiltering()
    {
        const std::string once = sanitizeHtml("<font size=\"\x17\">a&#1;b\x02" "c&#xFFFE;</font>");
        QCOMPARE(once, std::string("<font>abc</font>"));
        QCOMPARE(sanitizeHtml(once), once);
    }

    // Found by the fuzzer: a heading after "<h1><p>" nested inside the first one.
    void headingClosesHeadingThroughParagraph()
    {
        QCOMPARE(sanitizeHtml("<h1><p><h1>"), std::string("<h1><p></p></h1><h1></h1>"));
    }

    void hostileInputStaysWellFormed()
    {
        for (const char* s : {"<<<>>>", "<a href='x", "<b <i>", "&", "<!-- c --><!-->", "<div/><p/>", "</", "<img src=\"data:image/png,\"\"x\">"})
            QVERIFY2(wellFormed(sanitizeHtml(s)), s);
    }
};

QTEST_MAIN(TstRichText)
#include "tst_rich_text.moc"
