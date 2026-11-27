// qraw-fidelity-diff: scores Qraw renderings against draw.io renderings.
//
//   qraw-fidelity-diff --reference DIR --candidate DIR --sample sample.tsv --out DIR
//   qraw-fidelity-diff --pair --reference REF.png --candidate CAND.png [--composite OUT.png]
//   qraw-fidelity-diff --example ID --frac X,Y,W,H --reference DIR --candidate DIR --crop OUT.png
//       (a crop of the aligned draw.io | Qraw images, the region given as fractions of the canvas)
//
// DIR holds one PNG per sample entry, named <id>.png where <id> is the sample path
// without its extension and with '/' replaced by "__" (see tools/fidelity/run_fidelity.sh).
// Writes results.csv, summary.md and composites/<id>.png (draw.io | Qraw | heat map) to
// the output directory. Developer tool, built with -DQRAW_BUILD_TOOLS=ON, not installed.

#include "fidelity_score.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QImage>
#include <QPainter>
#include <QTextStream>

#include <algorithm>
#include <atomic>
#include <future>
#include <thread>
#include <vector>

using namespace qraw::fidelity;

namespace {

struct Entry {
    QString file, family, feature, id;
    QString status = QStringLiteral("ok");  // ok, no-reference, no-candidate, blank
    Score score;
    QString composite;
};

QString idFor(const QString& samplePath)
{
    QString id = samplePath;
    id.remove(QRegularExpression(QStringLiteral("\\.[^./]+$")));
    return id.replace(QLatin1Char('/'), QStringLiteral("__"));
}

QList<Entry> readSample(const QString& path, QString* error)
{
    QList<Entry> entries;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = QStringLiteral("cannot read %1").arg(path);
        return entries;
    }
    QTextStream in(&file);
    in.readLine();  // header
    while (!in.atEnd()) {
        const QStringList cols = in.readLine().split(QLatin1Char('\t'));
        if (cols.size() < 2 || cols[0].isEmpty())
            continue;
        Entry entry;
        entry.file = cols[0];
        entry.family = cols[1];
        entry.feature = cols.value(2);
        entry.id = idFor(entry.file);
        entries.append(entry);
    }
    return entries;
}

double percentile(std::vector<double> values, double p)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const double pos = p * static_cast<double>(values.size() - 1);
    const size_t lo = static_cast<size_t>(pos);
    const size_t hi = std::min(lo + 1, values.size() - 1);
    return values[lo] + (values[hi] - values[lo]) * (pos - static_cast<double>(lo));
}

QString pct(double v) { return QString::number(v * 100.0, 'f', 1) + QStringLiteral(" %"); }

void writeCsv(const QString& path, const QList<Entry>& entries)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream out(&file);
    out << "id,file,family,feature,status,drawio_w,drawio_h,qraw_w,qraw_h,size_mismatch,alignment,offset_x,offset_y,"
           "diff_share,coarse_share,mean_delta_e,ink_ratio,pixel_combined,combined\n";
    for (const Entry& e : entries) {
        const Score& s = e.score;
        auto quoted = [](const QString& text) { return QLatin1Char('"') + QString(text).replace('"', "\"\"") + QLatin1Char('"'); };
        out << e.id << ',' << e.file << ',' << e.family << ',' << quoted(e.feature) << ',' << e.status << ','
            << s.referenceSize.width() << ',' << s.referenceSize.height() << ',' << s.candidateSize.width() << ','
            << s.candidateSize.height() << ',' << QString::number(s.sizeMismatch, 'f', 4) << ','
            << (s.alignment == Alignment::TopLeft ? "top-left" : "centre") << ',' << s.offset.x() << ',' << s.offset.y() << ','
            << QString::number(s.diffShare, 'f', 4) << ',' << QString::number(s.coarseShare, 'f', 4) << ',' << QString::number(s.meanDeltaE, 'f', 3) << ','
            << QString::number(s.inkRatio, 'f', 3) << ',' << QString::number(s.pixelCombined, 'f', 4) << ',' << QString::number(s.combined, 'f', 4) << '\n';
    }
}

void writeSummary(const QString& path, QList<Entry> entries, const Options& options)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream out(&file);
    std::vector<double> combined, pixel, share, coarse, mismatch;
    int failed = 0;
    for (const Entry& e : entries) {
        if (e.status != QLatin1String("ok")) {
            ++failed;
            continue;
        }
        combined.push_back(e.score.combined);
        pixel.push_back(e.score.pixelCombined);
        share.push_back(e.score.diffShare);
        coarse.push_back(e.score.coarseShare);
        mismatch.push_back(e.score.sizeMismatch);
    }
    auto mean = [](const std::vector<double>& v) {
        double sum = 0;
        for (double x : v)
            sum += x;
        return v.empty() ? 0.0 : sum / static_cast<double>(v.size());
    };
    out << "# Fidelity summary\n\n"
        << "Files scored: " << combined.size() << ", failed to export or blank: " << failed
        << ". A block differs when its CIE76 deltaE exceeds " << options.deltaEThreshold << ": pixel level = 2x2 blocks, layout = "
        << options.coarseBlock << "x" << options.coarseBlock << " blocks.\n\n"
        << "| Metric | mean | median | p90 | max |\n|---|---|---|---|---|\n";
    auto row = [&](const char* name, const std::vector<double>& v) {
        out << "| " << name << " | " << pct(mean(v)) << " | " << pct(percentile(v, 0.5)) << " | " << pct(percentile(v, 0.9))
            << " | " << pct(percentile(v, 1.0)) << " |\n";
    };
    row("layout score (coarse blocks + size)", combined);
    row("pixel score (2x2 blocks + size)", pixel);
    row("differing 2x2 blocks", share);
    row("differing coarse blocks", coarse);
    row("size mismatch", mismatch);

    out << "\n## Files within a limit\n\n| Score | layout score | pixel score |\n|---|---|---|\n";
    for (double limit : {0.02, 0.05, 0.10, 0.25, 0.50}) {
        const auto layout = std::count_if(combined.begin(), combined.end(), [&](double v) { return v <= limit; });
        const auto px = std::count_if(pixel.begin(), pixel.end(), [&](double v) { return v <= limit; });
        out << "| up to " << pct(limit) << " | " << layout << " of " << combined.size() << " | " << px << " of " << pixel.size() << " |\n";
    }

    out << "\n## By family\n\n| Family | files | mean layout score | mean pixel score | mean size mismatch |\n|---|---|---|---|---|\n";
    QStringList families;
    for (const Entry& e : entries)
        if (!families.contains(e.family))
            families << e.family;
    families.sort();
    for (const QString& family : families) {
        std::vector<double> c, p, m;
        for (const Entry& e : entries)
            if (e.family == family && e.status == QLatin1String("ok")) {
                c.push_back(e.score.combined);
                p.push_back(e.score.pixelCombined);
                m.push_back(e.score.sizeMismatch);
            }
        if (!c.empty())
            out << "| " << family << " | " << c.size() << " | " << pct(mean(c)) << " | " << pct(mean(p)) << " | " << pct(mean(m)) << " |\n";
    }

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.score.combined > b.score.combined; });
    out << "\n## Worst 20 by layout score\n\n| # | id | family | layout | pixel | size | draw.io size | Qraw size | ink | composite |\n"
           "|---|---|---|---|---|---|---|---|---|---|\n";
    int rank = 0;
    for (const Entry& e : entries) {
        if (e.status != QLatin1String("ok"))
            continue;
        if (++rank > 20)
            break;
        out << "| " << rank << " | " << e.id << " | " << e.family << " | " << pct(e.score.combined) << " | "
            << pct(e.score.pixelCombined) << " | " << pct(e.score.sizeMismatch) << " | " << e.score.referenceSize.width() << "x"
            << e.score.referenceSize.height() << " | " << e.score.candidateSize.width() << "x" << e.score.candidateSize.height()
            << " | " << QString::number(e.score.inkRatio, 'f', 2) << " | " << e.composite << " |\n";
    }
    if (failed > 0) {
        out << "\n## Failed\n\n";
        for (const Entry& e : entries)
            if (e.status != QLatin1String("ok"))
                out << "- " << e.id << ": " << e.status << "\n";
    }
}

void scoreEntry(Entry& entry, const QString& refDir, const QString& candDir, const QString& compositeDir, const Options& options)
{
    const QImage ref(QDir(refDir).filePath(entry.id + QStringLiteral(".png")));
    const QImage cand(QDir(candDir).filePath(entry.id + QStringLiteral(".png")));
    if (ref.isNull() || cand.isNull()) {
        entry.status = ref.isNull() ? QStringLiteral("no-reference") : QStringLiteral("no-candidate");
        entry.score.combined = 1.0;
        return;
    }
    entry.score = compare(ref, cand, options);
    if (!entry.score.valid && entry.score.diffShare >= 1.0) {
        entry.status = QStringLiteral("blank");
        return;
    }
    const QString caption = QStringLiteral("layout %1  pixel %2  size %3  at %4")
                                .arg(pct(entry.score.combined), pct(entry.score.pixelCombined), pct(entry.score.sizeMismatch),
                                     entry.score.alignment == Alignment::TopLeft ? QStringLiteral("top-left") : QStringLiteral("centre"));
    const QImage composite = makeComposite(entry.score, caption);
    if (!composite.isNull()) {
        entry.composite = QStringLiteral("composites/%1.png").arg(entry.id);
        composite.save(QDir(compositeDir).filePath(entry.id + QStringLiteral(".png")), "PNG", 6);
    }
    // The canvases and heat map are only needed for the composite.
    entry.score.referenceCanvas = entry.score.candidateCanvas = entry.score.heatMap = QImage();
}

}  // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Scores Qraw renderings against draw.io renderings."));
    parser.addHelpOption();
    parser.addOption({"reference", "Directory with the draw.io PNGs (<id>.png).", "dir"});
    parser.addOption({"candidate", "Directory with the Qraw PNGs (<id>.png).", "dir"});
    parser.addOption({"sample", "tools/fidelity/sample.tsv: the files to score.", "tsv"});
    parser.addOption({"out", "Output directory (results.csv, summary.md, composites/).", "dir"});
    parser.addOption({"threshold", "deltaE above which a 2x2 block differs (default 10).", "n", "10"});
    parser.addOption({"pair", "Score one pair of PNG files: --pair --reference REF.png --candidate CAND.png."});
    parser.addOption({"composite", "With --pair: write the composite here.", "png"});
    parser.addOption({"example", "Crop mode: the id of the file to crop.", "id"});
    parser.addOption({"frac", "Crop mode: region as fractions of the aligned canvas: x,y,w,h (0..1).", "x,y,w,h"});
    parser.addOption({"crop", "Crop mode: output PNG.", "png"});
    parser.addOption({"panel-width", "Crop mode: width of each panel in pixels (default 420).", "px", "420"});
    parser.process(app);

    Options options;
    options.deltaEThreshold = parser.value(QStringLiteral("threshold")).toDouble();
    QTextStream out(stdout);

    if (parser.isSet(QStringLiteral("pair"))) {
        const QImage ref(parser.value(QStringLiteral("reference")));
        const QImage cand(parser.value(QStringLiteral("candidate")));
        if (ref.isNull() || cand.isNull()) {
            out << "cannot read --reference / --candidate images\n";
            return 2;
        }
        const Score s = compare(ref, cand, options);
        out << "diffShare " << s.diffShare << " sizeMismatch " << s.sizeMismatch << " combined " << s.combined
            << " meanDeltaE " << s.meanDeltaE << " inkRatio " << s.inkRatio << "\n";
        if (parser.isSet(QStringLiteral("composite")))
            makeComposite(s, QStringLiteral("diff %1").arg(pct(s.diffShare))).save(parser.value(QStringLiteral("composite")));
        return 0;
    }

    if (parser.isSet(QStringLiteral("example"))) {
        const QString id = parser.value(QStringLiteral("example"));
        const QStringList frac = parser.value(QStringLiteral("frac")).split(QLatin1Char(','));
        const QImage ref(QDir(parser.value(QStringLiteral("reference"))).filePath(id + QStringLiteral(".png")));
        const QImage cand(QDir(parser.value(QStringLiteral("candidate"))).filePath(id + QStringLiteral(".png")));
        if (frac.size() != 4 || ref.isNull() || cand.isNull() || !parser.isSet(QStringLiteral("crop"))) {
            out << "--example needs --frac x,y,w,h, --crop OUT.png and both images\n";
            return 2;
        }
        const Score s = compare(ref, cand, options);
        const QSize size = s.referenceCanvas.size();
        const QRect rect(static_cast<int>(frac[0].toDouble() * size.width()), static_cast<int>(frac[1].toDouble() * size.height()),
                         static_cast<int>(frac[2].toDouble() * size.width()), static_cast<int>(frac[3].toDouble() * size.height()));
        const int panelWidth = parser.value(QStringLiteral("panel-width")).toInt();
        // Never enlarge: a crop smaller than the panel width keeps its pixels.
        const double scale = std::min(1.0, static_cast<double>(panelWidth) / rect.width());
        const QSize panel(std::max(1, static_cast<int>(rect.width() * scale)), std::max(1, static_cast<int>(rect.height() * scale)));
        QImage image(panel.width() * 2 + 12, panel.height() + 22, QImage::Format_RGB32);
        image.fill(QColor(0x30, 0x30, 0x30));
        QPainter painter(&image);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setPixelSize(12);
        painter.setFont(font);
        painter.drawText(4, 15, QStringLiteral("draw.io"));
        painter.drawText(panel.width() + 12, 15, QStringLiteral("Qraw"));
        painter.drawImage(QRect(QPoint(0, 22), panel), s.referenceCanvas.copy(rect));
        painter.drawImage(QRect(QPoint(panel.width() + 12, 22), panel), s.candidateCanvas.copy(rect));
        painter.end();
        image.save(parser.value(QStringLiteral("crop")), "PNG", 9);
        return 0;
    }

    for (const char* required : {"reference", "candidate", "sample", "out"}) {
        if (!parser.isSet(QString::fromLatin1(required))) {
            out << "missing --" << required << "\n";
            return 2;
        }
    }
    QString error;
    QList<Entry> entries = readSample(parser.value(QStringLiteral("sample")), &error);
    if (entries.isEmpty()) {
        out << (error.isEmpty() ? QStringLiteral("empty sample") : error) << "\n";
        return 2;
    }
    const QString outDir = parser.value(QStringLiteral("out"));
    const QString compositeDir = QDir(outDir).filePath(QStringLiteral("composites"));
    QDir().mkpath(compositeDir);

    // Entries are independent: score them on a small pool of threads.
    const QString refDir = parser.value(QStringLiteral("reference"));
    const QString candDir = parser.value(QStringLiteral("candidate"));
    std::atomic<int> next{0};
    auto worker = [&] {
        for (int i = next++; i < entries.size(); i = next++)
            scoreEntry(entries[i], refDir, candDir, compositeDir, options);
    };
    std::vector<std::future<void>> pool;
    const unsigned threads = std::clamp(std::thread::hardware_concurrency() / 2, 1u, 8u);
    for (unsigned i = 0; i < threads; ++i)
        pool.push_back(std::async(std::launch::async, worker));
    for (auto& f : pool)
        f.get();

    writeCsv(QDir(outDir).filePath(QStringLiteral("results.csv")), entries);
    writeSummary(QDir(outDir).filePath(QStringLiteral("summary.md")), entries, options);
    out << "scored " << entries.size() << " files -> " << QDir(outDir).filePath(QStringLiteral("summary.md")) << "\n";
    return 0;
}
