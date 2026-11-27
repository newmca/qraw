// StencilRegistry: the vendored index, lazy set decoding, inline shapes,
// addSet and concurrent first use.

#include "qraw/format/compression.h"
#include "qraw/format/xml_io.h"
#include "qraw/render/stencil.h"

#include <QTest>

#include <string>
#include <thread>
#include <vector>

using namespace qraw;

namespace {

const std::string kShapeXml =
    "<shape name=\"inline one\" w=\"10\" h=\"20\" aspect=\"fixed\"><foreground><path><move x=\"0\" y=\"0\"/>"
    "<line x=\"10\" y=\"20\"/></path><stroke/></foreground></shape>";

} // namespace

class TstStencilRegistry : public QObject {
    Q_OBJECT
private slots:
    void builtinIndex();
    void findIsCaseInsensitiveAndCached();
    void unknownNames();
    void inlineStencils();
    void addSet();
    void concurrentFirstUse();
};

void TstStencilRegistry::builtinIndex()
{
    const render::StencilRegistry& registry = render::StencilRegistry::builtin();
    QVERIFY(registry.indexedCount() > 9000);
    QVERIFY(registry.indexedCount() < 9200);
    QVERIFY(registry.contains("mxgraph.flowchart.process"));
    QVERIFY(registry.contains("MxGraph.Basic.4_Point_Star"));
    QVERIFY(!registry.contains("mxgraph.nope.x"));
}

void TstStencilRegistry::findIsCaseInsensitiveAndCached()
{
    render::StencilRegistry& registry = render::StencilRegistry::builtin();
    const render::Stencil* process = registry.find("mxgraph.flowchart.process");
    QVERIFY(process);
    QCOMPARE(process->name(), std::string("Process"));
    QCOMPARE(registry.find("mxgraph.flowchart.process"), process);
    QCOMPARE(registry.find("MXGRAPH.Flowchart.PROCESS"), process);

    // Another shape of the same set comes from the same decoded set.
    const render::Stencil* star = registry.find("mxgraph.basic.4_point_star");
    QVERIFY(star);
    QCOMPARE(star->name(), std::string("4 Point Star"));
    QVERIFY(star->width() > 0 && star->height() > 0);
    QVERIFY(!star->connectionConstraints(render::ResolvedStyle(), star->width(), star->height()).empty());
}

void TstStencilRegistry::unknownNames()
{
    render::StencilRegistry& registry = render::StencilRegistry::builtin();
    QVERIFY(registry.find("mxgraph.nope.x") == nullptr);
    QVERIFY(registry.find("") == nullptr);
    QVERIFY(registry.find("rectangle") == nullptr);
}

void TstStencilRegistry::inlineStencils()
{
    render::StencilRegistry& registry = render::StencilRegistry::builtin();

    const std::string compressed = "stencil(" + format::compressDiagram(kShapeXml) + ")";
    const render::Stencil* a = registry.findInline(compressed);
    QVERIFY(a);
    QCOMPARE(a->name(), std::string("inline one"));
    QCOMPARE(a->width(), 10.0);
    QCOMPARE(a->height(), 20.0);
    QVERIFY(a->fixedAspect());
    QCOMPARE(registry.findInline(compressed), a); // cached by value

    // Plain URI-encoded XML, raw XML and a <shapes> wrapper are accepted too.
    QVERIFY(registry.findInline("stencil(" + format::encodeUriComponent(kShapeXml) + ")"));
    QVERIFY(registry.findInline("stencil(" + kShapeXml + ")"));
    QVERIFY(registry.findInline("stencil(" + format::compressDiagram("<shapes name=\"x\">" + kShapeXml + "</shapes>") + ")"));

    // Garbage never decodes (and does not poison later lookups).
    QVERIFY(registry.findInline("stencil(!!! not a stencil)") == nullptr);
    QVERIFY(registry.findInline("stencil()") == nullptr);
    QVERIFY(registry.findInline("stencil(" + format::compressDiagram("<mxCell/>") + ")") == nullptr);
    QVERIFY(registry.findInline("") == nullptr);
    QCOMPARE(registry.findInline(compressed), a);
}

void TstStencilRegistry::addSet()
{
    render::StencilRegistry registry; // empty, independent of the builtin one
    QCOMPARE(registry.indexedCount(), std::size_t(0));
    QVERIFY(registry.find("mxgraph.flowchart.process") == nullptr);

    const auto parsed = format::parseXml(QByteArray::fromStdString(
        "<shapes name=\"MyLib\"><shape name=\"Big Box\" w=\"40\" h=\"30\"/><shape name=\"Small\"/></shapes>"));
    QVERIFY(parsed.document);
    registry.addSet(parsed.document->root);
    QCOMPARE(registry.indexedCount(), std::size_t(2));
    QVERIFY(registry.contains("mylib.big_box"));
    const render::Stencil* box = registry.find("MyLib.Big_Box");
    QVERIFY(box);
    QCOMPARE(box->width(), 40.0);

    // Existing keys are replaced; earlier pointers stay valid.
    const auto replacement = format::parseXml(QByteArray::fromStdString(
        "<shapes name=\"MyLib\"><shape name=\"Big Box\" w=\"50\" h=\"30\"/></shapes>"));
    registry.addSet(replacement.document->root);
    QCOMPARE(box->width(), 40.0);
    QCOMPARE(registry.find("mylib.big_box")->width(), 50.0);
    QCOMPARE(registry.indexedCount(), std::size_t(2));
}

void TstStencilRegistry::concurrentFirstUse()
{
    // Keys from sets that no other test decodes; every thread asks for all of them.
    const std::vector<std::string> keys = {"mxgraph.bpmn.ad_hoc",
                                           "mxgraph.arrows.arrow_down",
                                           "mxgraph.aws3.action",
                                           "mxgraph.cabinets.auxiliary_contact_contactor_1_32a",
                                           "mxgraph.eip.aggregator"};
    constexpr int kThreads = 6;
    std::vector<std::vector<const render::Stencil*>> results(kThreads);
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
        threads.emplace_back([&, t] {
            for (const std::string& key : keys)
                results[t].push_back(render::StencilRegistry::builtin().find(key));
        });
    for (std::thread& thread : threads)
        thread.join();

    for (std::size_t i = 0; i < keys.size(); ++i) {
        QVERIFY2(results[0][i], keys[i].c_str());
        for (int t = 1; t < kThreads; ++t)
            QCOMPARE(results[t][i], results[0][i]);
    }
}

QTEST_MAIN(TstStencilRegistry)
#include "tst_stencil_registry.moc"
