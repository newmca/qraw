// The Freehand tool: a drag draws one stencil vertex, the live stroke is an
// overlay with exact bounds, Escape cancels, the stored default style applies,
// and Insert > Freehand (X) turns the tool on.

#include "support/select_fixture.h"
#include "support/shell_env.h"

#include "qraw/render/export.h"
#include "qraw/render/stencil.h"
#include "qraw/ui/freehand_tool.h"
#include "qraw/ui/main_window.h"

#include <QAction>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QSignalSpy>
#include <QToolBar>
#include <QtTest>

#include <cmath>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;
namespace id = qraw::ui::action_id;

namespace {

// A free-hand looking path in the empty area below the cells of the fixture.
const std::vector<QPointF>& sketch()
{
    static const std::vector<QPointF> points = [] {
        std::vector<QPointF> p;
        for (int i = 0; i <= 40; ++i)
            p.emplace_back(100 + i * 8.0, 420 + 60 * std::sin(i / 40.0 * 6.28318));
        return p;
    }();
    return points;
}

void drawSketch(CanvasView& view)
{
    const auto& points = sketch();
    press(view, points.front());
    for (std::size_t i = 1; i < points.size(); ++i)
        move(view, points[i]);
    release(view, points.back());
}

const core::Cell* strokeCell(const DocumentSession& session)
{
    const core::Cell* found = nullptr;
    for (const core::Cell* cell : session.currentPage()->cells()) {
        if (cell->style().value("shape").value_or("").starts_with("stencil(")) {
            if (found != nullptr)
                return nullptr; // more than one
            found = cell;
        }
    }
    return found;
}

std::size_t cellCount(const DocumentSession& session) { return session.currentPage()->cells().size(); }

QString value(const core::Cell& cell, const char* key)
{
    return QString::fromStdString(cell.style().value(key).value_or("<absent>"));
}

bool overlayDraws(CanvasView& view)
{
    QImage image(900, 700, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    OverlayContext context;
    context.scale = 1;
    view.interaction().paintOverlay(painter, context);
    painter.end();
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(image.pixel(x, y)) != 0)
                return true;
    return false;
}

} // namespace

class TestFreehand : public QObject {
    Q_OBJECT

private slots:
    void init() { AppSettings().setDefaultStyles({}); }

    void withTheToolOffADragSelectsAndDrawsNothing()
    {
        SelectFixture f;
        const std::size_t before = cellCount(f.session);
        drawSketch(f.canvas());
        QCOMPARE(cellCount(f.session), before);
        QVERIFY(!f.session.canUndo());
    }

    void aDragCreatesOneStencilVertexAndOneUndoStep()
    {
        SelectFixture f;
        f.canvas().setFreehandActive(true);
        const std::size_t before = cellCount(f.session);
        drawSketch(f.canvas());

        QCOMPARE(cellCount(f.session), before + 1);
        const core::Cell* cell = strokeCell(f.session);
        QVERIFY(cell != nullptr);
        QVERIFY(cell->isVertex());
        QCOMPARE(f.session.undoText(), QStringLiteral("Freehand"));
        QCOMPARE(f.selected(), std::vector<std::string>{cell->id()});
        // The cell lies where the pointer went (its extent, not snapped to the grid).
        const core::Rect bounds = cell->geometry()->bounds();
        QVERIFY(std::abs(bounds.x - 100) < 4);
        QVERIFY(std::abs(bounds.width - 320) < 4);
        QVERIFY(bounds.height > 100 && bounds.height < 125);
        // The stencil decodes and the style is plain: an unfilled line of the default width.
        QVERIFY(render::StencilRegistry::builtin().findInline(cell->style().value("shape").value_or("")) != nullptr);
        QCOMPARE(value(*cell, "fillColor"), QStringLiteral("none"));
        QCOMPARE(value(*cell, "strokeWidth"), QStringLiteral("2"));
        QCOMPARE(value(*cell, "strokeColor"), QStringLiteral("<absent>")); // the theme's line colour

        // One undo step removes it, redo brings it back.
        QVERIFY(f.undoRestores(f.original));
        QVERIFY(!f.session.canUndo());
        QVERIFY(f.session.redo());
        QVERIFY(strokeCell(f.session) != nullptr);
    }

    void theRenderedPathPassesNearTheInputPoints()
    {
        SelectFixture f;
        f.canvas().setFreehandActive(true);
        drawSketch(f.canvas());
        QTest::qWait(30);
        grabToPng(f.canvas(), QStringLiteral("freehand-stroke")); // manual review: QRAW_SCREENSHOT_DIR

        render::ExportOptions options;
        options.format = render::ExportFormat::Png;
        options.border = 0;
        const render::ExportResult result = render::exportPage(f.session.document(), 0, options);
        QVERIFY(result.ok());
        QImage image;
        QVERIFY(image.loadFromData(result.data));
        const render::PageView view = render::PageView::build(*f.session.currentPage(), render::RenderContext::defaults());
        const render::Rect content = view.bounds();

        // Every point the pointer visited has an ink pixel within two pixels. (The fixture's own
        // cells are far above the sketch, so any ink there is the stroke.)
        int checked = 0;
        for (const QPointF& p : sketch()) {
            const QPoint at(int(std::lround(p.x() - content.x)), int(std::lround(p.y() - content.y)));
            bool ink = false;
            for (int dy = -2; dy <= 2 && !ink; ++dy) {
                for (int dx = -2; dx <= 2 && !ink; ++dx) {
                    const QPoint q = at + QPoint(dx, dy);
                    if (image.rect().contains(q) && image.pixelColor(q).lightness() < 128 && image.pixelColor(q).alpha() > 128)
                        ink = true;
                }
            }
            QVERIFY2(ink, qPrintable(QStringLiteral("no ink near %1,%2").arg(p.x()).arg(p.y())));
            ++checked;
        }
        QCOMPARE(checked, int(sketch().size()));
    }

    void theLiveStrokeIsAnOverlayWithExactBounds()
    {
        SelectFixture f;
        f.canvas().setFreehandActive(true);
        const auto& points = sketch();
        OverlayContext context;
        context.scale = 1;
        const OverlayBounds idle = f.canvas().interaction().overlayBounds(context);
        QVERIFY(idle.has_value() && idle->isNull()); // nothing is painted before the press

        press(f.canvas(), points.front());
        for (std::size_t i = 1; i <= 20; ++i)
            move(f.canvas(), points[i]);
        QVERIFY(f.canvas().interaction().isGestureActive());
        QVERIFY(overlayDraws(f.canvas()));
        // The bounds are known (no full repaint) and contain every point drawn so far.
        const OverlayBounds bounds = f.canvas().interaction().overlayBounds(context);
        QVERIFY(bounds.has_value());
        QVERIFY(!bounds->isNull());
        for (std::size_t i = 0; i <= 20; ++i)
            QVERIFY(bounds->contains(points[i]));
        // ...and are not the whole page: a conservative box around the stroke.
        QVERIFY(bounds->width() < 200);
        QVERIFY(bounds->height() < 80);

        release(f.canvas(), points[20]);
        QVERIFY(!f.canvas().interaction().isGestureActive());
        const OverlayBounds after = f.canvas().interaction().overlayBounds(context);
        QVERIFY(after.has_value()); // still exact: the selection handles of the new cell, not the stroke's overlay
    }

    void escapeCancelsTheStrokeAndLeavesTheModelAlone()
    {
        SelectFixture f;
        f.canvas().setFreehandActive(true);
        const auto& points = sketch();
        press(f.canvas(), points.front());
        for (std::size_t i = 1; i <= 15; ++i)
            move(f.canvas(), points[i]);
        QVERIFY(overlayDraws(f.canvas()));

        QTest::keyClick(&f.canvas(), Qt::Key_Escape);
        QVERIFY(!f.canvas().interaction().isGestureActive());
        QVERIFY(!overlayDraws(f.canvas()));
        QVERIFY(f.canvas().freehandActive()); // the tool stays on for the next stroke
        release(f.canvas(), points[15]);       // the release of the cancelled stroke does nothing
        QCOMPARE(savedBytes(f.session), f.original);
        QVERIFY(!f.session.canUndo());
    }

    void escapeBetweenStrokesTurnsTheToolOff()
    {
        SelectFixture f;
        QSignalSpy spy(&f.canvas(), &CanvasView::freehandActiveChanged);
        f.canvas().setFreehandActive(true);
        QCOMPARE(spy.count(), 1);
        QTest::keyClick(&f.canvas(), Qt::Key_Escape);
        QVERIFY(!f.canvas().freehandActive());
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.last().first().toBool(), false);
    }

    void aClickWithoutMovingDrawsNothing()
    {
        SelectFixture f;
        f.canvas().setFreehandActive(true);
        click(f.canvas(), {300, 450});
        QCOMPARE(savedBytes(f.session), f.original);
    }

    void theStoredDefaultStyleApplies()
    {
        SelectFixture f;
        commands::DefaultStyles defaults;
        defaults.vertex = core::Style("strokeColor=#FF0000;strokeWidth=5;dashed=1;fillColor=#00FF00;");
        AppSettings().setDefaultStyles(defaults);
        f.canvas().setFreehandActive(true);
        drawSketch(f.canvas());
        const core::Cell* cell = strokeCell(f.session);
        QVERIFY(cell != nullptr);
        QCOMPARE(value(*cell, "strokeColor"), QStringLiteral("#FF0000"));
        QCOMPARE(value(*cell, "strokeWidth"), QStringLiteral("5"));
        QCOMPARE(value(*cell, "dashed"), QStringLiteral("1"));
        QCOMPARE(value(*cell, "fillColor"), QStringLiteral("none")); // an open stroke is never filled
        const FreehandBrush brush = freehandBrush(f.session);
        QCOMPARE(brush.color, QColor(0xff, 0, 0));
        QCOMPARE(brush.width, 5.0);
    }

    void theBrushFollowsTheDiagramScheme()
    {
        SelectFixture f;
        QCOMPARE(freehandBrush(f.session).color, QColor(Qt::black));
        f.session.setDiagramDark(true);
        QCOMPARE(freehandBrush(f.session).color, QColor(Qt::white));
    }

    void aStrokeNeedsTwoDifferentPoints()
    {
        SelectFixture f;
        const std::vector<QPointF> line{{100, 100}, {200, 200}};
        QVERIFY(actions::insertFreehandStroke(f.session, line, 1));
        QVERIFY(!actions::insertFreehandStroke(f.session, std::vector<QPointF>{{5, 5}}, 1));
        QVERIFY(!actions::insertFreehandStroke(f.session, std::vector<QPointF>{{5, 5}, {5, 5}}, 1));
    }

    void insertFreehandIsInTheInsertMenuAndTheToolBarAndTurnsTheToolOn()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QAction* freehand = window.actions()[id::kFreehand];
        QVERIFY(freehand != nullptr);
        QVERIFY(freehand->isCheckable());
        QCOMPARE(freehand->shortcut(), QKeySequence(Qt::Key_X));
        QCOMPARE(freehand->text(), QStringLiteral("Freehand"));
        QVERIFY(!freehand->toolTip().isEmpty());

        QMenu* insert = window.findChild<QMenu*>(QStringLiteral("menu.insert"));
        QVERIFY(insert != nullptr);
        QVERIFY(insert->actions().contains(freehand));
        QVERIFY(window.findChild<QToolBar*>(QStringLiteral("toolbar.main"))->actions().contains(freehand));

        QVERIFY(freehand->isEnabled());
        freehand->trigger();
        QVERIFY(window.canvas().freehandActive());
        QVERIFY(freehand->isChecked());
        // Escape on the canvas leaves the tool; the action follows.
        QTest::keyClick(&window.canvas(), Qt::Key_Escape);
        QVERIFY(!window.canvas().freehandActive());
        QVERIFY(!freehand->isChecked());
    }
};

SHELL_TEST_MAIN(TestFreehand)
#include "tst_freehand.moc"
