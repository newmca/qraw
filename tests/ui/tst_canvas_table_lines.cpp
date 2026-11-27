// Table grid lines on the canvas: a table paints the row and column lines
// between its rows and cells, as draw.io does (shape=table, rowLines and
// columnLines default to 1). Checked on pixels of the rendered scene.

#include "support/test_support.h"

#include "qraw/commands/ops_tables.h"

#include <QFile>
#include <QGraphicsScene>
#include <algorithm>
#include <QImage>
#include <QPainter>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;
namespace ops = qraw::commands::ops;

namespace {

// golden/containers-tables.drawio: table "tb" at page x 40..280, y 380..500.
// Its header is 30 high, rows are 45 high, columns 120 wide, so the row line
// is at y 455 and the column line at x 160 (inside the table, page space).
constexpr QRectF kTableRect{40, 380, 240, 120};

core::Document tablesDocument()
{
    QFile file(QStringLiteral(QRAW_SOURCE_DIR "/tests/render/data/golden/containers-tables.drawio"));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("fixture is missing");
    const QByteArray bytes = file.readAll();
    return documentFromXml(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
}

// Darkest gray level of the pixels in a small window around a scene point.
int darkestNear(const QImage& image, QPointF scenePos)
{
    int darkest = 255;
    const int cx = static_cast<int>(scenePos.x() - kTableRect.x());
    const int cy = static_cast<int>(scenePos.y() - kTableRect.y());
    for (int y = cy - 2; y <= cy + 2; ++y) {
        for (int x = cx - 2; x <= cx + 2; ++x) {
            if (x < 0 || y < 0 || x >= image.width() || y >= image.height())
                continue;
            darkest = std::min(darkest, qGray(image.pixel(x, y)));
        }
    }
    return darkest;
}

// The table's part of the canvas, rendered at 1:1 scale.
QImage renderTable(DiagramScene& scene)
{
    QImage image(kTableRect.size().toSize(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    scene.render(&painter, QRectF(0, 0, kTableRect.width(), kTableRect.height()), kTableRect);
    painter.end();
    return image;
}

} // namespace

class TestCanvasTableLines : public QObject {
    Q_OBJECT

private slots:
    void gridLinesAreDrawn()
    {
        DocumentSession session(tablesDocument());
        auto canvas = makeCanvas(session, {1280, 800});
        const QImage image = renderTable(canvas->diagramScene());
        QVERIFY2(darkestNear(image, {160, 440}) < 160, "column line between the two cells");
        QVERIFY2(darkestNear(image, {100, 455}) < 160, "row line between the two rows");
        QVERIFY2(darkestNear(image, {60, 470}) > 200, "no line through the cell interior");
    }

    void resizedColumnMovesItsLine()
    {
        DocumentSession session(tablesDocument());
        auto canvas = makeCanvas(session, {1280, 800});
        const QByteArray original = savedBytes(session);

        // Grows the first column by 20: the column line moves from x 160 to x 180.
        DiagramScene& scene = canvas->diagramScene();
        QList<QRectF> dirty;
        QObject::connect(&scene, &DiagramScene::contentDamaged, &scene,
                         [&dirty](const render::Damage& damage) { dirty.append(QList<QRectF>(damage.rects.begin(), damage.rects.end())); });
        QVERIFY(session.edit(QStringLiteral("Column Width"), [](commands::Transaction& tx) {
                    return ops::setTableColumnWidth(tx, 0, "tc00", 20);
                }).ok);
        // The table is recorded again: its own bounds are damaged, and render()
        // below would paint the table whether or not it was refreshed. The
        // header (y 380..410) is outside every cell, so only the table's own
        // record can damage it.
        QVERIFY2(std::any_of(dirty.begin(), dirty.end(), [](const QRectF& r) { return r.contains(QPointF(100, 395)); }),
                 "table record rebuilt after a descendant changed");
        const QImage moved = renderTable(scene);
        QVERIFY2(darkestNear(moved, {180, 440}) < 160, "line follows the column");
        QVERIFY2(darkestNear(moved, {160, 440}) > 200, "old position is clear");

        QVERIFY(session.undo());
        QCOMPARE(savedBytes(session), original);
        const QImage restored = renderTable(scene);
        QVERIFY2(darkestNear(restored, {160, 440}) < 160, "undo restores the line");
    }
};

QTEST_MAIN(TestCanvasTableLines)
#include "tst_canvas_table_lines.moc"
