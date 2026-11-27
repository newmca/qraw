#include "qraw/core/style.h"

#include <QTest>

using qraw::core::Style;

class TestStyle : public QObject {
    Q_OBJECT

private slots:
    void untouchedRoundTripIsByteIdentical_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("empty") << "";
        QTest::newRow("trailing semicolon") << "rounded=0;whiteSpace=wrap;html=1;";
        QTest::newRow("no trailing semicolon") << "ellipse;fillColor=#fff";
        QTest::newRow("bare names") << "text;html=1;align=center;";
        QTest::newRow("double separators") << "a=1;;b=2;;";
        QTest::newRow("value with '='") << "image=data:image/svg+xml,PHN2Zz0=;shape=image;";
        QTest::newRow("spaces kept") << " a = 1 ; b";
        QTest::newRow("only separator") << ";";
    }
    void untouchedRoundTripIsByteIdentical()
    {
        QFETCH(QString, text);
        QCOMPARE(QString::fromStdString(Style(text.toStdString()).toString()), text);
    }

    void readsValuesAndNames()
    {
        const Style s("ellipse;whiteSpace=wrap;fillColor=#dae8fc;fillColor=#ffffff;image=a=b;");
        QCOMPARE(s.value("fillColor").value_or(""), std::string("#ffffff")); // last wins
        QCOMPARE(s.value("image").value_or(""), std::string("a=b"));
        QVERIFY(!s.value("missing").has_value());
        QCOMPARE(s.value("missing", "x"), std::string("x"));
        QVERIFY(s.hasName("ellipse"));
        QVERIFY(!s.has("ellipse"));
        QCOMPARE(s.names(), std::vector<std::string>{"ellipse"});
    }

    void editsInPlaceAndAppends()
    {
        Style s("rounded=0;whiteSpace=wrap;html=1;");
        s.set("rounded", "1");
        QCOMPARE(s.toString(), std::string("rounded=1;whiteSpace=wrap;html=1;"));
        s.set("fillColor", "#f00");
        QCOMPARE(s.toString(), std::string("rounded=1;whiteSpace=wrap;html=1;fillColor=#f00;"));

        Style plain("a=1");
        plain.set("b", "2");
        QCOMPARE(plain.toString(), std::string("a=1;b=2"));

        Style empty;
        empty.set("shape", "cylinder3");
        QCOMPARE(empty.toString(), std::string("shape=cylinder3;"));
    }

    void removesKeysAndNames()
    {
        Style s("ellipse;a=1;b=2;a=3;");
        QVERIFY(s.remove("a"));
        QCOMPARE(s.toString(), std::string("ellipse;b=2;"));
        QVERIFY(!s.remove("a"));
        QVERIFY(s.removeName("ellipse"));
        QVERIFY(s.remove("b"));
        QVERIFY(s.empty());
        QCOMPARE(s.toString(), std::string());
    }

    void addsNamesBeforeKeys()
    {
        Style s("text;html=1;");
        s.addName("ellipse");
        QCOMPARE(s.toString(), std::string("text;ellipse;html=1;"));
        s.addName("text"); // already present
        QCOMPARE(s.toString(), std::string("text;ellipse;html=1;"));
    }
};

QTEST_GUILESS_MAIN(TestStyle)
#include "tst_style.moc"
