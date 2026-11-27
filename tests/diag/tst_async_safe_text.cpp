// The text building of the crash handler: fixed buffers, UTC times without gmtime, signal names.
#include "qraw/diag/async_safe_text.h"

#include <QtTest>

#include <csignal>
#include <ctime>

using namespace qraw::diag;

class TestAsyncSafeText : public QObject {
    Q_OBJECT

private slots:
    void appendsText()
    {
        FixedText<64> text;
        text.append("signal ").appendDecimal(11).append(' ').appendHex(0xdeadbeef).append(" end");
        QCOMPARE(QString::fromLatin1(text.c_str()), QStringLiteral("signal 11 0xdeadbeef end"));
        QCOMPARE(text.size(), static_cast<std::size_t>(24));
    }

    void zeroIsPrinted()
    {
        FixedText<32> text;
        text.appendDecimal(0).append(' ').appendHex(0);
        QCOMPARE(QString::fromLatin1(text.c_str()), QStringLiteral("0 0x0"));
    }

    void padsDecimals()
    {
        FixedText<32> text;
        text.appendDecimal(7, 2).append(' ').appendDecimal(2026, 4).append(' ').appendDecimal(12345, 2);
        QCOMPARE(QString::fromLatin1(text.c_str()), QStringLiteral("07 2026 12345"));
    }

    void dropsWhatDoesNotFit()
    {
        FixedText<8> text;
        text.append("0123456789").append('x');
        QCOMPARE(QString::fromLatin1(text.c_str()), QStringLiteral("0123456"));
        QCOMPARE(text.size(), static_cast<std::size_t>(7));
        FixedText<4> number;
        number.appendDecimal(1234567);
        QCOMPARE(QString::fromLatin1(number.c_str()), QStringLiteral("123"));
    }

    void ignoresANullString()
    {
        FixedText<8> text;
        text.append(static_cast<const char*>(nullptr)).append("a");
        QCOMPARE(QString::fromLatin1(text.c_str()), QStringLiteral("a"));
    }

    void formatsUtcTimes_data()
    {
        QTest::addColumn<qint64>("seconds");
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<QString>("text");
        QTest::newRow("epoch") << qint64(0) << "19700101T000000Z" << "1970-01-01 00:00:00 UTC";
        QTest::newRow("before the epoch") << qint64(-1) << "19691231T235959Z" << "1969-12-31 23:59:59 UTC";
        QTest::newRow("2023") << qint64(1700000000) << "20231114T221320Z" << "2023-11-14 22:13:20 UTC";
        QTest::newRow("leap day") << qint64(951782400) << "20000229T000000Z" << "2000-02-29 00:00:00 UTC";
        QTest::newRow("end of year") << qint64(1798761599) << "20261231T235959Z" << "2026-12-31 23:59:59 UTC";
        QTest::newRow("2100 is no leap year") << qint64(4107542400) << "21000301T000000Z" << "2100-03-01 00:00:00 UTC";
    }

    void formatsUtcTimes()
    {
        QFETCH(qint64, seconds);
        QFETCH(QString, fileName);
        QFETCH(QString, text);
        FixedText<64> forFile;
        appendUtcTime(forFile, seconds, true);
        QCOMPARE(QString::fromLatin1(forFile.c_str()), fileName);
        FixedText<64> readable;
        appendUtcTime(readable, seconds, false);
        QCOMPARE(QString::fromLatin1(readable.c_str()), text);
    }

    void agreesWithQtOnManyTimes()
    {
        // Every 7 hours plus a prime of seconds from 1960 to 2100.
        for (qint64 seconds = -315619200; seconds < 4102444800; seconds += 25200 + 1013) {
            FixedText<64> text;
            appendUtcTime(text, seconds, true);
            const QString expected = QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC).toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
            if (QString::fromLatin1(text.c_str()) != expected)
                QFAIL(qPrintable(QStringLiteral("%1: %2 != %3").arg(seconds).arg(QString::fromLatin1(text.c_str()), expected)));
        }
    }

    void namesTheFatalSignals()
    {
        QCOMPARE(QString::fromLatin1(fatalSignalName(SIGSEGV)), QStringLiteral("SIGSEGV"));
        QCOMPARE(QString::fromLatin1(fatalSignalName(SIGABRT)), QStringLiteral("SIGABRT"));
        QCOMPARE(QString::fromLatin1(fatalSignalName(SIGFPE)), QStringLiteral("SIGFPE"));
        QCOMPARE(QString::fromLatin1(fatalSignalName(SIGILL)), QStringLiteral("SIGILL"));
#ifdef SIGBUS
        QCOMPARE(QString::fromLatin1(fatalSignalName(SIGBUS)), QStringLiteral("SIGBUS"));
#endif
        QCOMPARE(QString::fromLatin1(fatalSignalName(12345)), QStringLiteral("signal"));
    }
};

QTEST_APPLESS_MAIN(TestAsyncSafeText)
#include "tst_async_safe_text.moc"
