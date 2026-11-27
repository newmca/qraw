#include "qraw/commands/ops_pages.h"

#include "qraw/commands/primitives.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/attributes.h"
#include "qraw/core/cell.h"
#include "qraw/core/document.h"
#include "qraw/core/page.h"

#include <algorithm>
#include <random>
#include <string>

namespace qraw::commands::ops {

namespace {

// Entry index in the document of the page at `index`, nullopt when absent.
std::optional<std::size_t> entryOfPage(const core::Document& doc, std::size_t index)
{
    if (index >= doc.pageCount())
        return std::nullopt;
    return doc.pageEntryIndex(index);
}

// Brings <mxfile pages="N"> (only when the file has the attribute) and the
// document kind in line with the page count. A bare model becomes an
// <mxfile> when a page is added, because it can hold only one page.
bool syncHeader(Transaction& tx, std::size_t pageCountAfter, bool addingPage)
{
    DocumentHeader header = DocumentHeader::of(tx.document());
    if (addingPage && header.kind == core::DocumentKind::BareModel)
        header.kind = core::DocumentKind::MxFile;
    if (header.fileAttributes.has("pages"))
        header.fileAttributes.set("pages", std::to_string(pageCountAfter));
    if (header == DocumentHeader::of(tx.document()))
        return true;
    return tx.emplace<ReplaceDocumentHeader>(std::move(header));
}

// Model attributes of a new page, in draw.io's canonical order.
void setDefaultModelAttributes(core::Page& page)
{
    core::Attributes& model = page.modelAttributes();
    model.set("grid", "1");
    model.set("gridSize", "10");
    model.set("guides", "1");
    model.set("tooltips", "1");
    model.set("connect", "1");
    model.set("arrows", "1");
    model.set("fold", "1");
    model.set("page", "1");
    model.set("pageScale", "1");
    model.set("pageWidth", "827");
    model.set("pageHeight", "1169");
    model.set("math", "0");
    model.set("shadow", "0");
}

} // namespace

std::string randomPageId()
{
    static constexpr std::string_view kAlphabet =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_";
    static_assert(kAlphabet.size() == 64);
    static thread_local std::mt19937 engine{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> pick(0, kAlphabet.size() - 1);
    std::string id;
    id.reserve(20);
    for (int i = 0; i < 20; ++i)
        id.push_back(kAlphabet[pick(engine)]);
    return id;
}

std::string nextPageName(const core::Document& document)
{
    const std::vector<const core::Page*> pages = document.pages();
    std::size_t n = document.pageCount();
    std::string name;
    bool taken = true;
    while (taken) {
        ++n;
        name = "Page-" + std::to_string(n);
        taken = std::any_of(pages.begin(), pages.end(), [&name](const core::Page* p) { return p->name() == name; });
    }
    return name;
}

core::Page makeEmptyPage(std::string id, std::string name, bool compressed)
{
    core::Page page;
    page.setId(id);
    page.setName(name);
    page.setEncoding(compressed ? core::PageEncoding::Compressed : core::PageEncoding::Inline);
    setDefaultModelAttributes(page);
    page.appendEntry(core::Cell("0"));
    page.appendEntry(core::Cell::makeLayer("1", "0"));
    return page;
}

Result addPage(Transaction& tx, std::optional<std::size_t> index, std::optional<std::string> name,
               const PageIdGenerator& newId)
{
    const core::Document& doc = tx.document();
    const std::size_t count = doc.pageCount();
    const bool inMiddle = index && *index < count;

    std::size_t entry = doc.entries().size();
    if (inMiddle) {
        entry = *doc.pageEntryIndex(*index);
    } else if (count > 0) {
        entry = *doc.pageEntryIndex(count - 1) + 1;
    }

    const bool compressed = doc.fileAttributes().value("compressed") == "true";
    const std::string pageName = name ? *name : nextPageName(doc);
    if (!syncHeader(tx, count + 1, true))
        return Result::failure("cannot update document header");
    if (!tx.emplace<InsertPage>(entry, makeEmptyPage(newId(), pageName, compressed)))
        return Result::failure("cannot insert page");

    Result r = Result::success();
    r.page = inMiddle ? *index : count;
    return r;
}

Result removePage(Transaction& tx, std::size_t index)
{
    const std::size_t count = tx.document().pageCount();
    if (index >= count)
        return Result::failure("no such page");
    if (count <= 1)
        return Result::failure("cannot remove the only page");
    if (!tx.emplace<RemovePage>(index))
        return Result::failure("cannot remove page");
    if (!syncHeader(tx, count - 1, false))
        return Result::failure("cannot update document header");

    Result r = Result::success();
    r.page = index > 0 ? index - 1 : 0;
    return r;
}

Result duplicatePage(Transaction& tx, std::size_t index, const PageIdGenerator& newId)
{
    const core::Document& doc = tx.document();
    const core::Page* original = tx.page(index);
    if (!original)
        return Result::failure("no such page");

    core::Page copy = *original;
    copy.setId(newId());
    copy.setName("Copy of " + original->name());

    const std::size_t count = doc.pageCount();
    const std::size_t entry = *entryOfPage(doc, index) + 1;
    if (!syncHeader(tx, count + 1, true))
        return Result::failure("cannot update document header");
    if (!tx.emplace<InsertPage>(entry, std::move(copy)))
        return Result::failure("cannot insert page copy");

    Result r = Result::success();
    r.page = index + 1;
    return r;
}

Result renamePage(Transaction& tx, std::size_t index, std::string_view name)
{
    const core::Page* page = tx.page(index);
    if (!page)
        return Result::failure("no such page");

    PageAttributes attributes = PageAttributes::of(*page);
    if (attributes.diagram.value("name") == name)
        return Result::success();
    attributes.diagram.setOrdered("name", name, {"id", "name"});
    if (!tx.emplace<ReplacePageAttributes>(index, std::move(attributes)))
        return Result::failure("cannot rename page");
    return Result::success();
}

Result replacePageModel(Transaction& tx, std::size_t index, core::Page model)
{
    const core::Page* page = tx.page(index);
    if (!page)
        return Result::failure("no such page");
    if (model.cellCount() == 0)
        return Result::failure("the model has no cells");

    core::Page next = std::move(model);
    next.diagramAttributes() = page->diagramAttributes();
    next.setEncoding(page->encoding());
    next.payloadFrame() = page->payloadFrame();
    next.setHasModel(true);
    if (next.encoding() == core::PageEncoding::Opaque)
        next.setEncoding(core::PageEncoding::Inline); // the page now has a model of its own
    else
        next.diagramForeign() = page->diagramForeign(); // an opaque payload is not kept next to a model
    if (!tx.emplace<ReplacePage>(index, std::move(next)))
        return Result::failure("cannot replace page");

    Result r = Result::success();
    r.page = index;
    return r;
}

Result movePage(Transaction& tx, std::size_t from, std::size_t to)
{
    const core::Document& doc = tx.document();
    const std::size_t count = doc.pageCount();
    if (from >= count)
        return Result::failure("no such page");

    const std::size_t target = std::min(to, count - 1);
    Result r = Result::success();
    r.page = target;
    if (from == target)
        return r;

    const std::size_t before = target > from ? *doc.pageEntryIndex(target) + 1 : *doc.pageEntryIndex(target);
    if (!tx.emplace<MovePage>(from, before))
        return Result::failure("cannot move page");
    return r;
}

Result setPageProperties(Transaction& tx, std::size_t index, const PageProperties& properties)
{
    const core::Page* page = tx.page(index);
    if (!page)
        return Result::failure("no such page");
    if (properties.pageWidth.has_value() != properties.pageHeight.has_value())
        return Result::failure("page width and height must be set together");

    // The typed setters of core::Page are used on a scratch copy; only the
    // resulting attributes are written.
    core::Page scratch = *page;
    if (properties.grid)
        scratch.setGridEnabled(*properties.grid);
    if (properties.gridSize)
        scratch.setGridSize(*properties.gridSize);
    if (properties.pageVisible)
        scratch.setPageVisible(*properties.pageVisible);
    if (properties.pageScale)
        scratch.setPageScale(*properties.pageScale);
    if (properties.pageWidth && properties.pageHeight)
        scratch.setPageSize(*properties.pageWidth, *properties.pageHeight);
    if (properties.background) {
        // draw.io writes "none" for no background; core::Page removes the attribute for "".
        const bool none = *properties.background == "none";
        scratch.setBackground(none ? std::string_view{} : std::string_view{*properties.background});
    }
    if (properties.math)
        scratch.setMath(*properties.math);
    if (properties.shadow)
        scratch.setShadow(*properties.shadow);
    if (properties.adaptiveColors)
        scratch.setAdaptiveColors(*properties.adaptiveColors);

    PageAttributes after{scratch.diagramAttributes(), scratch.modelAttributes()};
    if (after == PageAttributes::of(*page))
        return Result::success();
    if (!tx.emplace<ReplacePageAttributes>(index, std::move(after)))
        return Result::failure("cannot change page properties");
    return Result::success();
}

} // namespace qraw::commands::ops
