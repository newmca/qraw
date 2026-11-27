// Zoom buckets and the hysteresis of the displayed bucket (zoom_buckets.h).

#include "qraw/render/zoom_buckets.h"

#include <QTest>

#include <algorithm>
#include <cmath>

using namespace qraw::render;

namespace {

// The view's steps (Graph.getZoomSteps, factor 1.2) near 100 %, and the coarse low end.
const std::vector<double> kSteps = {0.05, 0.1, 0.15, 0.2, 0.25, 0.3, 0.4, 0.5, 0.6, 0.7, 0.85, 1,
                                    1.2,  1.44, 1.7,  2.0, 2.4, 2.9, 3.5, 4.2, 5,   6,   7.2, 9, 160};

} // namespace

class ZoomBucketsTest : public QObject {
    Q_OBJECT

private slots:
    void everyStepIsABucketAndNeighboursAreWithinTheSnapRatio()
    {
        const ZoomBuckets buckets(kSteps, 0.01);
        const std::vector<double>& ladder = buckets.ladder();
        QVERIFY(std::is_sorted(ladder.begin(), ladder.end()));
        for (const double step : kSteps)
            QVERIFY2(std::find(ladder.begin(), ladder.end(), step) != ladder.end(), qPrintable(QString::number(step)));
        QCOMPARE(ladder.front(), 0.01);
        QCOMPARE(ladder.back(), 160.0);
        // 9 -> 160 is the view's own jump (draw.io's last steps are far apart); below it every gap is within 1.2.
        for (std::size_t i = 1; i < ladder.size(); ++i) {
            if (ladder[i - 1] >= 9)
                continue;
            QVERIFY2(ladder[i] / ladder[i - 1] <= ZoomBuckets::kMaxRatio + 1e-9,
                     qPrintable(QStringLiteral("%1 -> %2").arg(ladder[i - 1]).arg(ladder[i])));
        }
    }

    void ceilingIsTheBucketAtOrAboveTheZoom()
    {
        const ZoomBuckets buckets(kSteps, 0.01);
        QCOMPARE(buckets.ceiling(1.0), 1.0);
        QCOMPARE(buckets.ceiling(1.0 - 1e-12), 1.0); // a zoom a hair below a step is that step
        QCOMPARE(buckets.ceiling(1.1), 1.2);
        QCOMPARE(buckets.ceiling(1.2000001), 1.44);
        QCOMPARE(buckets.ceiling(500), 160.0);
        QCOMPARE(buckets.ceiling(0.001), 0.01);
        for (double zoom = 0.012; zoom < 8; zoom *= 1.037) {
            const double bucket = buckets.ceiling(zoom);
            QVERIFY(bucket >= zoom * (1 - 1e-9));
            QVERIFY2(bucket / zoom <= ZoomBuckets::kMaxRatio + 1e-9, qPrintable(QString::number(zoom)));
        }
    }

    void aboveAndBelowWalkTheLadder()
    {
        const ZoomBuckets buckets(kSteps, 0.01);
        QCOMPARE(*buckets.above(1.0), 1.2);
        QCOMPARE(*buckets.below(1.2), 1.0);
        QVERIFY(!buckets.above(160));
        QVERIFY(!buckets.below(0.01));
    }

    void hysteresisKeepsTheTilingOnScreenDuringAGesture()
    {
        const ZoomBuckets buckets(kSteps, 0.01);
        // Nothing on screen yet: the target.
        QCOMPARE(chooseDisplayBucket(buckets, 1.1, std::nullopt, false, false), 1.2);
        // A gesture at 1.0 -> 1.44: the tiling at 1.0 stays (scaled 1.44x, within 2x).
        QCOMPARE(chooseDisplayBucket(buckets, 1.44, 1.0, false, false), 1.0);
        // Beyond 2x (either way) the target takes over, rasterisation starts.
        QCOMPARE(chooseDisplayBucket(buckets, 2.4, 1.0, false, false), 2.4);
        QCOMPARE(chooseDisplayBucket(buckets, 0.4, 1.0, false, false), 0.4);
        // Settled, or the target's tiles are all current: the target at once.
        QCOMPARE(chooseDisplayBucket(buckets, 1.44, 1.0, true, false), 1.44);
        QCOMPARE(chooseDisplayBucket(buckets, 1.44, 1.0, false, true), 1.44);
        // Between two steps, settled: the bucket above, drawn downscaled by at most 1.2...
        QCOMPARE(chooseDisplayBucket(buckets, 1.3, 1.0, true, false), 1.44);
        // ...until the tiling at exactly the zoom is ready; then it stays while the zoom does.
        QCOMPARE(chooseDisplayBucket(buckets, 1.3, 1.44, true, false, true), 1.3);
        QCOMPARE(chooseDisplayBucket(buckets, 1.3, 1.3, true, false, false), 1.3);
        QVERIFY(isBucket(buckets, 1.2));
        QVERIFY(!isBucket(buckets, 1.3));
    }
};

QTEST_APPLESS_MAIN(ZoomBucketsTest)
#include "tst_zoom_buckets.moc"
