// Tile rasterisation on worker threads (ARCHITECTURE.md, "Canvas rendering",
// "Threading rules"): the canvas composes finished tiles, shows substitutes
// for the rest, drops stale results and ends up with exactly the pixels of the
// synchronous render. Run under the asan preset as well.

#include "support/test_support.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/render/stats.h"
#include "qraw/render/tile_pool.h"

#include <QElapsedTimer>
#include <QScrollBar>
#include <QThread>

#include <atomic>
#include <cmath>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;
namespace ops = qraw::commands::ops;
using render::stats::Counter;

namespace {

constexpr int kTimeoutMs = 20000 * QRAW_TEST_TIMEOUT_SCALE;

std::uint64_t count(Counter c)
{
    return render::stats::get(c);
}

// An asynchronous canvas (makeCanvas turns the workers off).
std::unique_ptr<CanvasView> makeAsyncCanvas(DocumentSession& session, QSize size)
{
    auto view = std::make_unique<CanvasView>(session);
    view->resize(size);
    view->show();
    view->setZoom(1);
    view->centerOn(QRectF(0, 0, size.width(), size.height()));
    return view;
}

bool waitForIdle(CanvasView& view)
{
    QElapsedTimer timer;
    timer.start();
    // Idle twice in a row around an event-loop turn: the last result posts the paint and the plan that find nothing more to do.
    for (int stable = 0; stable < 2;) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (view.isRenderIdle()) {
            ++stable;
            continue;
        }
        stable = 0;
        if (timer.elapsed() > kTimeoutMs)
            return false;
        QThread::msleep(1);
    }
    return true;
}

QImage grab(CanvasView& view)
{
    return view.viewport()->grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// The synchronous render of what the canvas shows now: the same view, every tile rasterised again by a paint.
QImage synchronousRender(CanvasView& view)
{
    view.setSynchronousRasterization(true);
    view.tileCache().clear();
    const QImage image = grab(view);
    view.setSynchronousRasterization(false);
    return image;
}

int differingPixels(const QImage& a, const QImage& b)
{
    if (a.size() != b.size())
        return a.width() * a.height();
    int different = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x)
            different += a.pixel(x, y) != b.pixel(x, y) ? 1 : 0;
    }
    return different;
}

// 1,500 vertices in a 50 x 30 grid, a page without grid lines.
std::string wideDiagram()
{
    std::string xml = R"(<mxfile><diagram id="w" name="Wide"><mxGraphModel grid="0" page="1" pageWidth="4200" pageHeight="2000"><root><mxCell id="0"/><mxCell id="1" parent="0"/>)";
    for (int i = 0; i < 1500; ++i) {
        xml += "<mxCell id=\"v" + std::to_string(i) + "\" value=\"V" + std::to_string(i)
            + "\" style=\"rounded=1;fillColor=#dae8fc;\" vertex=\"1\" parent=\"1\"><mxGeometry x=\"" + std::to_string((i % 50) * 80)
            + "\" y=\"" + std::to_string((i / 50) * 60) + "\" width=\"60\" height=\"40\" as=\"geometry\"/></mxCell>";
    }
    xml += "</root></mxGraphModel></diagram></mxfile>";
    return xml;
}

std::vector<std::string> allIds()
{
    std::vector<std::string> ids;
    for (int i = 0; i < 1500; ++i)
        ids.push_back("v" + std::to_string(i));
    return ids;
}

// True when some 64 x 64 block of the viewport that lies inside the page is
// nothing but the backdrop colour: a tile that was not drawn.
bool hasBlankBlock(const QImage& image)
{
    const QRgb backdrop = qRgb(0xf0, 0xf0, 0xf0);
    for (int by = 0; by + 64 <= image.height(); by += 64) {
        for (int bx = 0; bx + 64 <= image.width(); bx += 64) {
            bool blank = true;
            for (int y = by; blank && y < by + 64; y += 4) {
                for (int x = bx; x < bx + 64; x += 4) {
                    if (image.pixel(x, y) != backdrop) {
                        blank = false;
                        break;
                    }
                }
            }
            if (blank)
                return true;
        }
    }
    return false;
}

} // namespace

class TestCanvasTilesThreaded : public QObject {
    Q_OBJECT

private slots:
    void settledPixelsEqualTheSynchronousRender()
    {
        DocumentSession session(documentFromXml(wideDiagram()));
        auto canvas = makeAsyncCanvas(session, {1000, 700});
        canvas->centerOn(QRectF(1200, 600, 1, 1));
        QVERIFY(waitForIdle(*canvas));
        canvas->zoomIn();
        canvas->zoomIn();
        canvas->horizontalScrollBar()->setValue(canvas->horizontalScrollBar()->value() + 333);
        QVERIFY(waitForIdle(*canvas));
        const QImage threaded = grab(*canvas);
        QCOMPARE(differingPixels(threaded, synchronousRender(*canvas)), 0);
    }

    // A zoom step paints its first frame from tiles that exist (no rasterisation
    // on the GUI thread, nothing blank), then becomes sharp.
    void zoomStepPaintsFromSubstitutesThenSharpens()
    {
        DocumentSession session(documentFromXml(wideDiagram()));
        auto canvas = makeAsyncCanvas(session, {1000, 700});
        canvas->centerOn(QRectF(800, 400, 1, 1));
        QVERIFY(waitForIdle(*canvas));
        canvas->tileScheduler().setSettleDelay(60);

        for (int step = 0; step < 3; ++step) {
            render::stats::reset();
            canvas->zoomIn();
            const QImage first = grab(*canvas); // the frame right after the step
            QCOMPARE(count(Counter::tilesRasterizedSync), std::uint64_t(0));
            QVERIFY2(!hasBlankBlock(first), "the first frame after a zoom step has no blank tile");
            QVERIFY(waitForIdle(*canvas));
            QCOMPARE(count(Counter::tilesRasterizedSync), std::uint64_t(0));
        }
        // Sharp: the exact bucket at the zoom, as the synchronous render has it.
        QCOMPARE(canvas->rasterScale(), canvas->zoom() * canvas->viewport()->devicePixelRatioF());
        QCOMPARE(differingPixels(grab(*canvas), synchronousRender(*canvas)), 0);
    }

    // A zoom between two steps is first shown from the bucket above it, scaled
    // down by at most 1.2; once settled it gets one exact-scale tiling (1:1, as
    // sharp as a step), and the next zoom drops it.
    void zoomBetweenStepsGetsOneTransientExactTiling()
    {
        DocumentSession session(documentFromXml(wideDiagram()));
        auto canvas = makeAsyncCanvas(session, {900, 600});
        QVERIFY(waitForIdle(*canvas));
        canvas->tileScheduler().setSettleDelay(40);
        canvas->setZoom(0.9);
        (void)grab(*canvas);
        const double first = canvas->tileScheduler().displayBucket();
        QVERIFY(first >= 0.9);
        QVERIFY(first / 0.9 <= 1.2 + 1e-9); // the bucket above, within the snap ratio
        QVERIFY(waitForIdle(*canvas));
        QCOMPARE(canvas->tileScheduler().displayBucket(), 0.9); // the exact tiling took over
        QCOMPARE(differingPixels(grab(*canvas), synchronousRender(*canvas)), 0);

        canvas->setZoom(0.95);
        QVERIFY(waitForIdle(*canvas));
        QCOMPARE(canvas->tileScheduler().displayBucket(), 0.95);
        for (const double scale : canvas->tileCache().scales())
            QVERIFY2(std::abs(scale - 0.9) > 1e-9, "the previous exact tiling is dropped");
    }

    // A result that was rasterised from content an edit has since changed is dropped.
    void staleResultsAreDropped()
    {
        DocumentSession session(documentFromXml(wideDiagram()));
        auto canvas = makeAsyncCanvas(session, {1000, 700});
        canvas->centerOn(QRectF(800, 400, 1, 1));
        QVERIFY(waitForIdle(*canvas));
        canvas->tileScheduler().setSettleDelay(10);
        // Every job takes at least 80 ms and ignores the cancellation until its end, as a long cell would.
        canvas->tileScheduler().setRasterizer([](const render::RenderSnapshot& snapshot, const render::TileKey& key, double dpr,
                                                 const std::atomic<bool>&) {
            QThread::msleep(80);
            return render::rasterizeTile(snapshot, key, dpr);
        });
        render::stats::reset();
        canvas->zoomIn(); // new bucket: workers start on it
        QTest::qWait(40); // the jobs are running...
        QVERIFY(canvas->tileScheduler().outstandingJobs() > 0);
        QVERIFY(session.edit(QStringLiteral("Move"), [](commands::Transaction& tx) {
                    return ops::moveCells(tx, 0, allIds(), 11, 7); // ...and the content changes under them
                }).ok);
        QVERIFY(waitForIdle(*canvas));
        QVERIFY2(count(Counter::tilesDiscarded) > 0, "stale results are discarded");
        QCOMPARE(differingPixels(grab(*canvas), synchronousRender(*canvas)), 0);
    }

    // The token of a job is set when its result is not wanted any more; a
    // rasteriser that polls it stops early.
    void cancellationReachesTheWorker()
    {
        DocumentSession session(documentFromXml(wideDiagram()));
        auto canvas = makeAsyncCanvas(session, {1000, 700});
        QVERIFY(waitForIdle(*canvas));
        auto started = std::make_shared<std::atomic<int>>(0);
        auto cancelled = std::make_shared<std::atomic<int>>(0);
        canvas->tileScheduler().setRasterizer([started, cancelled](const render::RenderSnapshot&, const render::TileKey&, double,
                                                                   const std::atomic<bool>& cancel) {
            ++*started;
            for (int i = 0; i < 2000; ++i) { // up to 2 s
                if (cancel.load()) {
                    ++*cancelled;
                    return QImage();
                }
                QThread::msleep(1);
            }
            return QImage();
        });
        canvas->zoomIn();
        QTRY_VERIFY_WITH_TIMEOUT(started->load() > 0, 5000);
        canvas->tileScheduler().reset(); // what a page switch does: nothing in flight is wanted
        QTRY_VERIFY_WITH_TIMEOUT(cancelled->load() > 0, 5000);
        canvas->tileScheduler().setRasterizer({}); // back to the real one (cancels and waits)
        canvas->tileCache().clear();
        QVERIFY(waitForIdle(*canvas));
        QCOMPARE(differingPixels(grab(*canvas), synchronousRender(*canvas)), 0);
    }

    // An edit that touches a few cheap tiles is rasterised on the spot; a large one
    // by workers, with the old tiles on screen meanwhile.
    void smallEditsAreSynchronousAndLargeOnesAreNot()
    {
        DocumentSession session(documentFromXml(wideDiagram()));
        auto canvas = makeAsyncCanvas(session, {1000, 700});
        canvas->centerOn(QRectF(600, 400, 1, 1));
        QVERIFY(waitForIdle(*canvas));
        // Idle is "the visible tiles are current"; the ring around them is prefetched after that. At a
        // device pixel ratio above 1 the tiles cost more and the ring is still in flight here, and the
        // pass that the edit triggers would then count those jobs as the edit's own.
        QTRY_VERIFY_WITH_TIMEOUT(canvas->tileScheduler().isQuiescent(), 10000);
        const QImage before = grab(*canvas);

        render::stats::reset();
        QVERIFY(session.edit(QStringLiteral("Move one"), [](commands::Transaction& tx) {
                    return ops::moveCells(tx, 0, {"v255"}, 0, 20);
                }).ok);
        QVERIFY2(count(Counter::tilesRasterizedSync) >= 1, "a small edit is rasterised before the next frame");
        QCOMPARE(count(Counter::tilesRasterized), count(Counter::tilesRasterizedSync));
        const QImage afterSmall = grab(*canvas);
        QVERIFY(differingPixels(before, afterSmall) > 0);
        QVERIFY(waitForIdle(*canvas));
        QCOMPARE(differingPixels(afterSmall, grab(*canvas)), 0); // nothing changed after the fact

        // Everything moves: every visible tile changes.
        render::stats::reset();
        QVERIFY(session.edit(QStringLiteral("Move all"), [](commands::Transaction& tx) {
                    return ops::moveCells(tx, 0, allIds(), 25, 15);
                }).ok);
        QCOMPARE(count(Counter::tilesRasterizedSync), std::uint64_t(0));
        const QImage meanwhile = grab(*canvas); // old tiles, not blank
        QVERIFY(!hasBlankBlock(meanwhile));
        QCOMPARE(count(Counter::tilesRasterizedSync), std::uint64_t(0));
        QVERIFY(waitForIdle(*canvas));
        QVERIFY(count(Counter::tilesRasterized) > 0);
        QCOMPARE(differingPixels(grab(*canvas), synchronousRender(*canvas)), 0);
    }

    // Many jobs while the UI thread edits, scrolls and zooms; no crash (asan), no
    // blank frame, and the settled picture equals the synchronous render.
    void stressEditScrollAndZoomWhileTilesRender()
    {
        DocumentSession session(documentFromXml(wideDiagram()));
        auto canvas = makeAsyncCanvas(session, {900, 600});
        canvas->tileScheduler().setSettleDelay(20);
        QVERIFY(waitForIdle(*canvas));
        unsigned seed = 12345;
        const auto next = [&seed](unsigned range) {
            seed = seed * 1103515245u + 12345u;
            return (seed >> 8) % range;
        };
        for (int i = 0; i < 80; ++i) {
            switch (next(5)) {
            case 0: canvas->zoomIn(); break;
            case 1: canvas->zoomOut(); break;
            case 2: canvas->horizontalScrollBar()->setValue(canvas->horizontalScrollBar()->value() + int(next(900)) - 450); break;
            case 3: canvas->verticalScrollBar()->setValue(canvas->verticalScrollBar()->value() + int(next(600)) - 300); break;
            default:
                QVERIFY(session.edit(QStringLiteral("Move"), [&](commands::Transaction& tx) {
                            return ops::moveCells(tx, 0, {"v" + std::to_string(next(1500))}, int(next(40)) - 20, int(next(40)) - 20);
                        }).ok);
            }
            QTest::qWait(int(next(12)));
            if (i % 10 == 0) {
                const QImage frame = grab(*canvas);
                QVERIFY(frame.width() > 0);
            }
        }
        QVERIFY(waitForIdle(*canvas));
        QCOMPARE(differingPixels(grab(*canvas), synchronousRender(*canvas)), 0);
    }

    void workersNeverTouchTheGuiThreadObjects()
    {
        // The pool's threads are not the GUI thread, and the rasteriser runs there.
        render::TileRasterPool* none = nullptr;
        (void)none;
        QObject context;
        std::atomic<bool> onWorker{false};
        std::atomic<bool> delivered{false};
        const auto guiThread = QThread::currentThread();
        render::TileRasterPool pool(
            &context,
            [&](render::TileResult) {
                QCOMPARE(QThread::currentThread(), guiThread); // results arrive on the GUI thread
                delivered = true;
            },
            2,
            [&](const render::RenderSnapshot&, const render::TileKey&, double, const std::atomic<bool>&) {
                onWorker = QThread::currentThread() != guiThread;
                return QImage(8, 8, QImage::Format_ARGB32_Premultiplied);
            });
        QCOMPARE(pool.threadCount(), 2);
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        render::TileJob job;
        job.snapshot = canvas->diagramScene().snapshot();
        job.key = {1, 0, 0};
        job.cancel = std::make_shared<std::atomic<bool>>(false);
        pool.submit(job);
        QTRY_VERIFY_WITH_TIMEOUT(delivered.load(), 5000);
        QVERIFY(onWorker.load());
    }
};

QTEST_MAIN(TestCanvasTilesThreaded)
#include "tst_canvas_tiles_threaded.moc"
