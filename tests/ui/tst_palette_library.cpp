// Tests of the palette data and loader: the generated
// resources/palettes/*.json, PaletteLibrary, PaletteEntry::cellsXml().

#include "qraw/format/codec.h"
#include "qraw/ui/palette_library.h"

#include <QFile>
#include <QSet>
#include <QtTest>

#include <algorithm>

using namespace qraw;
using namespace qraw::ui;

namespace {

format::ReadResult parseXml(const std::string& xml)
{
    return format::readDocument(QByteArrayView(xml.data(), static_cast<qsizetype>(xml.size())));
}

const PaletteEntry* findEntry(const PaletteLibrary& library, const QString& paletteId, const QString& entryId)
{
    return library.entry(paletteId + QLatin1Char('/') + entryId);
}

} // namespace

class TstPaletteLibrary : public QObject {
    Q_OBJECT

private slots:
    void generatedDataLoadsWithoutErrors()
    {
        PaletteLibrary library;
        const PaletteLoadReport report = library.loadDirectory(QStringLiteral(":/qraw/palettes"));
        QVERIFY2(report.errors.isEmpty(), qPrintable(report.errors.join('\n')));
        QCOMPARE(report.palettes, static_cast<int>(library.palettes().size()));
        QCOMPARE(static_cast<std::size_t>(report.entries), library.entryCount());
        QVERIFY(library.entryCount() > 3000);

        for (const char* id : {"general", "misc", "advanced", "basic", "arrows", "arrows2", "flowchart", "tables",
                               "containers", "uml", "er", "network", "kubernetes", "floorplan", "bpmn2Tasks"})
            QVERIFY2(library.palette(QString::fromLatin1(id)) != nullptr, id);
        for (const char* lib : {"aws4", "azure2", "gcp2", "mockups"}) {
            const auto it = std::find_if(library.palettes().begin(), library.palettes().end(),
                                         [&](const Palette& p) { return p.library == QLatin1String(lib); });
            QVERIFY2(it != library.palettes().end(), lib);
        }

        const PaletteEntry* rectangle = findEntry(library, QStringLiteral("general"), QStringLiteral("rectangle"));
        QVERIFY(rectangle != nullptr);
        QCOMPARE(rectangle->title, QStringLiteral("Rectangle"));
        QCOMPARE(rectangle->kind, PaletteEntry::Kind::Vertex);
        QCOMPARE(rectangle->width, 120.0);
        QCOMPARE(rectangle->height, 60.0);
        QVERIFY(rectangle->tags.contains(QStringLiteral("box")));
        QCOMPARE(rectangle->key(QStringLiteral("general")), QStringLiteral("general/rectangle"));
    }

    void palettesAreOrderedByOrderThenId()
    {
        const auto& palettes = PaletteLibrary::builtin().palettes();
        QVERIFY(std::is_sorted(palettes.begin(), palettes.end(), [](const Palette& a, const Palette& b) {
            return a.order != b.order ? a.order < b.order : a.id < b.id;
        }));
        QCOMPARE(palettes.front().id, QStringLiteral("general"));
    }

    void everyEntryParsesAsCells()
    {
        for (const Palette& palette : PaletteLibrary::builtin().palettes()) {
            QSet<QString> ids;
            for (const PaletteEntry& entry : palette.entries) {
                QVERIFY2(!ids.contains(entry.id), qPrintable(palette.id + "/" + entry.id));
                ids.insert(entry.id);
                const format::ReadResult read = parseXml(entry.cellsXml());
                QVERIFY2(read.ok() && read.document->pageCount() == 1, qPrintable(entry.key(palette.id)));
            }
        }
    }

    void searchFindsGeneralAndAwsEntries()
    {
        const PaletteLibrary& library = PaletteLibrary::builtin();
        const auto ellipse = library.search(QStringLiteral("ellipse"));
        QVERIFY(!ellipse.empty());
        QVERIFY(std::any_of(ellipse.begin(), ellipse.end(), [](const PaletteLibrary::SearchHit& hit) {
            return hit.palette->id == QLatin1String("general") && hit.entry->title == QLatin1String("Ellipse");
        }));

        const auto lambda = library.search(QStringLiteral("aws lambda"));
        QVERIFY(!lambda.empty());
        QVERIFY(std::any_of(lambda.begin(), lambda.end(), [](const PaletteLibrary::SearchHit& hit) {
            return hit.palette->library == QLatin1String("aws4") && hit.entry->title.contains(QLatin1String("Lambda"));
        }));
    }

    void searchRulesAndLimit()
    {
        const PaletteLibrary& library = PaletteLibrary::builtin();
        QVERIFY(library.search(QString()).empty());
        QVERIFY(library.search(QStringLiteral("   ")).empty());
        QVERIFY(library.search(QStringLiteral("zzzzqqqq")).empty());
        QCOMPARE(library.search(QStringLiteral("arrow"), 5).size(), std::size_t{5});
        // Case-insensitive; every term must match.
        QCOMPARE(library.search(QStringLiteral("ELLIPSE")).size(), library.search(QStringLiteral("ellipse")).size());
        QVERIFY(library.search(QStringLiteral("ellipse zzzzqqqq")).empty());
    }

    void searchListsCopiedEntriesOnce()
    {
        // Tables and Containers copy entries of General/Misc/Advanced.
        const PaletteLibrary& library = PaletteLibrary::builtin();
        QSet<QString> shapes;
        for (const auto& hit : library.search(QStringLiteral("container"))) {
            const QString content = hit.entry->title + hit.entry->style + hit.entry->xml;
            QVERIFY2(!shapes.contains(content), qPrintable(hit.entry->title));
            shapes.insert(content);
        }
        QVERIFY(!shapes.isEmpty());
    }

    void derivedPalettesCopyEntries()
    {
        const Palette* tables = PaletteLibrary::builtin().palette(QStringLiteral("tables"));
        QVERIFY(tables != nullptr && !tables->entries.empty());
        for (const PaletteEntry& entry : tables->entries) {
            const QString source = entry.kind == PaletteEntry::Kind::Cells ? entry.xml : entry.style;
            QVERIFY2(source.contains(QLatin1String("tableLayout")) || source.contains(QLatin1String("shape=table"))
                         || source.contains(QLatin1String("tableRow")),
                     qPrintable(entry.title));
        }
    }

    void vertexAndEdgeEntriesBecomeCells()
    {
        PaletteEntry vertex;
        vertex.id = QStringLiteral("v");
        vertex.kind = PaletteEntry::Kind::Vertex;
        vertex.style = QStringLiteral("rounded=1;html=1;");
        vertex.value = QStringLiteral("a<b & \"c\"\nd");
        vertex.width = 120.5;
        vertex.height = 60;
        const std::string vertexXml = vertex.cellsXml();
        QVERIFY(vertexXml.find("a&lt;b &amp; &quot;c&quot;&#xa;d") != std::string::npos);
        QVERIFY(vertexXml.find("width=\"120.5\" height=\"60\"") != std::string::npos);
        const format::ReadResult vertexRead = parseXml(vertexXml);
        QVERIFY(vertexRead.ok());
        const core::Cell* cell = vertexRead.document->page(0)->findCell("2");
        QVERIFY(cell != nullptr);
        QVERIFY(cell->isVertex());
        QCOMPARE(QString::fromStdString(cell->value()), QStringLiteral("a<b & \"c\"\nd"));

        PaletteEntry edge;
        edge.id = QStringLiteral("e");
        edge.kind = PaletteEntry::Kind::Edge;
        edge.style = QStringLiteral("endArrow=classic;");
        edge.width = 50;
        edge.height = 30;
        const std::string edgeXml = edge.cellsXml();
        QVERIFY(edgeXml.find("<mxPoint y=\"30\" as=\"sourcePoint\"/><mxPoint x=\"50\" as=\"targetPoint\"/>")
                != std::string::npos);
        const format::ReadResult edgeRead = parseXml(edgeXml);
        QVERIFY(edgeRead.ok());
        const core::Cell* edgeCell = edgeRead.document->page(0)->findCell("2");
        QVERIFY(edgeCell != nullptr);
        QVERIFY(edgeCell->isEdge());
    }

    void loaderToleratesMalformedEntries()
    {
        QFile file(QStringLiteral(QRAW_TEST_DATA_DIR "/m5-palette/malformed.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));

        PaletteLibrary library;
        PaletteLoadReport report;
        library.loadJson(file.readAll(), QStringLiteral("malformed.json"), &report);

        const Palette* fixture = library.palette(QStringLiteral("fx"));
        QVERIFY(fixture != nullptr);
        QStringList ids;
        for (const PaletteEntry& entry : fixture->entries)
            ids.push_back(entry.id);
        QCOMPARE(ids, (QStringList{QStringLiteral("box"), QStringLiteral("wire"), QStringLiteral("pair")}));
        QVERIFY(library.palette(QStringLiteral("early")) != nullptr);
        // 6 bad entries (no id, no style, no size, unknown kind, empty cells, not an object),
        // a duplicate entry id, a palette without id and a duplicate palette id.
        QCOMPARE(report.errors.size(), 9);
        QCOMPARE(report.palettes, 2);
        QCOMPARE(report.entries, 3);
        // order 1 sorts before order 5
        QCOMPARE(library.palettes().front().id, QStringLiteral("early"));
        QVERIFY(library.entry(QStringLiteral("fx/wire")) != nullptr);
        QVERIFY(library.entry(QStringLiteral("fx/nope")) == nullptr);
        QVERIFY(library.entry(QStringLiteral("nope/wire")) == nullptr);
        QVERIFY(library.entry(QStringLiteral("nokey")) == nullptr);
        QVERIFY(!library.entry(QStringLiteral("fx/pair"))->showLabel);
        QVERIFY(library.entry(QStringLiteral("fx/box"))->showLabel);
    }

    void loaderRejectsOtherSchemasAndGarbage()
    {
        PaletteLibrary library;
        PaletteLoadReport report;
        library.loadJson(R"({"schema": 2, "palettes": [{"id": "x", "entries": []}]})", QStringLiteral("v2"), &report);
        library.loadJson("{not json", QStringLiteral("garbage"), &report);
        library.loadJson("[]", QStringLiteral("array"), &report);
        QCOMPARE(report.errors.size(), 3);
        QVERIFY(report.errors.front().startsWith(QStringLiteral("v2: ")));
        QVERIFY(library.palettes().empty());

        const PaletteLoadReport missing = library.loadDirectory(QStringLiteral(":/qraw/no-such-directory"));
        QCOMPARE(missing.errors.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TstPaletteLibrary)
#include "tst_palette_library.moc"
