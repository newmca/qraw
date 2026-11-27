// Tables with merged cells and tableRender=collapsed, observed through the
// RecordingCanvas: the grid of a table whose cells are covered by a merged cell
// (hidden children), and the collapsed painter that strokes each shared border
// once and leaves the fills and borders of rows and cells to the table.
// golden/tables-merged.drawio: "tc" is a 3x3 collapsed table at (40, 40) with
// a 2x2 merged cell, a 2x1 merged cell and a cell with its own red stroke;
// "ts" a 3x2 table that paints no collapsed borders, with a 2x1 merged cell.

#include "qraw/format/codec.h"
#include "qraw/render/scene_renderer.h"
#include "qraw/render/shape.h"

#include "support/recording_canvas.h"

#include <QFile>
#include <QTest>

#include <algorithm>

using namespace qraw;
using render::ResolvedStyle;
using render::ShapeChild;
using render::StyleMap;
using test::RecordingCanvas;

namespace {

core::Document mergedFixture()
{
    QFile file(QStringLiteral(QRAW_TEST_DATA_DIR "/golden/tables-merged.drawio"));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("fixture is missing");
    auto read = format::readDocument(file.readAll());
    if (!read.ok())
        qFatal("fixture does not parse");
    return std::move(*read.document);
}

bool hasOp(const RecordingCanvas& c, const std::string& op)
{
    return std::find(c.ops.begin(), c.ops.end(), op) != c.ops.end();
}

int count(const RecordingCanvas& c, const std::string& op)
{
    return static_cast<int>(std::count(c.ops.begin(), c.ops.end(), op));
}

ResolvedStyle makeStyle(std::initializer_list<std::pair<const char*, const char*>> values)
{
    StyleMap map;
    for (const auto& [key, value] : values)
        map.set(key, value);
    return ResolvedStyle(std::move(map), false);
}

// A table of rows x cols cells of 50 x 30 (the cells' own styles in `cellStyle`).
std::vector<ShapeChild> grid(int rows, int cols, const ResolvedStyle& rowStyle, const ResolvedStyle& cellStyle)
{
    std::vector<ShapeChild> result;
    for (int r = 0; r < rows; ++r) {
        ShapeChild row;
        row.id = "row" + std::to_string(r);
        row.bounds = {0, r * 30.0, cols * 50.0, 30};
        row.style = &rowStyle;
        for (int c = 0; c < cols; ++c) {
            ShapeChild cell;
            cell.id = row.id + "c" + std::to_string(c);
            cell.bounds = {c * 50.0, 0, 50, 30};
            cell.style = &cellStyle;
            row.children.push_back(std::move(cell));
        }
        result.push_back(std::move(row));
    }
    return result;
}

RecordingCanvas paintTable(const ResolvedStyle& table, const std::vector<ShapeChild>& rows, double w, double h)
{
    const render::VertexShape* shape = render::ShapeRegistry::builtin().find("table");
    RecordingCanvas c;
    render::ShapePaintContext context{table, nullptr, [&] { return rows; }, false};
    shape->paint(c, context, 0, 0, w, h);
    return c;
}

const ResolvedStyle kCollapsed = makeStyle({{"shape", "table"},
                                            {"startSize", "0"},
                                            {"fixedHeader", "1"},
                                            {"childLayout", "tableLayout"},
                                            {"tableRender", "collapsed"},
                                            {"strokeColor", "#000000"}});
const ResolvedStyle kRowInherit = makeStyle({{"shape", "tableRow"},
                                             {"horizontal", "0"},
                                             {"startSize", "0"},
                                             {"fixedHeader", "1"},
                                             {"strokeColor", "inherit"},
                                             {"fillColor", "none"}});
const ResolvedStyle kCellInherit =
    makeStyle({{"shape", "partialRectangle"}, {"strokeColor", "inherit"}, {"fillColor", "none"}});

} // namespace

class TablesCollapsedTest : public QObject {
    Q_OBJECT

private slots:
    void sharedBordersAreStrokedOnceAsOneLinePerGridLine()
    {
        const auto rows = grid(2, 2, kRowInherit, kCellInherit);
        const RecordingCanvas c = paintTable(kCollapsed, rows, 100, 60);
        // One path for the inner lines (the column line and the row line, each
        // as one segment through both rows or columns) and one for the outline.
        QCOMPARE(count(c, "stroke"), 2);
        QVERIFY(hasOp(c, "moveTo 50 0"));
        QVERIFY(hasOp(c, "lineTo 50 60"));
        QVERIFY(hasOp(c, "moveTo 0 30"));
        QVERIFY(hasOp(c, "lineTo 100 30"));
        QCOMPARE(count(c, "moveTo 50 0"), 1);
        QCOMPARE(count(c, "moveTo 0 30"), 1);
        // The inner lines have square caps, the outline is stroked in one closed path.
        QVERIFY(hasOp(c, "setLineCap 2"));
        QCOMPARE(count(c, "close"), 1);
    }

    void aMergedCellHasNoBordersInsideAndCoveredCellsStillCount()
    {
        // The first cell covers the cell to its right and below it; the grid
        // lists the covered cells as hidden children.
        const ResolvedStyle merged = makeStyle({{"shape", "partialRectangle"}, {"strokeColor", "inherit"},
                                                {"fillColor", "none"}, {"rowspan", "2"}, {"colspan", "2"}});
        auto rows = grid(3, 3, kRowInherit, kCellInherit);
        rows[0].children[0].style = &merged;
        rows[0].children[0].bounds = {0, 0, 100, 60};
        rows[0].children[0].alternateBounds = render::Rect{0, 0, 50, 30};
        for (auto* cell : {&rows[0].children[1], &rows[1].children[0], &rows[1].children[1]})
            cell->visible = false;
        const RecordingCanvas c = paintTable(kCollapsed, rows, 150, 90);
        // The column line at x=100 and the row line at y=60 run the whole size,
        // the lines at x=50 and y=30 only outside the merged cell.
        QVERIFY(hasOp(c, "moveTo 100 0"));
        QVERIFY(hasOp(c, "lineTo 100 90"));
        QVERIFY(hasOp(c, "moveTo 0 60"));
        QVERIFY(hasOp(c, "lineTo 150 60"));
        QVERIFY(hasOp(c, "moveTo 100 30"));
        QVERIFY(hasOp(c, "lineTo 150 30"));
        QVERIFY(hasOp(c, "moveTo 50 60"));
        QVERIFY(hasOp(c, "lineTo 50 90"));
        QVERIFY(!hasOp(c, "moveTo 50 0"));
        QVERIFY(!hasOp(c, "moveTo 0 30"));
        QVERIFY(!hasOp(c, "moveTo 50 30"));
    }

    void aCellStrokeBeatsTheTableOnSharedBorders()
    {
        const ResolvedStyle red = makeStyle({{"shape", "partialRectangle"}, {"strokeColor", "#ff0000"},
                                             {"strokeWidth", "3"}, {"fillColor", "none"}});
        auto rows = grid(2, 2, kRowInherit, kCellInherit);
        rows[1].children[1].style = &red;
        const RecordingCanvas c = paintTable(kCollapsed, rows, 100, 60);
        // The red cell owns its top and left border and, being a corner cell, the
        // outline on its two sides: one red path for the inner lines, one for the
        // outline. The rest of the lines stay black.
        QCOMPARE(count(c, "setStrokeColor #ff0000"), 2);
        QCOMPARE(count(c, "setStrokeWidth 3"), 2);
        QVERIFY(hasOp(c, "moveTo 50 30"));  // its left side
        QVERIFY(hasOp(c, "lineTo 50 60"));
        QVERIFY(hasOp(c, "moveTo 50 0"));   // the column line above it stays black
        QVERIFY(hasOp(c, "lineTo 50 30"));
        QVERIFY(hasOp(c, "moveTo 0 30"));
        QVERIFY(hasOp(c, "lineTo 50 30"));
    }

    void sidesSetTo0HideTheSharedBorder()
    {
        const ResolvedStyle noTop = makeStyle({{"shape", "partialRectangle"}, {"strokeColor", "inherit"},
                                               {"fillColor", "none"}, {"top", "0"}});
        auto rows = grid(2, 1, kRowInherit, kCellInherit);
        rows[1].children[0].style = &noTop;
        const RecordingCanvas c = paintTable(kCollapsed, rows, 50, 60);
        // The line between the two rows is the top of the second cell: hidden.
        QVERIFY(!hasOp(c, "moveTo 0 30"));
        QVERIFY(!hasOp(c, "lineTo 50 30"));
    }

    void rowsAndCellsFillThroughTheTable()
    {
        const ResolvedStyle filled = makeStyle({{"shape", "partialRectangle"}, {"strokeColor", "inherit"},
                                                {"fillColor", "#dae8fc"}});
        auto rows = grid(1, 2, kRowInherit, kCellInherit);
        rows[0].children[1].style = &filled;
        const RecordingCanvas c = paintTable(kCollapsed, rows, 100, 30);
        QVERIFY(hasOp(c, "setFillColor #dae8fc"));
        // The filled cell is a rectangle path at its place.
        QVERIFY(hasOp(c, "moveTo 50 0"));
    }

    void aCellOfAnotherShapePaintsItself()
    {
        // The table paints rectangular cells only: an ellipse in a cell is its own.
        const QByteArray xml =
            "<mxfile><diagram id=\"p\" name=\"p\"><mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
            "<mxCell id=\"t\" style=\"shape=table;startSize=0;childLayout=tableLayout;fixedHeader=1;tableRender=collapsed;\" vertex=\"1\" parent=\"1\"><mxGeometry width=\"100\" height=\"40\" as=\"geometry\"/></mxCell>"
            "<mxCell id=\"r\" style=\"shape=tableRow;strokeColor=inherit;fillColor=none;\" vertex=\"1\" parent=\"t\"><mxGeometry width=\"100\" height=\"40\" as=\"geometry\"/></mxCell>"
            "<mxCell id=\"a\" style=\"ellipse;strokeColor=inherit;\" vertex=\"1\" parent=\"r\"><mxGeometry width=\"50\" height=\"40\" as=\"geometry\"/></mxCell>"
            "<mxCell id=\"b\" style=\"rounded=1;strokeColor=inherit;\" vertex=\"1\" parent=\"r\"><mxGeometry x=\"50\" width=\"50\" height=\"40\" as=\"geometry\"/></mxCell>"
            "</root></mxGraphModel></diagram></mxfile>";
        auto read = format::readDocument(xml);
        QVERIFY(read.ok());
        const auto context = render::RenderContext::defaults();
        const auto view = render::PageView::build(*read.document->page(0), context);
        const render::SceneRenderer renderer(context);
        for (const char* id : {"a", "b"}) {
            RecordingCanvas c;
            renderer.paintShape(c, *view.find(id), &view);
            QVERIFY2(c.paints > 0, id);
        }
        RecordingCanvas row;
        renderer.paintShape(row, *view.find("r"), &view);
        QCOMPARE(row.paints, 0);
    }

    void rowsAndCellsOfACollapsedTablePaintNothingThemselves()
    {
        const auto doc = mergedFixture();
        const auto context = render::RenderContext::defaults();
        const auto view = render::PageView::build(*doc.page(0), context);
        const render::SceneRenderer renderer(context);
        for (const char* id : {"tcr0", "tcr1", "tcc00", "tcc02", "tcc22"}) {
            const render::CellState* state = view.find(id);
            QVERIFY2(state != nullptr, id);
            RecordingCanvas c;
            renderer.paintShape(c, *state, &view);
            QCOMPARE(c.paints, 0);
        }
        // The table of the same fixture that is not collapsed leaves them to the cells.
        const render::CellState* cell = view.find("tsc02");
        QVERIFY(cell != nullptr);
        RecordingCanvas c;
        renderer.paintShape(c, *cell, &view);
        QVERIFY(c.paints > 0);
    }

    void theMergedFixtureCountsHiddenCellsInTheGrid()
    {
        const auto doc = mergedFixture();
        const auto context = render::RenderContext::defaults();
        const auto view = render::PageView::build(*doc.page(0), context);
        const render::SceneRenderer renderer(context);

        // Covered cells have no state; their row keeps them for the grid.
        QVERIFY(view.find("tcc01") == nullptr);
        QVERIFY(view.find("tcc10") == nullptr);
        const render::CellState* row0 = view.find("tcr0");
        QVERIFY(row0 != nullptr);
        QCOMPARE(row0->hiddenChildren.size(), std::size_t(1));
        QCOMPARE(row0->hiddenChildren[0].position, std::size_t(1));
        QCOMPARE(row0->hiddenChildren[0].id, std::string("tcc01"));

        // Collapsed table: the 2x2 cell (40..280 x 40..140) has no line inside, the
        // line at x=280 runs along its right side.
        RecordingCanvas collapsed;
        renderer.paintShape(collapsed, *view.find("tc"), &view);
        QVERIFY(!hasOp(collapsed, "moveTo 160 40"));
        QVERIFY(!hasOp(collapsed, "moveTo 40 90"));
        QVERIFY(hasOp(collapsed, "moveTo 280 40"));
        QVERIFY(hasOp(collapsed, "moveTo 280 90"));
        QVERIFY(hasOp(collapsed, "lineTo 400 90"));

        // Separate table: the grid lines leave a gap under the merged cell.
        RecordingCanvas separate;
        renderer.paintShape(separate, *view.find("ts"), &view);
        // No column line inside the merged cell: the line breaks at its top.
        QVERIFY(!QString::fromStdString(separate.text()).contains(QStringLiteral("moveTo 520 40\nlineTo 520")));
        QVERIFY(hasOp(separate, "moveTo 520 90"));   // below it the column line is there
        QVERIFY(hasOp(separate, "lineTo 520 140"));
        QVERIFY(hasOp(separate, "lineTo 600 140"));  // and the one between the merged cell and 0,2 runs along it
    }
};

QTEST_MAIN(TablesCollapsedTest)
#include "tst_tables_collapsed.moc"
