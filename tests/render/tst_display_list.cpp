// The render IR (display_list.h) and the snapshot built from it
// (render_snapshot.h): replaying a recording paints exactly what painting
// directly paints, the recorded bounds contain every painted pixel, and an
// edit re-records only the cells that changed.

#include "qraw/format/codec.h"
#include "qraw/render/display_list.h"
#include "qraw/render/page_view.h"
#include "qraw/render/painter_canvas.h"
#include "qraw/render/render_snapshot.h"
#include "qraw/render/scene_renderer.h"
#include "qraw/render/stats.h"
#include "qraw/render/svg_canvas.h"
#include "qraw/render/tile_rasterizer.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QElapsedTimer>
#include <QPainter>
#include <QTest>

#include <cmath>

using namespace qraw;
using namespace qraw::render;

namespace {

core::Document load(const QString& name)
{
    QFile file(QStringLiteral(QRAW_TEST_DATA_DIR "/golden/") + name + QStringLiteral(".drawio"));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("fixture is missing: %s", qPrintable(name));
    auto read = format::readDocument(file.readAll());
    if (!read.ok())
        qFatal("fixture does not parse: %s", qPrintable(name));
    return std::move(*read.document);
}

QStringList fixtureNames()
{
    QDir dir(QStringLiteral(QRAW_TEST_DATA_DIR "/golden"));
    QStringList names;
    for (const QString& f : dir.entryList({QStringLiteral("*.drawio")}, QDir::Files, QDir::Name))
        names << f.chopped(7);
    return names;
}

struct Page {
    core::Document document;
    RenderContext context = RenderContext::defaults();
    PageView view;
    std::shared_ptr<const RenderSnapshot> snapshot;
    std::unique_ptr<SceneRenderer> renderer;

    explicit Page(const QString& name) : document(load(name))
    {
        view = PageView::build(*document.page(0), context);
        renderer = std::make_unique<SceneRenderer>(context);
        SnapshotInput input;
        input.view = &view;
        input.renderer = renderer.get();
        snapshot = buildSnapshot(input);
    }
    [[nodiscard]] QRectF area() const
    {
        const Rect b = view.bounds();
        return QRectF(b.x, b.y, b.width, b.height).adjusted(-20, -20, 20, 20);
    }
};

QImage blank(const QSize& size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

} // namespace

class TestDisplayList : public QObject {
    Q_OBJECT

private slots:
    void replayPaintsWhatDirectPaintingPaints_data()
    {
        QTest::addColumn<QString>("fixture");
        QTest::addColumn<bool>("interactive");
        for (const QString& name : fixtureNames()) {
            QTest::newRow(qPrintable(name + "-exact")) << name << false;
            QTest::newRow(qPrintable(name + "-interactive")) << name << true;
        }
    }

    // Every fixture cell (shapes, HTML and plain labels, sketch, stencils,
    // tables, images, curved text) through a recording and back, onto a
    // PainterCanvas, equals painting it directly: the same QImage bytes.
    void replayPaintsWhatDirectPaintingPaints()
    {
        QFETCH(QString, fixture);
        QFETCH(bool, interactive);
        const Page page(fixture);
        const QRectF area = page.area();
        const QSize size = area.size().toSize() + QSize(1, 1);
        const ImageQuality quality = interactive ? ImageQuality::Interactive : ImageQuality::Exact;

        QImage direct = blank(size);
        {
            QPainter painter(&direct);
            painter.translate(-area.topLeft());
            for (const CellState& state : page.view.states()) {
                PainterCanvas canvas(painter);
                canvas.setImageQuality(quality);
                page.renderer->paintShape(canvas, state, &page.view, false);
                page.renderer->paintLabel(canvas, state);
            }
        }
        QImage replayed = blank(size);
        {
            QPainter painter(&replayed);
            painter.translate(-area.topLeft());
            for (const auto& record : page.snapshot->records())
                replayRecord(painter, *record, quality);
        }
        QCOMPARE(page.snapshot->records().size(), page.view.states().size());
        QVERIFY2(direct == replayed, qPrintable(fixture + ": replay differs from direct painting"));
        // Something was painted at all.
        QVERIFY(direct != blank(size));
    }

    // The same calls reach an SvgCanvas: identical SVG.
    void replayOnSvgGivesTheSameSvg()
    {
        for (const QString& name : fixtureNames()) {
            const Page page(name);
            SvgDocumentOptions options;
            options.width = 2000;
            options.height = 2000;
            SvgCanvas direct(options);
            SvgCanvas replayed(options);
            for (const CellState& state : page.view.states()) {
                direct.beginGroup(state.id);
                page.renderer->paintShape(direct, state, &page.view, false);
                page.renderer->paintLabel(direct, state);
                direct.endGroup();
            }
            for (const auto& record : page.snapshot->records()) {
                replayed.beginGroup(record->id);
                record->display.replayShape(replayed);
                record->display.replayLabel(replayed);
                replayed.endGroup();
            }
            if (direct.toSvg() != replayed.toSvg()) {
                const std::string a = direct.toSvg();
                const std::string b = replayed.toSvg();
                std::size_t i = 0;
                while (i < a.size() && i < b.size() && a[i] == b[i])
                    ++i;
                qWarning("%s: first difference at %zu: [%s] vs [%s]", qPrintable(name), i,
                         a.substr(i > 80 ? i - 80 : 0, 200).c_str(), b.substr(i > 80 ? i - 80 : 0, 200).c_str());
            }
            QVERIFY2(direct.toSvg() == replayed.toSvg(), qPrintable(name + ": svg differs"));
        }
    }

    // Each cell alone on a large transparent image: no pixel outside its
    // visual bounds (plus the one device pixel of antialiasing slack the
    // rasteriser adds). This is the guard against labels, shadows and strokes
    // clipped at tile edges.
    void visualBoundsContainEveryPaintedPixel_data()
    {
        QTest::addColumn<QString>("fixture");
        for (const QString& name : fixtureNames())
            QTest::newRow(qPrintable(name)) << name;
    }
    void visualBoundsContainEveryPaintedPixel()
    {
        QFETCH(QString, fixture);
        const Page page(fixture);
        const QRectF area = page.area().adjusted(-300, -300, 300, 300);
        const QSize size = area.size().toSize() + QSize(1, 1);
        int checked = 0;
        for (std::size_t i = 0; i < page.view.states().size(); ++i) {
            const CellRecord& record = *page.snapshot->records()[i];
            const CellState& state = page.view.states()[i];
            QImage image = blank(size);
            {
                QPainter painter(&image);
                painter.translate(-area.topLeft());
                replayRecord(painter, record, ImageQuality::Exact);
            }
            int minX = image.width(), minY = image.height(), maxX = -1, maxY = -1;
            for (int y = 0; y < image.height(); ++y) {
                const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
                for (int x = 0; x < image.width(); ++x) {
                    if (qAlpha(row[x]) == 0)
                        continue;
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }
            if (maxX < 0)
                continue;
            ++checked;
            const QRectF painted(QPointF(minX, minY) + area.topLeft(), QPointF(maxX + 1, maxY + 1) + area.topLeft());
            const QRectF allowed = record.visualBounds.adjusted(-kAntialiasSlackPx, -kAntialiasSlackPx,
                                                                kAntialiasSlackPx, kAntialiasSlackPx);
            QVERIFY2(allowed.contains(painted),
                     qPrintable(QStringLiteral("%1 / %2: painted %3,%4 %5x%6 outside bounds %7,%8 %9x%10")
                                    .arg(fixture, QString::fromStdString(state.id))
                                    .arg(painted.x()).arg(painted.y()).arg(painted.width()).arg(painted.height())
                                    .arg(allowed.x()).arg(allowed.y()).arg(allowed.width()).arg(allowed.height())));
            // Never smaller than the estimate of the page view.
            const QRectF estimate(state.paintBounds.x, state.paintBounds.y, state.paintBounds.width,
                                  state.paintBounds.height);
            QVERIFY(record.visualBounds.contains(estimate));
        }
        QVERIFY(checked > 0);
    }

    void labelAndShapeAreRecordedSeparately()
    {
        const Page page(QStringLiteral("labels"));
        bool labelled = false;
        for (const auto& record : page.snapshot->records()) {
            const DisplayList& list = record->display;
            QVERIFY(list.labelBegin() <= list.opCount());
            labelled = labelled || list.labelBegin() < list.opCount();
            QVERIFY(list.bounds().contains(list.shapeBounds().isNull() ? list.bounds() : list.shapeBounds()));
        }
        QVERIFY(labelled);
    }

    void snapshotsShareTheRecordsOfUnchangedCells()
    {
        Page page(QStringLiteral("basic-shapes"));
        const auto& first = *page.snapshot;
        QVERIFY(first.records().size() > 3);
        const std::string changedId = first.records()[1]->id;

        stats::reset();
        std::unordered_set<std::string> changed{changedId};
        SnapshotInput input;
        input.view = &page.view;
        input.renderer = page.renderer.get();
        input.previous = &first;
        input.changed = &changed;
        const auto second = buildSnapshot(input);
        QCOMPARE(stats::get(stats::Counter::recordsBuilt), std::uint64_t(1));
        QVERIFY(second->generation() > first.generation());
        for (std::size_t i = 0; i < first.records().size(); ++i) {
            if (first.records()[i]->id == changedId)
                QVERIFY(second->records()[i] != first.records()[i]);
            else
                QVERIFY(second->records()[i] == first.records()[i]);
        }
        // Damage: the re-recorded cell only.
        const Damage damage = computeDamage(&first, *second);
        QVERIFY(!damage.everything);
        QCOMPARE(damage.rects.size(), std::size_t(2)); // old and new bounds of the changed cell

        // Hiding a label re-records that cell only.
        stats::reset();
        SnapshotInput hidden = input;
        hidden.changed = nullptr;
        hidden.hiddenLabel = changedId;
        const auto third = buildSnapshot(hidden);
        QCOMPARE(stats::get(stats::Counter::recordsBuilt), std::uint64_t(1));
        QVERIFY(third->find(changedId)->labelHidden);
    }

    void restackDamagesTheMovedCellsAndRecordsNothing()
    {
        const auto documentWithOrder = [](const std::vector<std::string>& ids) {
            std::string xml = "<mxfile><diagram id=\"p\" name=\"p\"><mxGraphModel><root><mxCell id=\"0\"/>"
                              "<mxCell id=\"1\" parent=\"0\"/>";
            for (const std::string& id : ids) {
                const int n = id[0] - 'a';
                xml += "<mxCell id=\"" + id + "\" value=\"" + id + "\" style=\"rounded=0\" vertex=\"1\" parent=\"1\">"
                       "<mxGeometry x=\"" + std::to_string(n * 50) + "\" y=\"0\" width=\"80\" height=\"40\" as=\"geometry\"/></mxCell>";
            }
            xml += "</root></mxGraphModel></diagram></mxfile>";
            auto read = format::readDocument(QByteArray::fromStdString(xml));
            return std::move(*read.document);
        };
        const auto context = RenderContext::defaults();
        const SceneRenderer renderer(context);
        const auto before = documentWithOrder({"a", "b", "c", "d"});
        const auto after = documentWithOrder({"c", "a", "b", "d"}); // c brought to the back
        const PageView viewBefore = PageView::build(*before.page(0), context);
        const PageView viewAfter = PageView::build(*after.page(0), context);
        SnapshotInput input;
        input.view = &viewBefore;
        input.renderer = &renderer;
        const auto first = buildSnapshot(input);

        stats::reset();
        input.view = &viewAfter;
        input.previous = first.get();
        const auto second = buildSnapshot(input);
        QCOMPARE(stats::get(stats::Counter::recordsBuilt), std::uint64_t(0)); // expose damage, no re-recording
        QCOMPARE(second->indexOf("c"), std::size_t(0));

        const Damage damage = computeDamage(first.get(), *second);
        QVERIFY(!damage.everything);
        QCOMPARE(damage.rects.size(), std::size_t(1));
        QCOMPARE(damage.rects.front(), second->find("c")->visualBounds); // only c moved relative to the rest

        // Nothing changed: no damage.
        input.view = &viewBefore;
        input.previous = first.get();
        QVERIFY(computeDamage(first.get(), *buildSnapshot(input)).empty());
        // A different page decoration damages everything.
        input.decor.gridEnabled = true;
        QVERIFY(computeDamage(first.get(), *buildSnapshot(input)).everything);
    }

    // Not an assertion (wall time): what recording costs against painting
    // directly and against replaying, on 4,000 labelled rounded rectangles and
    // 500 edges (see docs/BENCHMARKS.md).
    void recordingAndReplayTimes()
    {
        std::string xml = R"(<mxfile><diagram id="t" name="t"><mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/>)";
        for (int i = 0; i < 4000; ++i) {
            xml += "<mxCell id=\"v" + std::to_string(i) + "\" value=\"Cell " + std::to_string(i)
                + "\" style=\"rounded=1;whiteSpace=wrap;html=1;\" vertex=\"1\" parent=\"1\"><mxGeometry x=\""
                + std::to_string((i % 80) * 90) + "\" y=\"" + std::to_string((i / 80) * 60)
                + "\" width=\"80\" height=\"40\" as=\"geometry\"/></mxCell>";
        }
        for (int i = 0; i < 500; ++i) {
            xml += "<mxCell id=\"e" + std::to_string(i) + "\" edge=\"1\" parent=\"1\" source=\"v" + std::to_string(i)
                + "\" target=\"v" + std::to_string(i + 1) + "\"><mxGeometry relative=\"1\" as=\"geometry\"/></mxCell>";
        }
        xml += "</root></mxGraphModel></diagram></mxfile>";
        auto read = format::readDocument(QByteArray::fromStdString(xml));
        QVERIFY(read.ok());
        const auto context = RenderContext::defaults();
        const PageView view = PageView::build(*read.document->page(0), context);
        const SceneRenderer renderer(context);
        const QRectF area(0, 0, 7300, 3100);
        QImage image(QSize(1830, 780), QImage::Format_ARGB32_Premultiplied); // a quarter scale: the cost per cell is what counts
        const auto timed = [](const auto& fn) {
            QElapsedTimer t;
            t.start();
            fn();
            return t.nsecsElapsed() / 1e6;
        };
        const auto direct = [&] {
            image.fill(Qt::white);
            QPainter painter(&image);
            painter.scale(0.25, 0.25);
            for (const CellState& state : view.states()) {
                PainterCanvas canvas(painter);
                renderer.paintShape(canvas, state, &view, false);
                renderer.paintLabel(canvas, state);
            }
        };
        direct(); // warms the label layouts
        const double directMs = timed(direct);
        std::shared_ptr<const RenderSnapshot> snapshot;
        SnapshotInput input;
        input.view = &view;
        input.renderer = &renderer;
        const double recordMs = timed([&] { snapshot = buildSnapshot(input); });
        const auto replay = [&] {
            image.fill(Qt::white);
            QPainter painter(&image);
            painter.scale(0.25, 0.25);
            for (const auto& record : snapshot->records())
                replayRecord(painter, *record, ImageQuality::Interactive);
        };
        replay();
        const double replayMs = timed(replay);
        std::size_t bytes = 0;
        for (const auto& record : snapshot->records())
            bytes += record->display.memoryBytes();
        qInfo("%zu cells: direct paint %.1f ms, record %.1f ms (%.1f us per cell, once), replay %.1f ms; %.1f MB of ops (%.0f bytes per cell)",
              view.states().size(), directMs, recordMs, recordMs * 1000 / double(view.states().size()), replayMs,
              double(bytes) / 1e6, double(bytes) / double(view.states().size()));
        Q_UNUSED(area);
    }

    void spatialGridQueriesComeBackInPaintOrder()
    {
        const Page page(QStringLiteral("basic-shapes"));
        const QRectF all = page.snapshot->contentBounds().adjusted(-5, -5, 5, 5);
        const auto hits = page.snapshot->query(all);
        QVERIFY(!hits.empty());
        QVERIFY(std::is_sorted(hits.begin(), hits.end()));
        std::size_t visible = 0;
        for (const auto& record : page.snapshot->records())
            visible += record->visualBounds.isNull() ? 0 : 1;
        QCOMPARE(hits.size(), visible);
        // A far-away rectangle finds nothing.
        QVERIFY(page.snapshot->query(QRectF(1e6, 1e6, 10, 10)).empty());
    }
};

QTEST_MAIN(TestDisplayList)
#include "tst_display_list.moc"
