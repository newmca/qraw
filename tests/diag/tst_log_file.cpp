// The GUI log file: line format, rotation at start, the size cap, threads, and the message handler
// that chains to the previous one.
#include "qraw/diag/log_file.h"

#include <QtTest>

#include <thread>
#include <vector>

using namespace qraw::diag;

namespace {

QStringList lines(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll()).split(u'\n', Qt::SkipEmptyParts);
}

QStringList received;
QMutex receivedMutex;

void recordingHandler(QtMsgType, const QMessageLogContext&, const QString& message)
{
    const QMutexLocker lock(&receivedMutex);
    received.push_back(message);
}

Q_LOGGING_CATEGORY(lcTest, "qraw.test.log", QtDebugMsg)

} // namespace

class TestLogFile : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Some distributions switch debug output off for every category by default.
        QLoggingCategory::setFilterRules(QStringLiteral("qraw.test.log.debug=true"));
    }

    void formatsALine_data()
    {
        QTest::addColumn<int>("type");
        QTest::addColumn<QString>("letter");
        QTest::newRow("debug") << int(QtDebugMsg) << "D";
        QTest::newRow("info") << int(QtInfoMsg) << "I";
        QTest::newRow("warning") << int(QtWarningMsg) << "W";
        QTest::newRow("critical") << int(QtCriticalMsg) << "C";
        QTest::newRow("fatal") << int(QtFatalMsg) << "F";
    }

    void formatsALine()
    {
        QFETCH(int, type);
        QFETCH(QString, letter);
        const QDateTime when(QDate(2026, 11, 26), QTime(12, 34, 56, 789), QTimeZone::UTC);
        QCOMPARE(LogFile::formatLine(when, static_cast<QtMsgType>(type), "qraw.ui.session", QStringLiteral("hello")),
                 QStringLiteral("2026-11-26T12:34:56.789Z %1 qraw.ui.session: hello").arg(letter));
    }

    void namesTheDefaultCategory()
    {
        const QString line = LogFile::formatLine(QDateTime::currentDateTimeUtc(), QtInfoMsg, nullptr, QStringLiteral("x"));
        QVERIFY(line.endsWith(QStringLiteral(" I default: x")));
    }

    void writesLinesAndKeepsTheLastRunAsDotOne()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("logs/qraw.log");
        {
            LogFile first(path);
            QVERIFY(first.open());
            first.write(QStringLiteral("first run"));
        }
        {
            LogFile second(path);
            QVERIFY(second.open()); // creates the folder when needed, rotates the old file
            second.write(QStringLiteral("second run"));
            QCOMPARE(lines(path), QStringList{"second run"});
            QCOMPARE(lines(LogFile::previousPath(path)), QStringList{"first run"});
        }
        {
            LogFile third(path);
            QVERIFY(third.open());
            QCOMPARE(lines(LogFile::previousPath(path)), QStringList{"second run"}); // the oldest one is gone
            QVERIFY(lines(path).isEmpty());
        }
    }

    void failsQuietlyWithoutAFolder()
    {
        QTemporaryDir dir;
        QFile blocker(dir.filePath("file"));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        LogFile file(dir.filePath("file/sub/qraw.log")); // a folder cannot be made below a file
        QVERIFY(!file.open());
        QVERIFY(!file.isOpen());
        file.write(QStringLiteral("dropped")); // no crash
    }

    void emptiesTheFileAtTheCapAndSaysSo()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("qraw.log");
        LogFile file(path, 400);
        QVERIFY(file.open());
        for (int i = 0; i < 40; ++i)
            file.write(QStringLiteral("line %1 with some text").arg(i, 3, 10, QLatin1Char('0')));
        QVERIFY(QFileInfo(path).size() <= 400);
        const QStringList kept = lines(path);
        QVERIFY(kept.size() > 1);
        QVERIFY2(kept.first().contains(QStringLiteral("earlier lines of this run were dropped")), qPrintable(kept.first()));
        QVERIFY(kept.last().contains(QStringLiteral("line 039")));
    }

    void threadsDoNotInterleaveLines()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("qraw.log");
        LogFile file(path);
        QVERIFY(file.open());
        constexpr int kThreads = 8;
        constexpr int kLines = 300;
        std::vector<std::thread> jobs;
        for (int t = 0; t < kThreads; ++t) {
            jobs.emplace_back([&file, t] {
                for (int i = 0; i < kLines; ++i)
                    file.write(QStringLiteral("thread %1 line %2 ").arg(t).arg(i) + QString(60, QLatin1Char('x')));
            });
        }
        for (auto& job : jobs)
            job.join();
        const QStringList written = lines(path);
        QCOMPARE(written.size(), kThreads * kLines);
        const QRegularExpression whole(QStringLiteral("^thread \\d+ line \\d+ x{60}$"));
        for (const QString& line : written)
            QVERIFY2(whole.match(line).hasMatch(), qPrintable(line));
    }

    void handlerWritesTheCategoryAndChains()
    {
        QTemporaryDir dir;
        LogFile file(dir.filePath("qraw.log"));
        QVERIFY(file.open());
        received.clear();
        const QtMessageHandler original = qInstallMessageHandler(recordingHandler);
        installLogHandler(file); // chains to recordingHandler, which is what was installed before
        qCInfo(lcTest) << "an info message";
        qCDebug(lcTest) << "a debug message";
        qCWarning(lcTest) << "a warning";
        uninstallLogHandler();
        qCWarning(lcTest) << "after the handler is gone";
        qInstallMessageHandler(original);

        const QStringList written = lines(file.path());
        QVERIFY2(written.size() == 3, qPrintable(written.join("|")));
        QVERIFY2(written[0].contains(QStringLiteral(" I qraw.test.log: an info message")), qPrintable(written[0]));
        QVERIFY2(written[1].contains(QStringLiteral(" D qraw.test.log: a debug message")), qPrintable(written[1]));
        QVERIFY2(written[2].contains(QStringLiteral(" W qraw.test.log: a warning")), qPrintable(written[2]));
        QVERIFY(QRegularExpression(QStringLiteral("^\\d{4}-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\d\\.\\d{3}Z ")).match(written[0]).hasMatch());
        QCOMPARE(received, (QStringList{"an info message", "a debug message", "a warning", "after the handler is gone"}));
    }

    void handlerIsSafeFromWorkerThreads()
    {
        QTemporaryDir dir;
        LogFile file(dir.filePath("qraw.log"));
        QVERIFY(file.open());
        received.clear();
        const QtMessageHandler original = qInstallMessageHandler(recordingHandler);
        installLogHandler(file);
        std::vector<std::thread> jobs;
        for (int t = 0; t < 4; ++t)
            jobs.emplace_back([t] {
                for (int i = 0; i < 100; ++i)
                    qCInfo(lcTest) << "worker" << t << "message" << i;
            });
        for (auto& job : jobs)
            job.join();
        uninstallLogHandler();
        qInstallMessageHandler(original);
        QCOMPARE(lines(file.path()).size(), 400);
        QCOMPARE(received.size(), 400);
    }
};

QTEST_MAIN(TestLogFile)
#include "tst_log_file.moc"
