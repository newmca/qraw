#include "qraw/format/compression.h"

#include <QTest>

using namespace qraw::format;

namespace {
// Vectors generated with Python's zlib (wbits=-15 / 15, level 6) and
// urllib.parse.quote(safe="-_.!~*'()"), which matches encodeURIComponent.
const char* const kXml =
    "<mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/><mxCell id=\"2\" "
    "value=\"H\xC3\xA9llo &amp; \xE2\x9C\x93\" style=\"rounded=1;\" vertex=\"1\" parent=\"1\"><mxGeometry x=\"10\" "
    "y=\"20\" width=\"120\" height=\"60\" as=\"geometry\"/></mxCell></root></mxGraphModel>";
const char* const kRawUri =
    "jVFBCoMwEHzN3tMNCB7bVO2ljwhkMUI0EmOrvy8ma6UHobedmZ1h2AWp+qUJerRPb8iBrECq4H3MU78ocg5QdAbkHRAFIALWJ+olqWLUgYb4jwGz"
    "4aXdTJl5gJJwLZ3zsHkL3Y8gb9tcIZQKSpktU1wdW4KfB0Nb4iWtpkQKkZbTVoniSg35nmJYAcVuyKXFyiUZvjsTLW/snKWutRxaMKenjNtv8HEB"
    "wJqPsMPj2En7+cUH";
const char* const kRawLegacy =
    "ZY+xDYMwFERX+fpFWtsUafCnSUGaDGHJXxjJxsgYAnOkzQKZI5tkksgiDUp3p9Od7umwtsmM7hYt+0anGHOjw3ph76G3hBLFwSuE0SQe8n9UISzG"
    "z0x4fb+8j3AyYazh83wgTHnzTJjiPFi2pGqEhVPm9bCosAy2HAPntEEJJcJGWEmEe2+zI1RFO+47lwnPEsFMhN2vUh6J/VKjxQ4jDoRf";
const char* const kZlibUri =
    "eJyNUUEKgzAQfM3e0w0IHttU7aWPCGQxQjQSY6u/LyZrpQeht52ZnWHYBan6pQl6tE9vyIGsQKrgfcxTvyhyDlB0BuQdEAUgAtYn6iWpYtSBhviP"
    "AbPhpd1MmXmAknAtnfOweQvdjyBv21whlApKmS1TXB1bgp8HQ1viJa2mRAqRltNWieJKDfmeYlgBxW7IpcXKJRm+OxMtb+ycpa61HFowp6eM22/w"
    "cQHAmo+ww+PYSfv5xQfT2W2p";
} // namespace

class TestCompression : public QObject {
    Q_OBJECT

private slots:
    void decodesKnownVectors_data()
    {
        QTest::addColumn<QString>("payload");
        QTest::newRow("deflateRaw + URI encoding (current)") << QString::fromLatin1(kRawUri);
        QTest::newRow("deflateRaw without URI encoding (legacy)") << QString::fromLatin1(kRawLegacy);
        QTest::newRow("zlib-wrapped") << QString::fromLatin1(kZlibUri);
    }
    void decodesKnownVectors()
    {
        QFETCH(QString, payload);
        const auto xml = decompressDiagram(payload.toStdString());
        QVERIFY(xml.has_value());
        QCOMPARE(*xml, std::string(kXml));
    }

    void toleratesWhitespaceInPayload()
    {
        std::string wrapped = std::string("\n   ") + kRawUri;
        wrapped.insert(40, "\n  ");
        QCOMPARE(decompressDiagram(wrapped).value_or(""), std::string(kXml));
    }

    void rejectsGarbage()
    {
        QVERIFY(!decompressDiagram("not base64 !!!").has_value());
        QVERIFY(!decompressDiagram("aGVsbG8gd29ybGQ=").has_value()); // base64 of non-deflate data
        QVERIFY(!decompressDiagram("").has_value());
    }

    void roundTripIsDeterministic()
    {
        const std::string a = compressDiagram(kXml);
        QCOMPARE(compressDiagram(kXml), a);
        QCOMPARE(decompressDiagram(a).value_or(""), std::string(kXml));
    }

    void uriComponentMatchesJavaScript()
    {
        QCOMPARE(encodeUriComponent("a b&c=d/\xC3\xA9!~*'()-_."), std::string("a%20b%26c%3Dd%2F%C3%A9!~*'()-_."));
        QCOMPARE(decodeUriComponent("a%20b%C3%A9"), std::string("a b\xC3\xA9"));
        QCOMPARE(decodeUriComponent("100%"), std::string("100%")); // malformed escapes stay literal
        QCOMPARE(decodeUriComponent("%zz%4"), std::string("%zz%4"));
    }
};

QTEST_GUILESS_MAIN(TestCompression)
#include "tst_compression.moc"
