#pragma once

// Page operations. Pages are addressed by index into Document::pages().
// Same conventions as ops_cells.h. The page-level code of draw.io
// (js/diagramly/Pages.js) is not part of the reference tree; behavior below
// follows draw.io Desktop as documented, with resource strings from
// $DRAWIO_SRC/src/main/webapp/resources/dia.txt
// (pageWithNumber=Page-{1}, copyOf=Copy of {1}).
//
// Every operation that changes the page count also keeps the <mxfile>
// `pages` attribute up to date *if the file has one* (newer draw.io writes
// pages="N"; it is never added), with one ReplaceDocumentHeader. Adding a
// page to a bare <mxGraphModel> document (DocumentKind::BareModel) switches
// it to MxFile in the same header change, because a bare model can hold only
// one page.

#include "qraw/commands/result.h"
#include "qraw/core/document.h"
#include "qraw/core/page.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace qraw::commands {
class Transaction;
}

namespace qraw::commands::ops {

// draw.io's Editor.guid(): 20 characters drawn uniformly from
// "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_"
// (js/diagramly/Editor.js:4609-4619), using std::random_device seeded
// std::mt19937.
[[nodiscard]] std::string randomPageId();

// Where new page ids come from. Tests pass a deterministic generator.
using PageIdGenerator = std::function<std::string()>;

// The name draw.io gives a new page (EditorUi.createPageName): "Page-N" with
// N = page count + 1, incremented while a page with that name exists.
[[nodiscard]] std::string nextPageName(const core::Document& document);

// A new, empty page as draw.io creates it: <diagram id=".." name="..">
// with a model holding the root cell "0" and the default layer "1"
// (parent "0"), and draw.io's default model attributes in canonical order:
// grid="1" gridSize="10" guides="1" tooltips="1" connect="1" arrows="1"
// fold="1" page="1" pageScale="1" pageWidth="827" pageHeight="1169"
// math="0" shadow="0". Encoding Inline unless `compressed`.
[[nodiscard]] core::Page makeEmptyPage(std::string id, std::string name, bool compressed = false);

// Inserts makeEmptyPage(newId(), name or nextPageName()) before the
// `index`-th page (end when nullopt or out of range); immediately before the
// page entry, so foreign <mxfile> children keep their place. The new page
// is compressed iff the document's <mxfile compressed="true">. Result.page =
// its index.
Result addPage(Transaction& tx, std::optional<std::size_t> index, std::optional<std::string> name,
               const PageIdGenerator& newId = randomPageId);

// Removes the page. Refuses (failure) to remove the only page: draw.io's
// page menu offers Delete only when there is more than one page.
// Result.page = the index of the page to show next (the previous one, or 0).
Result removePage(Transaction& tx, std::size_t index);

// Copies the page (model, encoding, foreign content, view attributes),
// gives it id newId() and name "Copy of <name>", and inserts it right after
// the original. Cell ids are kept: they only need to be unique per page
// (D-009). Divergence: draw.io gives the copy fresh cell ids with a GUID
// prefix; keeping them is deterministic and keeps in-page links valid.
// Result.page = the new index.
Result duplicatePage(Transaction& tx, std::size_t index, const PageIdGenerator& newId = randomPageId);

// Sets the <diagram name>. Same name -> success without a command.
Result renamePage(Transaction& tx, std::size_t index, std::string_view name);

// Moves page `from` so that it ends up at page index `to` (clamped to the
// last index). Foreign <mxfile> children keep their entry positions.
// Result.page = the new index.
Result movePage(Transaction& tx, std::size_t from, std::size_t to);

// Replaces the content of a page by the model parts of `model` (attributes,
// foreign children, <root> entries, e.g. as parsed from the Edit Diagram text).
// The page keeps what is not part of the model: its <diagram> attributes (id,
// name), its encoding, the foreign <diagram> children and the payload frame.
// One ReplacePage; observers rebuild the page (ChangeSet "reset"). Fails when
// there is no such page, or when `model` has no cells (a page needs its root
// cell and a layer to be editable).
Result replacePageModel(Transaction& tx, std::size_t index, core::Page model);

// Page setup (File > Page Setup, Format panel "Diagram" tab): each set field
// is written with the corresponding core::Page setter (canonical attribute
// order, draw.io number format); unset fields are left alone. `background`
// "" or "none" removes the attribute. One ReplacePageAttributes; nothing
// when no value changes.
struct PageProperties {
    std::optional<bool> grid;
    std::optional<double> gridSize;
    std::optional<bool> pageVisible;
    std::optional<double> pageScale;
    std::optional<double> pageWidth;  // both width and height are needed to
    std::optional<double> pageHeight; // change the size; one alone -> failure
    std::optional<std::string> background;
    std::optional<bool> math;
    std::optional<bool> shadow;
    // "auto", "simple" or "none" (dark diagram mode); "" or "default" removes the attribute.
    std::optional<std::string> adaptiveColors;
};
Result setPageProperties(Transaction& tx, std::size_t index, const PageProperties& properties);

} // namespace qraw::commands::ops
