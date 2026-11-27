// SpatialGrid: queries equal a brute-force scan, in ascending order, for
// small and huge rectangles, hairlines, empty-sized items and bad input.

#include "qraw/render/spatial_grid.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace qraw;
using render::SpatialGrid;

namespace {

// A reproducible pseudo-random sequence in [0, 1).
class Random {
public:
    explicit Random(std::uint64_t seed) : state_(seed) {}
    double next()
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return static_cast<double>(state_ % 1000003) / 1000003.0;
    }

private:
    std::uint64_t state_;
};

bool meets(const QRectF& a, const QRectF& b)
{
    return a.left() <= b.right() && a.right() >= b.left() && a.top() <= b.bottom() && a.bottom() >= b.top();
}

std::vector<std::uint32_t> bruteForce(const std::vector<QRectF>& items, const QRectF& rect)
{
    std::vector<std::uint32_t> result;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (!items[i].isNull() && meets(items[i], rect))
            result.push_back(static_cast<std::uint32_t>(i));
    }
    return result;
}

std::vector<std::uint32_t> query(const SpatialGrid& grid, const QRectF& rect)
{
    std::vector<std::uint32_t> result;
    grid.query(rect, result);
    return result;
}

} // namespace

class SpatialGridTest : public QObject {
    Q_OBJECT

private slots:
    void emptyGridFindsNothing()
    {
        SpatialGrid grid;
        QVERIFY(query(grid, QRectF(0, 0, 100, 100)).empty());
        grid.build({});
        QVERIFY(query(grid, QRectF(-1e9, -1e9, 2e9, 2e9)).empty());
    }

    void randomRectanglesMatchBruteForce()
    {
        Random random(7);
        std::vector<QRectF> items;
        for (int i = 0; i < 3000; ++i) {
            const double x = random.next() * 20000 - 5000;
            const double y = random.next() * 20000 - 5000;
            // Mostly small, some long edges, a few that span the page.
            const double kind = random.next();
            const double w = kind < 0.8 ? random.next() * 150 : kind < 0.97 ? random.next() * 3000 : random.next() * 30000;
            const double h = kind < 0.8 ? random.next() * 150 : kind < 0.97 ? random.next() * 40 : random.next() * 30000;
            items.emplace_back(x, y, w, h);
        }
        SpatialGrid grid;
        grid.build(items);
        QCOMPARE(grid.size(), items.size());
        for (int q = 0; q < 400; ++q) {
            const double w = q % 7 == 0 ? random.next() * 30000 : random.next() * 600;
            const double h = q % 7 == 0 ? random.next() * 30000 : random.next() * 600;
            const QRectF rect(random.next() * 20000 - 5000, random.next() * 20000 - 5000, w, h);
            const auto found = query(grid, rect);
            QVERIFY2(found == bruteForce(items, rect), qPrintable(QStringLiteral("query %1").arg(q)));
            QVERIFY(std::is_sorted(found.begin(), found.end()));
        }
    }

    void hairlinesAndZeroSizeItemsAreFound()
    {
        // A horizontal and a vertical edge (no height, no width) and a point-sized item.
        const std::vector<QRectF> items{QRectF(10, 50, 200, 0), QRectF(300, 10, 0, 200), QRectF(500, 500, 0, 0),
                                        QRectF(1000, 1000, 10, 10)};
        SpatialGrid grid;
        grid.build(items);
        QCOMPARE(query(grid, QRectF(100, 40, 10, 20)), (std::vector<std::uint32_t>{0}));
        QCOMPARE(query(grid, QRectF(290, 100, 20, 0)), (std::vector<std::uint32_t>{1}));
        // The point-sized item is a null rectangle: it is not indexed (callers add a margin).
        QVERIFY(query(grid, QRectF(499, 499, 2, 2)).empty());
        // A query without size, exactly on a hairline.
        QCOMPARE(query(grid, QRectF(50, 50, 0, 0)), (std::vector<std::uint32_t>{0}));
        // Touching edges count.
        QCOMPARE(query(grid, QRectF(210, 50, 10, 10)), (std::vector<std::uint32_t>{0}));
    }

    void resultsAreInPaintOrderWithoutDuplicates()
    {
        // One big item (many grid cells) between small ones: ascending, each once.
        std::vector<QRectF> items;
        for (int i = 0; i < 50; ++i)
            items.emplace_back(i * 30.0, 0, 20, 20);
        items.insert(items.begin() + 10, QRectF(-5000, -5000, 20000, 20000));
        SpatialGrid grid;
        grid.build(items);
        const auto found = query(grid, QRectF(-1e6, -1e6, 2e6, 2e6));
        QCOMPARE(found.size(), items.size());
        QVERIFY(std::is_sorted(found.begin(), found.end()));
        QVERIFY(std::adjacent_find(found.begin(), found.end()) == found.end());
    }

    void hugeAndBrokenBoundsAreHandled()
    {
        const double inf = std::numeric_limits<double>::infinity();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const std::vector<QRectF> items{QRectF(0, 0, 50, 50), QRectF(-1e30, -1e30, 2e30, 2e30), QRectF(nan, 0, 10, 10),
                                        QRectF(0, 0, inf, 10), QRectF(100, 100, 50, 50)};
        SpatialGrid grid;
        grid.build(items);
        // The huge one is clamped and found everywhere; the broken ones are never found.
        const auto found = query(grid, QRectF(10, 10, 5, 5));
        QCOMPARE(found, (std::vector<std::uint32_t>{0, 1}));
        QVERIFY(query(grid, QRectF(nan, 0, 10, 10)).empty());
        QVERIFY(query(grid, QRectF(0, 0, inf, 10)).empty());
        QCOMPARE(query(grid, QRectF(-1e30, -1e30, 2e30, 2e30)).size(), std::size_t(3));
    }

    void rebuildReplacesTheContent()
    {
        SpatialGrid grid;
        grid.build({QRectF(0, 0, 10, 10)});
        QCOMPARE(query(grid, QRectF(0, 0, 5, 5)).size(), std::size_t(1));
        grid.build({QRectF(1000, 1000, 10, 10)});
        QVERIFY(query(grid, QRectF(0, 0, 5, 5)).empty());
        QCOMPARE(query(grid, QRectF(1000, 1000, 5, 5)).size(), std::size_t(1));
    }
};

QTEST_GUILESS_MAIN(SpatialGridTest)
#include "tst_spatial_grid.moc"
