// Building a page view and resolving styles with multi-megabyte `image`
// values: nothing copies them after the first parse, and the resolved style
// shares the cell's buffer.
#include "qraw/core/cell.h"
#include "qraw/core/document.h"
#include "qraw/core/page.h"
#include "qraw/core/shared_value.h"
#include "qraw/render/image_cache.h"
#include "qraw/render/page_view.h"
#include "qraw/render/stats.h"
#include "qraw/render/stylesheet.h"

#include <QTest>

using namespace qraw;
using namespace qraw::render;

namespace {

std::string imageStyle(char fill)
{
    return "shape=image;image=data:image/png," + std::string(2 * 1024 * 1024, fill) + ";shadow=1;";
}

core::Page samplePage()
{
    core::Page page;
    page.setId("p");
    page.appendEntry(core::Cell("0"));
    page.appendEntry(core::Cell::makeLayer("1", "0"));
    for (int i = 0; i < 3; ++i)
        page.appendEntry(core::Cell::makeVertex("img" + std::to_string(i), "1", "", imageStyle(static_cast<char>('a' + i)),
                                                {i * 100.0, 0, 90, 60}));
    page.appendEntry(core::Cell::makeVertex("box", "1", "Hello", "rounded=1;", {0, 200, 80, 40}));
    return page;
}

} // namespace

class TstLargeValues : public QObject {
    Q_OBJECT

private slots:
    void rebuildingThePageViewCopiesNothing()
    {
        const core::Page page = samplePage();
        stats::reset();
        const PageView first = PageView::build(page, RenderContext::defaults());
        QCOMPARE(stats::get(stats::Counter::largeValueCopies), 3u); // each image's token, parsed once
        const PageView second = PageView::build(page, RenderContext::defaults());
        const PageView third = PageView::build(page, RenderContext::defaults());
        QCOMPARE(stats::get(stats::Counter::largeValueCopies), 3u);
        QCOMPARE(third.states().size(), first.states().size());
    }

    void resolvedStyleSharesTheCellsBuffer()
    {
        const core::Page page = samplePage();
        const PageView a = PageView::build(page, RenderContext::defaults());
        const PageView b = PageView::build(page, RenderContext::defaults());
        const CellState* first = a.find("img0");
        const CellState* second = b.find("img0");
        QVERIFY(first && second);
        const core::SharedValue* x = first->style.imageValue();
        const core::SharedValue* y = second->style.imageValue();
        QVERIFY(x && y && x->isShared());
        QVERIFY(x->sameBuffer(*y));
        QCOMPARE(x->str(), page.findCell("img0")->style().findValue("image")->str());
        // Equal styles compare without scanning the megabytes.
        QVERIFY(first->style == second->style);
    }

    void aStyleResolvedTwiceFromTheSameCellKeepsItsImageHandle()
    {
        const core::Page page = samplePage();
        const core::Cell& cell = *page.findCell("img1");
        stats::reset();
        const ResolvedStyle a = Stylesheet::drawioDefault().resolve(cell.style(), CellKind::Vertex);
        const ResolvedStyle b = Stylesheet::drawioDefault().resolve(cell.style(), CellKind::Vertex);
        QVERIFY(a.imageValue()->sameBuffer(*b.imageValue()));
        QCOMPARE(stats::get(stats::Counter::largeValueCopies), 1u);
    }
};

QTEST_MAIN(TstLargeValues)
#include "tst_large_values.moc"
