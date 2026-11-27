// Level of detail on the canvas: a cell too small to see is not drawn at a far
// zoom, and is still there for hit testing, selection and the rubber band.

#include "support/test_support.h"

#include "qraw/render/stats.h"

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;
using render::stats::Counter;

class TestCanvasLevelOfDetail : public QObject {
    Q_OBJECT

private slots:
    void tinyCellsAreSkippedButStillHittable()
    {
        const std::string xml =
            R"(<mxfile><diagram id="p" name="P"><mxGraphModel grid="0" page="0"><root><mxCell id="0"/><mxCell id="1" parent="0"/>)"
            R"(<mxCell id="big" value="Big" style="fillColor=#dae8fc;" vertex="1" parent="1"><mxGeometry x="0" y="0" width="2000" height="1000" as="geometry"/></mxCell>)"
            R"(<mxCell id="tiny" value="" style="fillColor=#f8cecc;" vertex="1" parent="1"><mxGeometry x="500" y="500" width="3" height="3" as="geometry"/></mxCell>)"
            R"(</root></mxGraphModel></diagram></mxfile>)";
        DocumentSession session(documentFromXml(xml));
        auto view = makeCanvas(session);
        (void)QTest::qWaitForWindowExposed(view.get()); // repaint() paints nothing in a window that is not exposed
        // The thresholds are in device pixels: the zoom is the one that makes 0.05 device pixels per
        // canvas unit at any device pixel ratio (a 13 px cell is 1.3 device px at 0.05 on a 2x screen).
        view->setZoom(0.05 / view->viewport()->devicePixelRatioF());
        render::stats::reset();
        view->viewport()->repaint();
        // The tiny cell (13 x 13 with what it can paint: 0.65 device px) and the label of "Big" (11 px font: 0.55 device px).
        QVERIFY(render::stats::get(Counter::cellsSkippedLod) >= 1);
        QVERIFY(render::stats::get(Counter::labelsSkippedLod) >= 1);

        DiagramScene& scene = view->diagramScene();
        // Skipped on the screen, selectable all the same: the hit test and the rubber band read the model.
        QCOMPARE(QString::fromStdString(scene.cellAt(QPointF(501.5, 501.5), 2 / view->zoom(),
                                                     [](const render::CellState& s) { return s.id == "tiny"; })),
                 QStringLiteral("tiny"));
        const auto picked = scene.cellsInRect(QRectF(490, 490, 30, 30));
        QCOMPARE(picked.size(), std::size_t(1));
        QCOMPARE(QString::fromStdString(picked.front()), QStringLiteral("tiny"));

        // Zoomed in again, nothing is skipped.
        view->setZoom(1);
        view->centerOn(QRectF(500, 500, 3, 3));
        render::stats::reset();
        view->viewport()->repaint();
        QCOMPARE(render::stats::get(Counter::cellsSkippedLod), std::uint64_t(0));
        QCOMPARE(render::stats::get(Counter::labelsSkippedLod), std::uint64_t(0));
    }
};

QTEST_MAIN(TestCanvasLevelOfDetail)
#include "tst_level_of_detail.moc"
