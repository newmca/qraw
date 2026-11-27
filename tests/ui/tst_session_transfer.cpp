// cells as XML (clipboard / palette payloads).

#include "support/test_support.h"

#include "qraw/commands/ops_structure.h"
#include "qraw/commands/transaction.h"

#include <QMimeData>
#include <QUrl>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;

namespace {

const core::Cell& cellOf(const CellBundle& bundle, std::string_view id)
{
    for (const core::Cell& cell : bundle.cells) {
        if (cell.id() == id)
            return cell;
    }
    throw std::runtime_error("no such cell in bundle: " + std::string(id));
}

std::vector<std::string> idsOf(const CellBundle& bundle)
{
    std::vector<std::string> ids;
    for (const core::Cell& cell : bundle.cells)
        ids.push_back(cell.id());
    return ids;
}

} // namespace

class TestSessionTransfer : public QObject {
    Q_OBJECT

private slots:
    void encodeIsACompactModelAndKeepsIds()
    {
        const core::Document document = test::simpleDocument();
        const std::string xml = encodeCells(*document.page(0), {"a", "b", "e", "g"});
        QVERIFY(xml.starts_with("<mxGraphModel>")); // the codec's own pretty-printed XML
        QVERIFY(!xml.starts_with("<?xml"));

        const auto bundle = decodeCells(xml);
        QVERIFY(bundle);
        QCOMPARE(idsOf(*bundle), (std::vector<std::string>{"a", "b", "e", "g", "c1", "c2"}));
        QCOMPARE(cellOf(*bundle, "a").parent(), std::string());
        QCOMPARE(cellOf(*bundle, "g").parent(), std::string());
        QCOMPARE(cellOf(*bundle, "c1").parent(), std::string("g")); // inside the group
        QCOMPARE(cellOf(*bundle, "e").source(), std::string("a"));
        QVERIFY(bundle->bounds());
        QCOMPARE(bundle->bounds()->x, 40.0);
        QCOMPARE(bundle->bounds()->width, 360.0); // 40..400
    }

    void encodeMakesCoordinatesAbsolute()
    {
        const core::Document document = test::simpleDocument();
        const auto bundle = decodeCells(encodeCells(*document.page(0), {"c2"}));
        QVERIFY(bundle);
        const core::Cell& c2 = cellOf(*bundle, "c2");
        QCOMPARE(c2.parent(), std::string()); // top level of the payload
        QCOMPARE(c2.geometry()->x(), 160.0);  // 40 (group) + 120
        QCOMPARE(c2.geometry()->y(), 260.0);
    }

    void encodeReplacesOutsideTerminalsByPoints()
    {
        const core::Document document = test::simpleDocument();
        const auto bundle = decodeCells(encodeCells(*document.page(0), {"e"}));
        QVERIFY(bundle);
        const core::Cell& e = cellOf(*bundle, "e");
        QVERIFY(e.source().empty() && e.target().empty());
        QCOMPARE(*e.geometry()->sourcePoint(), (core::Point{100, 70})); // centre of "a"
        QCOMPARE(*e.geometry()->targetPoint(), (core::Point{340, 70})); // centre of "b"
    }

    void encodeSkipsLayersAndTheRoot()
    {
        const core::Document document = test::simpleDocument();
        const auto bundle = decodeCells(encodeCells(*document.page(0), {"0", "1"}));
        QVERIFY(bundle);
        QVERIFY(bundle->empty());
    }

    void decodeReplacesNonFiniteCoordinatesOnly()
    {
        const std::string xml =
            R"(<mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/>)"
            R"(<mxCell id="v" value="" vertex="1" parent="1"><mxGeometry x="nan" y="inf" width="-inf" height="10" as="geometry"/></mxCell>)"
            R"(<mxCell id="e" edge="1" parent="1" source="v" target="v"><mxGeometry relative="1" as="geometry">)"
            R"(<mxPoint x="NaN" y="5" as="sourcePoint"/><Array as="points"><mxPoint x="1.5" y="-inf"/></Array>)"
            R"(</mxGeometry></mxCell></root></mxGraphModel>)";
        const auto bundle = decodeCells(xml);
        QVERIFY(bundle);

        const core::Geometry& vertex = *cellOf(*bundle, "v").geometry();
        QCOMPARE(vertex.x(), 0.0);
        QCOMPARE(vertex.y(), 0.0);
        QCOMPARE(vertex.width(), 0.0);
        QCOMPARE(vertex.height(), 10.0);
        QCOMPARE(vertex.attributes()[0].name, std::string("x")); // attribute order kept
        QCOMPARE(vertex.attributes().value("x"), std::string("0"));

        const core::Geometry& edge = *cellOf(*bundle, "e").geometry();
        QCOMPARE(edge.sourcePoint(), (std::optional<core::Point>{core::Point{0, 5}}));
        QCOMPARE(edge.children()[0].attributes.value("x"), std::string("0"));
        QCOMPARE(edge.children()[0].attributes.value("y"), std::string("5")); // finite: as written
        QCOMPARE(edge.children()[1].children[0].attributes.value("x"), std::string("1.5"));
        QCOMPARE(edge.children()[1].children[0].attributes.value("y"), std::string("0"));
    }

    void decodeAcceptsUriEncodedAndFiles()
    {
        const core::Document document = test::simpleDocument();
        const std::string xml = encodeCells(*document.page(0), {"a", "b"});

        const QByteArray encoded = QUrl::toPercentEncoding(QString::fromStdString(xml));
        QVERIFY(encoded.startsWith("%3C"));
        const auto uri = decodeCells(std::string_view(encoded.constData(), size_t(encoded.size())));
        QVERIFY(uri);
        QCOMPARE(idsOf(*uri), (std::vector<std::string>{"a", "b"}));

        // An <mxfile>, compressed: the first page is used.
        format::WriteOptions options;
        options.pageEncoding = format::PageEncodingPolicy::Compressed;
        const QByteArray file = format::writeDocument(document, options);
        QVERIFY(!file.contains("<mxGraphModel"));
        const auto fromFile = decodeCells(std::string_view(file.constData(), size_t(file.size())));
        QVERIFY(fromFile);
        QCOMPARE(idsOf(*fromFile), (std::vector<std::string>{"a", "b", "e", "g", "c1", "c2"}));

        QVERIFY(!decodeCells("just some text"));
        QVERIFY(!decodeCells("<html><body/></html>"));
        QVERIFY(!decodeCells("<mxGraphModel><root"));
    }

    void insertGivesFreshIdsAndRemapsReferences()
    {
        DocumentSession session(test::simpleDocument());
        const auto bundle = decodeCells(encodeCells(*session.currentPage(), {"a", "b", "e", "g"}));
        QVERIFY(bundle);

        const commands::Result result = session.edit(QStringLiteral("Insert"), [&](commands::Transaction& tx) {
            return insertBundle(tx, 0, *bundle, "1", 100, 50);
        });
        QVERIFY(result.ok);
        QCOMPARE(result.cells.size(), std::size_t(4)); // only the top-level cells
        const core::Page& page = *session.currentPage();
        QCOMPARE(page.cellCount(), std::size_t(2 + 6 + 6));

        const core::Cell* a = page.findCell(result.cells[0]);
        const core::Cell* edge = page.findCell(result.cells[2]);
        const core::Cell* group = page.findCell(result.cells[3]);
        QVERIFY(a && edge && group);
        QVERIFY(a->id() != "a");
        QCOMPARE(a->geometry()->x(), 140.0);
        QCOMPARE(a->geometry()->y(), 90.0);
        QCOMPARE(edge->source(), a->id());       // remapped to the clone
        QCOMPARE(edge->target(), result.cells[1]);
        QCOMPARE(group->geometry()->x(), 140.0);
        const auto children = page.children(group->id());
        QCOMPARE(children.size(), std::size_t(2));
        QCOMPARE(children[0]->geometry()->x(), 0.0); // group-relative stays
        QCOMPARE(session.selection().cells(), result.cells);

        QVERIFY(session.undo());
        QCOMPARE(session.currentPage()->cellCount(), std::size_t(2 + 6));
    }

    void insertConvertsAbsoluteToParentCoordinates()
    {
        DocumentSession session(test::simpleDocument());
        const auto bundle = decodeCells(encodeCells(*session.currentPage(), {"a"}));
        const commands::Result result = session.edit(QStringLiteral("Insert"), [&](commands::Transaction& tx) {
            return insertBundle(tx, 0, *bundle, "g", 0, 0); // into the group at (40,200)
        });
        QVERIFY(result.ok);
        const core::Cell* clone = session.currentPage()->findCell(result.cells.front());
        QCOMPARE(clone->parent(), std::string("g"));
        QCOMPARE(clone->geometry()->x(), 0.0); // absolute 40 minus the group's 40
        QCOMPARE(clone->geometry()->y(), -160.0);
    }

    void insertKeepsEdgesThatPrecedeTheirTerminals()
    {
        DocumentSession session(test::simpleDocument());
        QVERIFY(session.edit(QStringLiteral("Back"), [](commands::Transaction& tx) {
                       return commands::ops::orderCells(tx, 0, {"e"}, commands::ops::ZOrder::ToBack);
                   }).ok);
        const auto bundle = decodeCells(encodeCells(*session.currentPage(), {"a", "b", "e"}));
        QVERIFY(bundle);
        QCOMPARE(idsOf(*bundle).front(), std::string("e")); // the edge is first in z-order

        const commands::Result result = session.edit(QStringLiteral("Insert"), [&](commands::Transaction& tx) {
            return insertBundle(tx, 0, *bundle, "1", 10, 10);
        });
        QVERIFY2(result.ok, result.error.c_str());
        const core::Cell* edge = session.currentPage()->findCell(result.cells[0]);
        QVERIFY(edge->isEdge());
        QVERIFY(!edge->source().empty());
        QVERIFY(session.currentPage()->findCell(edge->source()) != nullptr);
        QVERIFY(edge->source() != "a");
    }

    void insertDropsTerminalsOutsideTheBundle()
    {
        DocumentSession session(test::simpleDocument());
        // Hand-written payload whose edge points at a cell that is not in it.
        const auto bundle = decodeCells(
            R"(<mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/><mxCell id="x" edge="1" parent="1" source="missing"><mxGeometry relative="1" as="geometry"/></mxCell></root></mxGraphModel>)");
        QVERIFY(bundle);
        const commands::Result result = session.edit(QStringLiteral("Insert"), [&](commands::Transaction& tx) {
            return insertBundle(tx, 0, *bundle, "1", 0, 0);
        });
        QVERIFY(result.ok);
        QVERIFY(session.currentPage()->findCell(result.cells[0])->source().empty());
    }

    void mimeDataCarriesBothFlavours()
    {
        const core::Document document = test::simpleDocument();
        const std::string xml = encodeCells(*document.page(0), {"b"});
        QMimeData mime;
        setCellsMimeData(mime, xml);
        QVERIFY(mime.hasFormat(QString::fromLatin1(kCellsMimeType)));
        QCOMPARE(mime.text().toStdString(), xml);
        QVERIFY(cellsFromMimeData(mime));

        QMimeData textOnly; // another application's clipboard: the plain text flavour decodes too
        textOnly.setText(QString::fromStdString(xml));
        const auto bundle = cellsFromMimeData(textOnly);
        QVERIFY(bundle);
        QCOMPARE(idsOf(*bundle), (std::vector<std::string>{"b"}));

        QMimeData words;
        words.setText(QStringLiteral("hello"));
        QVERIFY(!cellsFromMimeData(words));
    }
};

QTEST_MAIN(TestSessionTransfer)
#include "tst_session_transfer.moc"
