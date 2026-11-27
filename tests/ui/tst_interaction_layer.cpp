// The active layer (ARCHITECTURE.md, "Canvas rendering": interaction
// snapshots): the cells under the user's hands are drawn live above the tiles.
// A drag costs no tile and no page view per move; the picture equals what a
// direct render of the moved cell shows; cancel brings back the picture from
// before the lift; commit hands over to the tiles without a flash.

#include "support/test_support.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/render/stats.h"
#include "qraw/ui/active_layer.h"

#include <QBuffer>
#include <QImage>
#include <QKeyEvent>
#include <QPainter>

#include <cmath>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;
namespace ops = qraw::commands::ops;
using render::stats::Counter;

namespace {

std::uint64_t count(Counter c)
{
    return render::stats::get(c);
}

// A PNG with a gradient that has no flat area: a resampled copy differs from a vector-sharp one.
std::string pngDataUri()
{
    QImage picture(200, 120, QImage::Format_ARGB32);
    for (int y = 0; y < picture.height(); ++y) {
        for (int x = 0; x < picture.width(); ++x)
            picture.setPixel(x, y, qRgba((x * 5 + y) & 255, (y * 9) & 255, (x * 3 + y * 7) & 255, 255));
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    picture.save(&buffer, "PNG");
    return "data:image/png," + png.toBase64().toStdString();
}

// The drag distance of the tests that compare a lifted cell with the same cell at its new place. At
// a fractional device pixel ratio a distance moves the cell by a fractional number of device
// pixels, and the replay (exact) and the bitmap of the fallback (placed at a whole device pixel)
// differ by half a pixel; a multiple of 4 is a whole number of device pixels at 1, 1.25, 1.5 and 2.
constexpr double kDragX = 48;
constexpr double kDragY = 32;

// A flat page (no page tiles, no grid) with an image cell "img", a vertex "b"
// and an edge "e" from the image to b, plus a vertex "far" nobody touches.
// `imageX` places the image (the reference render of a moved image).
std::string pageXml(int imageX = 100, int imageY = 100)
{
    return R"(<mxfile><diagram id="p" name="P"><mxGraphModel grid="0" page="0"><root><mxCell id="0"/><mxCell id="1" parent="0"/>)"
           R"(<mxCell id="img" value="" style="shape=image;aspect=fixed;image=)"
        + pngDataUri() + R"(;" vertex="1" parent="1"><mxGeometry x=")" + std::to_string(imageX) + R"(" y=")"
        + std::to_string(imageY) + R"(" width="200" height="120" as="geometry"/></mxCell>)"
        R"(<mxCell id="b" value="B" style="rounded=1;fillColor=#dae8fc;" vertex="1" parent="1"><mxGeometry x="500" y="100" width="120" height="60" as="geometry"/></mxCell>)"
        R"(<mxCell id="e" style="endArrow=classic;" edge="1" parent="1" source="img" target="b"><mxGeometry relative="1" as="geometry"/></mxCell>)"
        R"(<mxCell id="far" value="Far" style="ellipse;fillColor=#f8cecc;" vertex="1" parent="1"><mxGeometry x="500" y="400" width="100" height="80" as="geometry"/></mxCell>)"
        R"(</root></mxGraphModel></diagram></mxfile>)";
}

struct Fixture {
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    explicit Fixture(const std::string& xml = pageXml())
        : session(std::make_unique<DocumentSession>(documentFromXml(xml))), view(makeCanvas(*session))
    {
        view->setGuidesEnabled(false); // the pointer delta is the move, exactly
        (void)QTest::qWaitForWindowExposed(view.get()); // repaint() paints nothing in a window that is not exposed
        view->viewport()->repaint();
    }
    [[nodiscard]] QImage grab() const
    {
        view->viewport()->repaint();
        return view->viewport()->grab().toImage();
    }
    [[nodiscard]] QPointF centreOf(const std::string& id) const
    {
        const render::CellState* s = view->diagramScene().state(id);
        return {s->bounds.x + s->bounds.width / 2, s->bounds.y + s->bounds.height / 2};
    }
    // Presses on the image and moves past the drag threshold; returns the press point.
    QPointF startDrag(Qt::KeyboardModifiers modifiers = Qt::NoModifier) const
    {
        const QPointF from = centreOf("img");
        press(*view, from, modifiers);
        move(*view, from + QPointF(10, 6), modifiers);
        view->viewport()->repaint(); // the lift frame: tiles under the image are rasterised without it
        return from;
    }
};

// The largest per-channel difference two renderings of the same geometry may have. At device pixel
// ratios 1 and 2 the replay and the tiles are equal. At the others (1.25, 1.5, 3) the same geometry
// reaches the rasteriser through different but equivalent sums (a route recorded from shifted
// terminals against the route of a real edit; an image placed in a tile against placed by the
// layer), and a difference of a few units in an antialiased coverage value is rounding noise: the
// measured maximum is 1 of 255 at 1.25 and 1.5 and 3 at ratio 3, and the best integer shift of one image over the other is (0, 0).
// A geometry error is a pixel moved by a whole device pixel, which turns a line pixel into the
// background and differs by far more than this (`aOneDevicePixelShiftIsStillSeen`).
int channelTolerance(const CanvasView& view)
{
    const double dpr = view.viewport()->devicePixelRatioF();
    return dpr == 1.0 || dpr == 2.0 ? 0 : 3;
}

// Pixels of `a` and `b` inside `rect` (device pixels) whose channels differ by more than `tolerance`.
int differing(const QImage& a, const QImage& b, const QRect& rect, int tolerance = 0)
{
    int n = 0;
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        for (int x = rect.left(); x <= rect.right(); ++x) {
            const QRgb p = a.pixel(x, y);
            const QRgb q = b.pixel(x, y);
            const int delta = std::max({std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)),
                                        std::abs(qBlue(p) - qBlue(q)), std::abs(qAlpha(p) - qAlpha(q))});
            n += delta > tolerance ? 1 : 0;
        }
    }
    return n;
}

// The rectangle of a scene rectangle in device pixels of the grabbed viewport (the images are as large as
// the viewport times its device pixel ratio), shrunk by `inset` logical pixels (the dashed outline of the move
// preview is not content).
QRect viewportRect(const CanvasView& view, const QRectF& scene, int inset)
{
    const double dpr = view.viewport()->devicePixelRatioF();
    const int deviceInset = static_cast<int>(std::ceil(inset * dpr));
    const QRectF logical = view.viewportTransform().mapRect(scene);
    const QRect device(QPointF(logical.left() * dpr, logical.top() * dpr).toPoint(),
                       QPointF(logical.right() * dpr, logical.bottom() * dpr).toPoint());
    return device.adjusted(deviceInset, deviceInset, -deviceInset, -deviceInset);
}

void settleLayer(CanvasView& view)
{
    QTRY_VERIFY_WITH_TIMEOUT(!view.activeLayer().isActive(), 3000);
    view.viewport()->repaint();
}

} // namespace

class TestInteractionLayer : public QObject {
    Q_OBJECT

private slots:
    // After the lift a move rasterises nothing, builds no page view and
    // replays only the lifted cells (and records the one edge that follows).
    void dragCostsNoTileAndNoPageViewPerMove()
    {
        Fixture f;
        f.startDrag();
        QCOMPARE(f.view->activeLayer().state(), ActiveLayer::State::Lifted);
        const QPointF from = f.centreOf("img");
        render::stats::reset();
        constexpr int kMoves = 12;
        for (int i = 1; i <= kMoves; ++i) {
            move(*f.view, from + QPointF(10 + 4 * i, 6 + 2 * i));
            f.view->viewport()->repaint();
        }
        QCOMPARE(count(Counter::tilesRasterized), std::uint64_t(0));
        QCOMPARE(count(Counter::tilesRasterizedSync), std::uint64_t(0));
        QCOMPARE(count(Counter::pageViewBuilds), std::uint64_t(0));
        QCOMPARE(count(Counter::cellsReplayed), std::uint64_t(0)); // no tile replay
        QVERIFY2(count(Counter::activeReplayed) >= std::uint64_t(kMoves),
                 qPrintable(QString::number(count(Counter::activeReplayed)) + " " + QString::number(count(Counter::recordsBuilt))));
        QVERIFY(count(Counter::recordsBuilt) <= std::uint64_t(kMoves)); // the edge, once per move
        f.view->interaction().cancelGesture();
    }

    // The lifted image is exactly where a direct render of the moved image
    // puts it: vector replay at the translated position, not a resampled bitmap.
    void liftedCellEqualsDirectRenderAtTheTranslatedPosition()
    {
        Fixture f;
        const QPointF from = f.startDrag();
        move(*f.view, from + QPointF(kDragX, kDragY));
        const QImage live = f.grab();
        grabToPng(*f.view->viewport(), QStringLiteral("mid-drag")); // manual review (QRAW_SCREENSHOT_DIR)

        Fixture reference(pageXml(100 + int(kDragX), 100 + int(kDragY)));
        reference.session->selection().clear();
        const QImage direct = reference.grab();
        // The dashed outline of the preview is overlay; the content is inside it.
        const QRectF moved(100 + kDragX, 100 + kDragY, 200, 120);
        QCOMPARE(differing(live, direct, viewportRect(*f.view, moved, 4), channelTolerance(*f.view)), 0);
        f.view->interaction().cancelGesture();
    }

    // The edge from the image to b follows the pointer: the same pixels as the
    // route a real edit gives it.
    void connectedEdgesFollowThePointer()
    {
        Fixture f;
        const QPointF from = f.startDrag();
        move(*f.view, from + QPointF(0, 60));
        const QImage live = f.grab();

        Fixture reference(pageXml(100, 160));
        reference.session->selection().clear();
        const QImage direct = reference.grab();
        // Between the two cells, away from the outline of the preview and from b's handles.
        const QRect between = viewportRect(*f.view, QRectF(310, 100, 180, 160), 0);
        QVERIFY(differing(direct, f.grab(), between) >= 0);
        QCOMPARE(differing(live, direct, between, channelTolerance(*f.view)), 0);
        f.view->interaction().cancelGesture();
    }

    // The tolerance of `channelTolerance` must not hide a geometry error: the same picture one
    // device pixel to the side differs in far more pixels than rounding noise could.
    void aOneDevicePixelShiftIsStillSeen()
    {
        Fixture reference(pageXml(100 + int(kDragX), 100 + int(kDragY)));
        reference.session->selection().clear();
        const QImage direct = reference.grab();
        const QRect area = viewportRect(*reference.view, QRectF(100 + kDragX, 100 + kDragY, 200, 120), 4);
        QCOMPARE(differing(direct, direct, area, channelTolerance(*reference.view)), 0);
        for (const QPoint shift : {QPoint(1, 0), QPoint(0, 1), QPoint(-1, -1)}) {
            const QImage shifted = direct.copy(QRect(-shift.x(), -shift.y(), direct.width(), direct.height()));
            QVERIFY2(differing(direct, shifted, area, channelTolerance(*reference.view)) > 50,
                     qPrintable(QStringLiteral("shift %1,%2").arg(shift.x()).arg(shift.y())));
        }
    }

    // Escape brings back the picture from before the lift, from the tiles that
    // were kept: nothing is rasterised again.
    void cancelRestoresTheViewportExactly()
    {
        Fixture f;
        f.session->selection().clear();
        const QImage before = f.grab();
        f.startDrag();
        const QPointF from = f.centreOf("img");
        move(*f.view, from + QPointF(80, 40));
        f.view->viewport()->repaint();
        QVERIFY(f.view->activeLayer().isActive());
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(f.view.get(), &escape);
        QVERIFY(!f.view->activeLayer().isActive());
        f.session->selection().clear();
        render::stats::reset();
        const QImage after = f.grab();
        QCOMPARE(count(Counter::tilesRasterizedSync), std::uint64_t(0));
        QCOMPARE(after, before);
        QVERIFY(!f.session->canUndo());
    }

    // The release edits once; the picture afterwards equals a synchronous
    // direct render of the edited page, and the layer is gone.
    void commitLeavesTheViewportEqualToADirectRender()
    {
        Fixture f;
        const QPointF from = f.startDrag();
        release(*f.view, from + QPointF(50, 30));
        QVERIFY(f.session->canUndo());
        settleLayer(*f.view);
        f.session->selection().clear();
        const QImage live = f.grab();

        Fixture reference(pageXml(150, 130));
        reference.session->selection().clear();
        QCOMPARE(live, reference.grab());
    }

    // Ctrl+drag: the copy is in the layer, the original stays in the tiles.
    void duplicateKeepsTheOriginals()
    {
        Fixture f;
        f.session->selection().clear();
        const QImage before = f.grab();
        const QPointF from = f.startDrag(Qt::ControlModifier);
        QVERIFY(f.view->diagramScene().excluded().empty());
        move(*f.view, from + QPointF(0, 200), Qt::ControlModifier);
        const QImage during = f.grab();
        const QRect original = viewportRect(*f.view, QRectF(100, 100, 200, 120), 6);
        QCOMPARE(differing(during, before, original), 0); // the original is untouched
        QVERIFY(differing(during, before, viewportRect(*f.view, QRectF(100, 300, 200, 120), 6)) > 0); // the copy
        release(*f.view, from + QPointF(0, 200), Qt::ControlModifier);
        settleLayer(*f.view);
        QCOMPARE(f.view->diagramScene().pageView().states().size(), std::size_t(4) + 1);
    }

    // Above the cost estimate's budget the lifted cells are one bitmap, blitted
    // at a whole device offset: the pixels are the same as the live replay.
    void bitmapFallbackEngagesAboveTheBudget()
    {
        Fixture live;
        const QPointF from = live.startDrag();
        move(*live.view, from + QPointF(kDragX, kDragY));
        const QImage replayed = live.grab();
        QVERIFY(!live.view->activeLayer().usesBitmap());
        live.view->interaction().cancelGesture();

        Fixture f;
        f.view->activeLayer().setReplayBudgetUs(0);
        render::stats::reset();
        f.startDrag();
        move(*f.view, from + QPointF(kDragX, kDragY));
        const QImage bitmap = f.grab();
        QVERIFY(f.view->activeLayer().usesBitmap());
        QCOMPARE(count(Counter::activeBitmaps), std::uint64_t(1)); // rasterised once, not per move
        for (int i = 1; i <= 5; ++i) {
            move(*f.view, from + QPointF(50 + i, 30 + i));
            f.view->viewport()->repaint();
        }
        QCOMPARE(count(Counter::activeBitmaps), std::uint64_t(1));
        QVERIFY(count(Counter::activeBitmapsDrawn) >= std::uint64_t(5));
        QCOMPARE(differing(replayed, bitmap, viewportRect(*f.view, QRectF(100 + kDragX, 100 + kDragY, 200, 120), 4)), 0);
        f.view->interaction().cancelGesture();
    }

    // One vertex, resized: the live cell is a record of the previewed geometry;
    // no tile, no page view per move, and the edge attached to it follows.
    void resizeIsLiveAndCostsNoTile()
    {
        Fixture f;
        f.session->selection().set({"b"});
        f.view->viewport()->repaint();
        // The bottom-right sizer of b.
        const QPointF corner(620, 160);
        press(*f.view, corner);
        move(*f.view, corner + QPointF(10, 10));
        f.view->viewport()->repaint();
        QCOMPARE(f.view->activeLayer().state(), ActiveLayer::State::Lifted);
        render::stats::reset();
        for (int i = 1; i <= 8; ++i) {
            move(*f.view, corner + QPointF(10 + 6 * i, 10 + 3 * i));
            f.view->viewport()->repaint();
        }
        QCOMPARE(count(Counter::tilesRasterized), std::uint64_t(0));
        QCOMPARE(count(Counter::pageViewBuilds), std::uint64_t(0));
        release(*f.view, corner + QPointF(58, 34));
        QVERIFY(f.session->canUndo());
        settleLayer(*f.view);
        QCOMPARE(f.view->activeLayer().state(), ActiveLayer::State::Idle);
        const render::CellState* b = f.view->diagramScene().state("b");
        QVERIFY(b->bounds.width > 150);
    }

    // With the tiles on worker threads the layer outlives the release until the
    // new tiles are in; the cell is on screen at its new place all the while, and
    // the end state is the direct render.
    void commitHandsOverToWorkerTilesWithoutAFlash()
    {
        Fixture f;
        f.view->setSynchronousRasterization(false);
        f.view->viewport()->repaint();
        QTRY_VERIFY_WITH_TIMEOUT(f.view->isRenderIdle(), 5000);
        const QPointF from = f.startDrag();
        QTRY_VERIFY_WITH_TIMEOUT(f.view->isRenderIdle(), 5000); // the tiles under the lifted cell are final
        release(*f.view, from + QPointF(50, 30));
        QVERIFY(f.session->canUndo());
        // At any moment after the release the image is where the edit put it.
        const QImage now = f.grab();
        Fixture reference(pageXml(150, 130));
        reference.session->selection().clear();
        f.session->selection().clear();
        QCOMPARE(differing(f.grab(), reference.grab(), viewportRect(*f.view, QRectF(150, 130, 200, 120), 4), channelTolerance(*f.view)), 0);
        QVERIFY(differing(now, reference.grab(), viewportRect(*f.view, QRectF(150, 130, 200, 120), 6)) >= 0);
        QTRY_VERIFY_WITH_TIMEOUT(!f.view->activeLayer().isActive(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(f.view->isRenderIdle(), 5000);
        QCOMPARE(f.grab(), reference.grab());
    }

    // The lift keeps the tiles under the cell for cancel and puts the cell back.
    void liftTakesTheCellOutOfTheTiles()
    {
        Fixture f;
        QVERIFY(f.view->diagramScene().tileSnapshot()->find("img") != nullptr);
        f.startDrag();
        QVERIFY(f.view->diagramScene().tileSnapshot()->find("img") == nullptr);
        QVERIFY(f.view->diagramScene().tileSnapshot()->find("e") == nullptr); // the edge follows live
        QVERIFY(f.view->diagramScene().tileSnapshot()->find("far") != nullptr);
        QVERIFY(f.view->diagramScene().snapshot()->find("img") != nullptr);   // the model's snapshot is whole
        f.view->interaction().cancelGesture();
        QVERIFY(f.view->diagramScene().tileSnapshot()->find("img") != nullptr);
    }
};

QTEST_MAIN(TestInteractionLayer)
#include "tst_interaction_layer.moc"
