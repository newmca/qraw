// SharedValue: large attribute and style values are shared, not copied.

#include "qraw/core/cell.h"
#include "qraw/core/shared_value.h"
#include "qraw/core/style.h"

#include <QTest>

#include <string>

using namespace qraw::core;

namespace {

std::string megabytes(double mb, char fill = 'A')
{
    return std::string(static_cast<std::size_t>(mb * 1024 * 1024), fill);
}

} // namespace

class TestSharedValue : public QObject {
    Q_OBJECT

private slots:
    void init() { resetLargeValueCopies(); }

    void smallValuesStayInline()
    {
        const SharedValue v("abc");
        QVERIFY(!v.isShared());
        QCOMPARE(v.str(), std::string("abc"));
        QVERIFY(v == "abc");
        QVERIFY(v == std::string("abc"));
        QVERIFY(v == std::string_view("abc"));
        QVERIFY(v == SharedValue("abc"));
        QCOMPARE(largeValueCopies(), 0u);
    }

    void largeValuesAreSharedOnCopy()
    {
        const std::string text = megabytes(2);
        const SharedValue big(text);
        QVERIFY(big.isShared());
        QCOMPARE(largeValueCopies(), 1u); // the const& constructor copied the bytes once
        const SharedValue copy = big;
        const SharedValue assigned = copy;
        QVERIFY(copy.sameBuffer(big));
        QVERIFY(assigned.sameBuffer(big));
        QCOMPARE(copy.data(), big.data()); // the same bytes in memory
        QCOMPARE(largeValueCopies(), 1u);
    }

    void movingAnRvalueStringDoesNotCopy()
    {
        std::string text = megabytes(1);
        const char* where = text.data();
        const SharedValue v(std::move(text));
        QVERIFY(v.isShared());
        QCOMPARE(v.data(), where);
        QCOMPARE(largeValueCopies(), 0u);
    }

    void settingAReplacesTheBufferAndLeavesCopiesAlone()
    {
        SharedValue a(megabytes(1, 'A'));
        const SharedValue snapshot = a;
        a = megabytes(1, 'B');
        QCOMPARE(snapshot.str().front(), 'A');
        QCOMPARE(a.str().front(), 'B');
        QVERIFY(!a.sameBuffer(snapshot));
        a = "small";
        QVERIFY(!a.isShared());
        QCOMPARE(a.str(), std::string("small"));
        QCOMPARE(snapshot.str().size(), megabytes(1).size());
    }

    void selfAssignmentFromItsOwnBytesIsSafe()
    {
        SharedValue a(megabytes(1, 'Q'));
        a = std::string_view(a.str()).substr(10); // points into the buffer that is replaced
        QCOMPARE(a.size(), megabytes(1).size() - 10);
        QCOMPARE(a.str().front(), 'Q');
    }

    void contentHashIsMemoisedAndMatchesTheFunction()
    {
        const std::string text = megabytes(1, 'Z');
        const SharedValue v{std::string(text)};
        const SharedValue copy = v;
        QCOMPARE(v.contentHash(), hashBytes(text));
        QCOMPARE(copy.contentHash(), v.contentHash());
        QVERIFY(hashBytes("a") != hashBytes("b"));
        QVERIFY(hashBytes("") != 0);
        QVERIFY(hashBytes(megabytes(1, 'A')) != hashBytes(megabytes(1, 'B')));
        QCOMPARE(SharedValue("tiny").contentHash(), hashBytes("tiny"));
    }

    void attributesAndCellsCopyWithoutCopyingTheBytes()
    {
        Cell cell = Cell::makeVertex("v", "1", "label", "shape=image;image=data:image/png," + megabytes(2) + ";", {0, 0, 10, 10});
        const std::uint64_t before = largeValueCopies();
        const Cell copy = cell;
        const Cell snapshot = copy;
        const Attributes attributes = cell.attributes();
        QCOMPARE(largeValueCopies(), before);
        QVERIFY(copy.attributes().findValue("style")->sameBuffer(*cell.attributes().findValue("style")));
        QVERIFY(snapshot == cell);

        // An edit of the live cell replaces its buffer; the snapshot keeps the old text.
        cell.setStyleText("shape=ellipse;");
        QVERIFY(snapshot != cell);
        QVERIFY(snapshot.styleText().size() > 2'000'000);
        QCOMPARE(cell.styleText(), std::string("shape=ellipse;"));
        QCOMPARE(largeValueCopies(), before);
    }

    void styleOfALargeTextIsParsedOnceAndSharesItsTokens()
    {
        const std::string image = "data:image/png," + megabytes(2, 'x');
        Cell cell = Cell::makeVertex("v", "1", "", "shape=image;" + std::string("image=") + image + ";shadow=1;", {0, 0, 10, 10});
        resetLargeValueCopies();

        const Style first = cell.style();
        QCOMPARE(largeValueCopies(), 1u); // the token's own buffer, made by the first parse
        const Style second = cell.style();
        const Style third = Cell(cell).style();
        QCOMPARE(largeValueCopies(), 1u);
        const SharedValue* a = first.findValue("image");
        const SharedValue* b = second.findValue("image");
        const SharedValue* c = third.findValue("image");
        QVERIFY(a && b && c);
        QVERIFY(a->isShared());
        QVERIFY(a->sameBuffer(*b));
        QVERIFY(a->sameBuffer(*c));
        QCOMPARE(a->str(), image);
        QCOMPARE(first.value("shadow").value_or(""), std::string("1"));
    }

    void untouchedStyleRoundTripsByteIdentically()
    {
        const std::string text = "shape=image;image=data:image/png," + megabytes(1, 'y') + ";rounded=1;";
        Cell cell = Cell::makeVertex("v", "1", "", text, {0, 0, 10, 10});
        QCOMPARE(cell.style().toString(), text);
        QCOMPARE(cell.styleText(), text);
        // Editing another key keeps the image bytes and the order.
        Style style = cell.style();
        style.set("rounded", "0");
        QCOMPARE(style.toString(), "shape=image;image=data:image/png," + megabytes(1, 'y') + ";rounded=0;");
    }

    void changingTheStyleGivesTheNextParseTheNewText()
    {
        Cell cell = Cell::makeVertex("v", "1", "", "image=" + megabytes(1, 'a') + ";", {0, 0, 10, 10});
        QCOMPARE(cell.style().findValue("image")->str().front(), 'a');
        cell.setStyleText("image=" + megabytes(1, 'b') + ";");
        QCOMPARE(cell.style().findValue("image")->str().front(), 'b');
    }

    void tokenValueConvertsToStringForOldCallers()
    {
        const Style style("fillColor=#fff;");
        const Style::Token& token = style.tokens().front();
        const std::string& asString = token.value;
        QCOMPARE(asString, std::string("#fff"));
        QVERIFY(token.value == "#fff");
    }
};

QTEST_APPLESS_MAIN(TestSharedValue)
#include "tst_shared_value.moc"
