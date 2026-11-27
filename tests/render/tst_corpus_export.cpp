// Every corpus and compat fixture, every page, exported to SVG and PNG
// (docs/design/render.md section 9).

#include "qraw/format/codec.h"
#include "qraw/format/xml_io.h"
#include "qraw/render/export.h"


#include <QDirIterator>
#include <QElapsedTimer>
#include <QImage>
#include <QTest>

#include <algorithm>

using namespace qraw;

namespace {

QStringList fixtureFiles()
{
    QStringList files;
    for (const char* root : {"/tests/corpus", "/tests/compat/fixtures"}) {
        QDirIterator it(QStringLiteral(QRAW_SOURCE_DIR) + QLatin1String(root), {"*.drawio", "*.xml"}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
            files << it.next();
    }
    files.sort();
    return files;
}

} // namespace

class CorpusExportTest : public QObject {
    Q_OBJECT

private slots:
    void everyFileExports()
    {
        const QStringList files = fixtureFiles();
        QVERIFY2(files.size() > 100, "corpus not found");

        constexpr qint64 kBudgetMs = 300 * 1000;
        QElapsedTimer total;
        total.start();
        std::vector<std::pair<qint64, QString>> timings;
        int exported = 0;

        for (const QString& file : files) {
            const auto read = format::readFile(file);
            if (!read.ok())
                continue; // malformed inputs are the codec's business (M1 corpus tests)
            QElapsedTimer timer;
            timer.start();
            for (std::size_t page = 0; page < read.document->pageCount(); ++page) {
                render::ExportOptions options;
                options.maxPixelSize = 2048;
                options.format = render::ExportFormat::Svg;
                const auto svg = render::exportPage(*read.document, page, options);
                QVERIFY2(svg.ok(), qPrintable(file + ": no SVG for page " + QString::number(page)));
                QVERIFY2(format::parseXml(svg.data).document.has_value(), qPrintable(file + ": SVG does not parse"));
                options.format = render::ExportFormat::Png;
                const auto png = render::exportPage(*read.document, page, options);
                QVERIFY2(png.ok() && !QImage::fromData(png.data).isNull(), qPrintable(file + ": PNG does not decode"));
                for (const auto* r : {&svg, &png})
                    for (const auto& d : r->diagnostics)
                        QVERIFY2(d.severity != core::Diagnostic::Severity::Error,
                                 qPrintable(file + ": " + QString::fromStdString(d.message)));
                ++exported;
            }
            timings.emplace_back(timer.elapsed(), file);
            QVERIFY2(total.elapsed() < kBudgetMs, "corpus export exceeded the 300 s budget");
        }

        std::ranges::sort(timings, std::greater<>());
        for (std::size_t i = 0; i < std::min<std::size_t>(5, timings.size()); ++i)
            qInfo().noquote() << timings[i].first << "ms" << timings[i].second;
        qInfo() << exported << "pages exported in" << total.elapsed() << "ms";
    }
};

QTEST_MAIN(CorpusExportTest)
#include "tst_corpus_export.moc"
