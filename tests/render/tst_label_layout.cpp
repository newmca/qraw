// Label placement (src/render/geometry/label_layout.cpp). Expected values are
// worked out by hand from mxCellRenderer.getLabelBounds / rotateLabelBounds
// and mxText.getSpacing; draw.io's text spacing is `spacing` (2) per side
// plus baseSpacingTop 5 / baseSpacingBottom 1 for top / bottom alignment.

#include "qraw/geometry/label_layout.h"

#include <QTest>

#include <cmath>

using namespace qraw::geometry;

namespace {

bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }

// The vertex used throughout: x 10..130, y 20..80, center (70, 50).
const Rect kCell{10, 20, 120, 60};

} // namespace

class LabelLayoutTest : public QObject {
    Q_OBJECT

private slots:
    void boxRect()
    {
        LabelBox b;
        b.x = 100;
        b.y = 50;
        b.width = 40;
        b.height = 20;
        QCOMPARE(b.rect(), (Rect{80, 40, 40, 20}));
        b.align = HAlign::Left;
        b.verticalAlign = VAlign::Top;
        QCOMPARE(b.rect(), (Rect{100, 50, 40, 20}));
        b.align = HAlign::Right;
        b.verticalAlign = VAlign::Bottom;
        QCOMPARE(b.rect(), (Rect{60, 30, 40, 20}));
    }

    void defaultVertexLabel()
    {
        // center/middle: anchor at the cell center; the spacing shifts by
        // (left - right) / 2 = 0 and (top - bottom) / 2 = 0, and shrinks the
        // box by 2 + 2 on each axis.
        const LabelBox b = layoutVertexLabel(kCell, LabelParams{});
        QCOMPARE(b.x, 70.0);
        QCOMPARE(b.y, 50.0);
        QCOMPARE(b.width, 116.0);
        QCOMPARE(b.height, 56.0);
        QCOMPARE(b.align, HAlign::Center);
        QCOMPARE(b.verticalAlign, VAlign::Middle);
        QCOMPARE(b.rotation, 0.0);
    }

    void directionalSpacing()
    {
        // spacingTop 6 + spacing 2 = 8 and spacingBottom 0 + 2 = 2: the center
        // moves down by (8 - 2) / 2 = 3, the height shrinks by 10.
        LabelParams p;
        p.spacingTop = 6;
        const LabelBox b = layoutVertexLabel(kCell, p);
        QCOMPARE(b.y, 53.0);
        QCOMPARE(b.height, 50.0);
    }

    void leftTopAligned()
    {
        // The anchor is the top-left corner (10, 20) plus spacingLeft 2 and
        // spacingTop 2 + baseSpacingTop 5.
        LabelParams p;
        p.align = HAlign::Left;
        p.verticalAlign = VAlign::Top;
        const LabelBox b = layoutVertexLabel(kCell, p);
        QCOMPARE(b.x, 12.0);
        QCOMPARE(b.y, 27.0);
        QCOMPARE(b.width, 116.0);
        QCOMPARE(b.height, 56.0);
        QCOMPARE(b.rect(), (Rect{12, 27, 116, 56}));

        // Right/bottom: the anchor is the bottom-right corner minus
        // spacingRight 2 and spacingBottom 2 + baseSpacingBottom 1.
        p.align = HAlign::Right;
        p.verticalAlign = VAlign::Bottom;
        const LabelBox r = layoutVertexLabel(kCell, p);
        QCOMPARE(r.x, 128.0);
        QCOMPARE(r.y, 77.0);
    }

    void labelOutsideRightBottom()
    {
        // labelPosition=right moves the offset by the width (120),
        // verticalLabelPosition=bottom by the height (60): the text box starts
        // at (130, 80); left/top alignment adds (2, 7). The box is not reduced
        // by the spacing since the position is not centered.
        LabelParams p;
        p.labelPosition = HAlign::Right;
        p.verticalLabelPosition = VAlign::Bottom;
        p.align = HAlign::Left;
        p.verticalAlign = VAlign::Top;
        const LabelBox b = layoutVertexLabel(kCell, p);
        QCOMPARE(b.x, 132.0);
        QCOMPARE(b.y, 87.0);
        QCOMPARE(b.width, 120.0);
        QCOMPARE(b.height, 60.0);

        // labelPosition=left shifts by the width (or labelWidth) the other way.
        LabelParams l;
        l.labelPosition = HAlign::Left;
        l.align = HAlign::Right;
        QCOMPARE(layoutVertexLabel(kCell, l).x, 10.0 - 2.0); // right edge at the cell's left, minus spacingRight
        l.labelWidth = 50;
        const LabelBox lw = layoutVertexLabel(kCell, l);
        QCOMPARE(lw.x, 10.0 - 50.0 + 50.0 - 2.0); // offset -50, then + width 50 for the right anchor
        QCOMPARE(lw.width, 50.0);
    }

    void labelWidthAlignsInsideVertex()
    {
        // labelWidth 60 centered in the 120 wide vertex: offset -(60 - 120) * 0.5 = +30,
        // then the center anchor adds half of the label width.
        LabelParams p;
        p.labelWidth = 60;
        const LabelBox b = layoutVertexLabel(kCell, p);
        QCOMPARE(b.x, 10.0 + 30.0 + 30.0);
        QCOMPARE(b.width, 60.0); // not reduced by the horizontal spacing
    }

    void verticalText()
    {
        // horizontal=0: the label box is turned: 60 x 120 around the same
        // center, anchored at the center (70, 50), reduced by 4 each, and
        // the text is rotated -90.
        LabelParams p;
        p.horizontal = false;
        const LabelBox b = layoutVertexLabel(kCell, p);
        QCOMPARE(b.x, 70.0);
        QCOMPARE(b.y, 50.0);
        QCOMPARE(b.width, 56.0);
        QCOMPARE(b.height, 116.0);
        QCOMPARE(b.rotation, -90.0);
    }

    void rotatedVertex()
    {
        // Anchor (12, 27) rotated by 90 degrees around the center (70, 50):
        // (-58, -23) -> (23, -58) -> (93, -8).
        LabelParams p;
        p.align = HAlign::Left;
        p.verticalAlign = VAlign::Top;
        p.rotation = 90;
        const LabelBox b = layoutVertexLabel(kCell, p);
        QVERIFY(near(b.x, 93));
        QVERIFY(near(b.y, -8));
        QCOMPARE(b.rotation, 90.0);

        // A centered anchor does not move.
        LabelParams c;
        c.rotation = 90;
        const LabelBox cb = layoutVertexLabel(kCell, c);
        QCOMPARE(cb.x, 70.0);
        QCOMPARE(cb.y, 50.0);

        // Vertical text adds -90 to the rotation.
        c.horizontal = false;
        QCOMPARE(layoutVertexLabel(kCell, c).rotation, 0.0);
    }

    void shapeHookOnlyForCenteredPositions()
    {
        int calls = 0;
        Rect seen;
        const ShapeLabelBoundsFn hook = [&](const Rect& r) {
            ++calls;
            seen = r;
            return Rect{r.x + 5, r.y + 5, r.width - 10, r.height - 10};
        };

        const LabelBox b = layoutVertexLabel(kCell, LabelParams{}, hook);
        QCOMPARE(calls, 1);
        QCOMPARE(seen, kCell);
        // Hook area (15, 25, 110, 50): center (70, 50), reduced by 4.
        QCOMPARE(b.x, 70.0);
        QCOMPARE(b.y, 50.0);
        QCOMPARE(b.width, 106.0);
        QCOMPARE(b.height, 46.0);

        LabelParams outside;
        outside.labelPosition = HAlign::Right;
        (void)layoutVertexLabel(kCell, outside, hook);
        outside = LabelParams{};
        outside.verticalLabelPosition = VAlign::Top;
        (void)layoutVertexLabel(kCell, outside, hook);
        QCOMPARE(calls, 1);
    }

    void geometryOffsetShiftsLabel()
    {
        LabelParams p;
        p.offset = {7, -3};
        const LabelBox b = layoutVertexLabel(kCell, p);
        QCOMPARE(b.x, 77.0);
        QCOMPARE(b.y, 47.0);
    }

    void overflowFillIgnoresSpacing()
    {
        LabelParams p;
        p.overflow = Overflow::Fill;
        const LabelBox b = layoutVertexLabel(kCell, p);
        QCOMPARE(b.width, 120.0);
        QCOMPARE(b.height, 60.0);
    }

    void edgeLabel()
    {
        // Center/middle: the spacing offsets cancel out.
        LabelBox b = layoutEdgeLabel({200, 100}, 0, 0, LabelParams{});
        QCOMPARE(b.x, 200.0);
        QCOMPARE(b.y, 100.0);
        QCOMPARE(b.width, 0.0);
        QCOMPARE(b.rotation, 0.0);

        // Left/top: + spacingLeft 2 and spacingTop 2 + base 5; negative sizes clamp.
        LabelParams p;
        p.align = HAlign::Left;
        p.verticalAlign = VAlign::Top;
        p.horizontal = false;
        b = layoutEdgeLabel({200, 100}, -4, 10, p);
        QCOMPARE(b.x, 202.0);
        QCOMPARE(b.y, 107.0);
        QCOMPARE(b.width, 0.0);
        QCOMPARE(b.height, 10.0);
        QCOMPARE(b.rotation, -90.0);

        // Right/bottom: - spacingRight 2, - spacingBottom 2 - base 1.
        p.align = HAlign::Right;
        p.verticalAlign = VAlign::Bottom;
        b = layoutEdgeLabel({200, 100}, 0, 0, p);
        QCOMPARE(b.x, 198.0);
        QCOMPARE(b.y, 97.0);
    }

    void estimateOneLine()
    {
        // 10 characters at 12 px: 10 * 0.6 * 12 = 72 wide, 1.2 * 12 = 14.4 high.
        LabelBox box;
        box.x = 100;
        box.y = 50;
        TextExtentInput t;
        t.longestLine = 10;
        const Rect r = estimateTextExtent(box, t);
        QVERIFY(near(r.width, 72) && near(r.height, 14.4));
        QVERIFY(near(r.x, 64) && near(r.y, 42.8)); // centered around the anchor

        t.bold = true; // 0.65 per character
        QVERIFY(near(estimateTextExtent(box, t).width, 78));

        box.align = HAlign::Left;
        box.verticalAlign = VAlign::Top;
        const Rect lt = estimateTextExtent(box, t);
        QVERIFY(near(lt.x, 100) && near(lt.y, 50));
    }

    void estimateWrapsAtBoxWidth()
    {
        LabelBox box;
        box.x = 100;
        box.y = 50;
        box.width = 36;
        TextExtentInput t;
        t.longestLine = 10;
        t.wrap = true;
        const Rect r = estimateTextExtent(box, t);
        QVERIFY(near(r.width, 36));
        QVERIFY(near(r.height, 28.8)); // 72 / 36 = 2 lines
        QVERIFY(near(r.x, 82)); // centered in the box (82..118)

        t.wrap = false; // no wrapping: the text overflows the box
        QVERIFY(near(estimateTextExtent(box, t).width, 72));
    }

    void estimateRotatedAndBounded()
    {
        LabelBox box;
        box.x = 100;
        box.y = 50;
        box.rotation = -90;
        TextExtentInput t;
        t.longestLine = 10;
        // 72 x 14.4 centered at the anchor turns into 14.4 x 72.
        const Rect r = estimateTextExtent(box, t);
        QVERIFY(near(r.width, 14.4, 1e-9) && near(r.height, 72, 1e-9));
        QVERIFY(near(r.x, 92.8, 1e-9) && near(r.y, 14, 1e-9));

        // Bounded in both directions: placed inside the box rectangle.
        LabelBox b2;
        b2.x = 100;
        b2.y = 50;
        b2.width = 200;
        b2.height = 100;
        b2.align = HAlign::Right;
        b2.verticalAlign = VAlign::Bottom;
        const Rect r2 = estimateTextExtent(b2, t); // box rect: (-100, -50, 200, 100)
        QVERIFY(near(r2.x, 100 - 72) && near(r2.y, 50 - 14.4));
    }
};

QTEST_APPLESS_MAIN(LabelLayoutTest)
#include "tst_label_layout.moc"
