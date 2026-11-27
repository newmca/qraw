// Data-driven round-trip test over every diagram file in the corpus
// directories (tests/corpus/ and tests/compat/fixtures/). Files are
// discovered when the test runs. For each file:
//   1. it loads without errors,
//   2. load -> save is semantically equal to the original file,
//   3. save -> reload is semantically equal to the loaded document,
//   4. the second save is byte-identical to the first.
#include "qraw/format/codec.h"
#include "qraw/format/semantic.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTest>

#include <algorithm>
#include <vector>

using namespace qraw;

namespace {

struct FileResult {
    QString path;
    QString failure; // empty: pass
};

QStringList corpusFiles()
{
    QStringList files;
    const QStringList dirs = QString::fromUtf8(QRAW_CORPUS_DIRS).split(u'|', Qt::SkipEmptyParts);
    for (const QString& dir : dirs) {
        QDirIterator it(dir, {"*.drawio", "*.xml"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            files << it.next();
    }
    std::sort(files.begin(), files.end());
    return files;
}

QString firstError(const format::ReadResult& result)
{
    for (const auto& d : result.diagnostics) {
        if (d.severity == core::Diagnostic::Severity::Error)
            return QStringLiteral("line %1:%2: %3").arg(d.line).arg(d.column).arg(QString::fromStdString(d.message));
    }
    return QStringLiteral("unknown error");
}

// Returns an empty string on success, otherwise the first failed check.
QString roundTrip(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("cannot open: ") + file.errorString();
    const QByteArray original = file.readAll();

    const auto loaded = format::readDocument(original);
    if (loaded.hasErrors())
        return QStringLiteral("load failed: ") + firstError(loaded);

    const QByteArray first = format::writeDocument(*loaded.document);
    std::string difference;
    if (!format::semanticEqual(original, first, &difference))
        return QStringLiteral("save differs from original: ") + QString::fromStdString(difference);

    const auto reloaded = format::readDocument(first);
    if (reloaded.hasErrors())
        return QStringLiteral("reload failed: ") + firstError(reloaded);
    if (!format::semanticEqual(*loaded.document, *reloaded.document, &difference))
        return QStringLiteral("reload differs: ") + QString::fromStdString(difference);

    const QByteArray second = format::writeDocument(*reloaded.document);
    if (second != first) {
        const auto mismatch = std::mismatch(first.begin(), first.end(), second.begin(), second.end());
        return QStringLiteral("second save not byte-identical (first difference at byte %1)")
            .arg(mismatch.first - first.begin());
    }
    return {};
}

} // namespace

class TestCorpus : public QObject {
    Q_OBJECT

private slots:
    void roundTrip_data()
    {
        QTest::addColumn<QString>("path");
        const QStringList files = corpusFiles();
        if (files.isEmpty())
            QSKIP("no corpus files found");
        const QString root = QDir::cleanPath(QStringLiteral(QRAW_SOURCE_DIR));
        for (const QString& f : files)
            QTest::newRow(qPrintable(QDir(root).relativeFilePath(f))) << f;
    }

    void roundTrip()
    {
        QFETCH(QString, path);
        const QString failure = ::roundTrip(path);
        results_.push_back({path, failure});
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
    }

    void cleanupTestCase()
    {
        const auto failed = std::count_if(results_.begin(), results_.end(),
                                          [](const FileResult& r) { return !r.failure.isEmpty(); });
        const QDir root(QStringLiteral(QRAW_SOURCE_DIR));
        QString table = QStringLiteral("\nCorpus round-trip: %1 files, %2 passed, %3 failed\n")
                            .arg(results_.size()).arg(results_.size() - failed).arg(failed);
        for (const auto& r : results_) {
            table += (r.failure.isEmpty() ? QStringLiteral("  PASS  ") : QStringLiteral("  FAIL  "))
                + root.relativeFilePath(r.path);
            if (!r.failure.isEmpty())
                table += QStringLiteral("  -- ") + r.failure;
            table += u'\n';
        }
        qInfo().noquote() << table;
    }

private:
    std::vector<FileResult> results_;
};

QTEST_GUILESS_MAIN(TestCorpus)
#include "tst_corpus.moc"
