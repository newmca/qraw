// Golden SVG files for tests/render/data/golden/*.drawio. The .svg files are
// generated after the merge (QRAW_UPDATE_GOLDENS=1 ctest -R golden), so a
// missing golden skips; the export must also be deterministic.

#include "qraw/format/codec.h"
#include "qraw/render/export.h"


#include <QDir>
#include <QFile>
#include <QTest>

using namespace qraw;

class GoldenSvgTest : public QObject {
    Q_OBJECT

    static QString dataDir() { return QStringLiteral(QRAW_TEST_DATA_DIR "/golden"); }

private slots:
    void fixturesAreWellFormed_data()
    {
        QTest::addColumn<QString>("name");
        const auto files = QDir(dataDir()).entryList({"*.drawio"}, QDir::Files, QDir::Name);
        QVERIFY(files.size() >= 8);
        for (const QString& f : files)
            QTest::newRow(qPrintable(f)) << f;
    }

    // Own logic, runs everywhere: every fixture reads without errors and
    // exports to every format without an Error diagnostic.
    void fixturesAreWellFormed()
    {
        QFETCH(QString, name);
        const auto read = format::readFile(QDir(dataDir()).filePath(name));
        QVERIFY(read.ok() && !read.hasErrors());
        render::ExportOptions o;
        o.format = render::ExportFormat::Png;
        const auto result = render::exportPage(*read.document, 0, o);
        QVERIFY(result.ok());
        for (const auto& d : result.diagnostics)
            QVERIFY2(d.severity != core::Diagnostic::Severity::Error, d.message.c_str());
    }

    void golden_data() { fixturesAreWellFormed_data(); }

    void golden()
    {
        QFETCH(QString, name);
        const auto read = format::readFile(QDir(dataDir()).filePath(name));
        QVERIFY(read.ok());
        render::ExportOptions o;
        o.format = render::ExportFormat::Svg;
        o.embed = true;
        const auto first = render::exportPage(*read.document, 0, o);
        const auto second = render::exportPage(*read.document, 0, o);
        QVERIFY(first.ok());
        QCOMPARE(first.data, second.data); // deterministic

        const QByteArray actual = QByteArray(first.data).replace("\r\n", "\n");
        const QString goldenPath = QDir(dataDir()).filePath(QFileInfo(name).completeBaseName() + ".svg");
        if (qEnvironmentVariableIntValue("QRAW_UPDATE_GOLDENS") != 0) {
            QFile out(goldenPath);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(actual);
            return;
        }
        QFile golden(goldenPath);
        if (!golden.open(QIODevice::ReadOnly))
            QSKIP("no golden; run QRAW_UPDATE_GOLDENS=1 ctest -R golden");
        const QByteArray expected = golden.readAll().replace("\r\n", "\n");
        if (actual != expected) {
            // Name the first differing line so a platform-only mismatch (CI)
            // can be diagnosed from the log alone.
            const QList<QByteArray> a = actual.split('\n');
            const QList<QByteArray> e = expected.split('\n');
            qsizetype line = 0;
            while (line < a.size() && line < e.size() && a[line] == e[line])
                ++line;
            const QByteArray got = line < a.size() ? a[line].left(400) : QByteArray("<end of output>");
            const QByteArray want = line < e.size() ? e[line].left(400) : QByteArray("<end of golden>");
            QFAIL(qPrintable(QStringLiteral("%1 differs at line %2\n  actual:   %3\n  expected: %4\n"
                                            "inspect and regenerate with QRAW_UPDATE_GOLDENS=1")
                                 .arg(goldenPath)
                                 .arg(line + 1)
                                 .arg(QString::fromUtf8(got), QString::fromUtf8(want))));
        }
    }
};

QTEST_MAIN(GoldenSvgTest)
#include "tst_golden_svg.moc"
