// Packed stencil decoder (src/render/stencil_pack.cpp) against the vendored
// resources and a fixture produced by the Python reference decoder
// (tools/stencils/dump_shape.py).

#include "qraw/format/compression.h"
#include "qraw/format/xml_io.h"
#include "qraw/render/stencil.h"

#include <QFile>
#include <QTest>

#include <map>
#include <string>

using namespace qraw;

namespace {

// stencils.bin: magic, u32be count, then (u16be len, path, u32be len, blob)*.
std::map<std::string, QByteArray> readPack()
{
    std::map<std::string, QByteArray> sets;
    QFile file(QStringLiteral(":/qraw/stencils/stencils.bin"));
    if (!file.open(QIODevice::ReadOnly))
        return sets;
    const QByteArray data = file.readAll();
    const auto u32 = [&](qsizetype pos) {
        return (quint32(quint8(data[pos])) << 24) | (quint32(quint8(data[pos + 1])) << 16) |
               (quint32(quint8(data[pos + 2])) << 8) | quint32(quint8(data[pos + 3]));
    };
    qsizetype pos = 12;
    const quint32 count = u32(8);
    for (quint32 i = 0; i < count; ++i) {
        const int pathLength = (quint8(data[pos]) << 8) | quint8(data[pos + 1]);
        pos += 2;
        const std::string path(data.constData() + pos, pathLength);
        pos += pathLength;
        const qsizetype blobLength = u32(pos);
        pos += 4;
        sets[path] = data.mid(pos, blobLength);
        pos += blobLength;
    }
    return sets;
}

std::optional<std::string> inflateSet(const QByteArray& blob)
{
    return format::inflateBytes(std::string_view(blob.constData(), static_cast<std::size_t>(blob.size())), -15);
}

int indexedShapes(const std::string& setPath)
{
    QFile file(QStringLiteral(":/qraw/stencils/index.tsv"));
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    int count = 0;
    for (const QByteArray& line : file.readAll().split('\n'))
        if (line.endsWith('\t' + QByteArray::fromStdString(setPath)))
            ++count;
    return count;
}

} // namespace

class TstStencilPack : public QObject {
    Q_OBJECT
private slots:
    void flowchartSet();
    void firstShapeMatchesReferenceDecoder();
    void everySetDecodesToTheIndexedShapeCount();
    void malformedInputIsRejected();
};

void TstStencilPack::flowchartSet()
{
    const auto sets = readPack();
    QCOMPARE(sets.size(), std::size_t(204));
    const auto it = sets.find("flowchart.xml");
    QVERIFY(it != sets.end());
    const auto inflated = inflateSet(it->second);
    QVERIFY(inflated);

    const auto root = render::decodePackedStencilSet(QByteArrayView(inflated->data(), qsizetype(inflated->size())));
    QVERIFY(root);
    QCOMPARE(root->name, std::string("shapes"));
    QCOMPARE(root->attributes.value("name"), std::string("mxGraph.flowchart"));
    QVERIFY(root->attributes.size() == 1);
}

void TstStencilPack::firstShapeMatchesReferenceDecoder()
{
    const auto sets = readPack();
    const auto inflated = inflateSet(sets.at("flowchart.xml"));
    QVERIFY(inflated);
    const auto root = render::decodePackedStencilSet(QByteArrayView(inflated->data(), qsizetype(inflated->size())));
    QVERIFY(root && !root->children.empty());

    QFile fixture(QStringLiteral(QRAW_TEST_DATA_DIR "/stencils/flowchart_annotation_1.xml"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const auto expected = format::parseXml(fixture.readAll());
    QVERIFY(expected.document);

    const core::XmlNode& first = root->children.front();
    QCOMPARE(first.attributes.value("name"), std::string("Annotation 1"));
    // Same names, same attribute order and values, same children.
    QVERIFY(first == expected.document->root);
}

void TstStencilPack::everySetDecodesToTheIndexedShapeCount()
{
    int total = 0;
    for (const auto& [path, blob] : readPack()) {
        const auto inflated = inflateSet(blob);
        QVERIFY2(inflated, path.c_str());
        const auto root =
            render::decodePackedStencilSet(QByteArrayView(inflated->data(), qsizetype(inflated->size())));
        QVERIFY2(root, path.c_str());
        int shapes = 0;
        for (const core::XmlNode& child : root->children)
            shapes += child.isElement("shape");
        total += shapes;
        // Sets with duplicate keys have fewer index entries; none of the
        // vendored sets has more entries than shapes.
        QVERIFY2(indexedShapes(path) <= shapes, path.c_str());
    }
    QCOMPARE(total, 9090);
}

void TstStencilPack::malformedInputIsRejected()
{
    const auto inflated = inflateSet(readPack().at("flowchart.xml"));
    QVERIFY(inflated);
    const std::string& good = *inflated;
    const auto decode = [](const std::string& bytes) {
        return render::decodePackedStencilSet(QByteArrayView(bytes.data(), qsizetype(bytes.size())));
    };

    // Truncated at the start, inside the header, and inside each stream.
    for (const std::size_t length : {std::size_t(0), std::size_t(7), std::size_t(19), std::size_t(20),
                                     std::size_t(21), good.size() / 4, good.size() / 2, good.size() - 1})
        QVERIFY2(!decode(good.substr(0, length)), std::to_string(length).c_str());

    // Header streams longer than the data.
    std::string badHeader = good;
    badHeader[0] = char(0x7f);
    QVERIFY(!decode(badHeader));

    // Single damaged bytes must never crash or read out of bounds (the
    // result may be a decoded tree or nullopt).
    for (std::size_t i = 0; i < std::min<std::size_t>(good.size(), 3000); i += 5) {
        std::string damaged = good;
        damaged[i] = char(damaged[i] ^ 0xFF);
        (void)decode(damaged);
    }
    // Ops stream without any element.
    QVERIFY(!decode(std::string(20, '\0')));
}

QTEST_MAIN(TstStencilPack)
#include "tst_stencil_pack.moc"
