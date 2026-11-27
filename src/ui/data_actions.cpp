#include "qraw/ui/data_actions.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_properties.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/cell.h"
#include "qraw/core/document.h"
#include "qraw/core/page.h"
#include "qraw/render/stylesheet.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/document_session.h"

#include <QCoreApplication>
#include <QDesktopServices>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace qraw::ui::actions {

namespace {

using commands::Result;
using commands::Transaction;
namespace ops = commands::ops;

constexpr const char* kContext = "qraw::ui::actions";
constexpr std::string_view kNewWindowTarget = "_blank"; // Graph.linkTarget, what draw.io writes
// setAttributeForCell wraps a plain cell in a <UserObject>; Edit Data's dialog in an <object>.
constexpr std::string_view kAttributeWrapper = "UserObject";

// For the names of edits (QT_TRANSLATE_NOOP at the call sites collects them).
QString translated(const char* text)
{
    return QCoreApplication::translate(kContext, text);
}

// The value of a wrapper attribute; nullopt on a plain cell or without it.
std::optional<std::string> dataAttribute(const core::Cell& cell, std::string_view name)
{
    const core::CellWrapper* wrapper = cell.wrapper();
    if (wrapper == nullptr)
        return std::nullopt;
    const std::string* value = wrapper->attributes.find(name);
    if (value == nullptr)
        return std::nullopt;
    return *value;
}

std::vector<const core::Cell*> cellsOf(const core::Page& page, const std::vector<std::string>& ids)
{
    std::vector<const core::Cell*> cells;
    for (const std::string& id : ids) {
        if (const core::Cell* cell = page.findCell(id))
            cells.push_back(cell);
    }
    return cells;
}

// The selection without locked cells (Graph.getEditableCells).
std::vector<std::string> editableSelection(const DocumentSession& session)
{
    std::vector<std::string> ids;
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return ids;
    for (const std::string& id : session.selection().cells()) {
        if (page->findCell(id) != nullptr && !isCellLocked(*page, id))
            ids.push_back(id);
    }
    return ids;
}

// Runs `fn` as one named edit; true when the model changed.
bool runEdit(DocumentSession& session, const char* name, const std::function<Result(Transaction&)>& fn)
{
    bool changed = false;
    const Result result = session.edit(translated(name), [&](Transaction& tx) {
        Result r = fn(tx);
        changed = r.ok && !tx.empty();
        // The selection stays as it is: an edit of data is not a change of what is selected.
        if (r.ok)
            r.cells.clear();
        return r;
    });
    return result.ok && changed;
}

// Edit Data hides draw.io's data source bookkeeping (Graph.isDataSourceBaseAttribute).
bool isHiddenDataName(const std::string& name)
{
    return name == "label" || name == "id" || name == "placeholders" || name == "tags" ||
           name.starts_with("dataSourceBase");
}

// The attribute names an `Edit Data` row can show, with the cell's value.
std::vector<std::pair<std::string, std::string>> dataRowsOf(const core::Cell& cell)
{
    std::vector<std::pair<std::string, std::string>> rows;
    if (const core::CellWrapper* wrapper = cell.wrapper()) {
        for (const core::Attribute& attribute : wrapper->attributes) {
            if (!isHiddenDataName(attribute.name))
                rows.emplace_back(attribute.name, std::string(attribute.value));
        }
    }
    return rows;
}

// The first value of `pick` when every cell has it, nullopt when they differ.
template <class Pick>
std::optional<std::string> commonValue(const std::vector<const core::Cell*>& cells, const Pick& pick)
{
    const std::string first = pick(*cells.front());
    for (const core::Cell* cell : cells) {
        if (pick(*cell) != first)
            return std::nullopt;
    }
    return first;
}

// The shape a vertex is drawn with, from its style and the default stylesheet's named styles,
// without copying an embedded image (the image key is dropped before resolving).
std::string effectiveShape(const core::Cell& cell)
{
    core::Style style = cell.style();
    style.remove("image");
    return std::string(render::Stylesheet::drawioDefault().resolve(style, render::CellKind::Vertex).shape());
}

// EditorUi.js:39312 enables Edit Image for cells that have an image: an `image` style, or the image shape
// (draw.io's default vertex is a label shape, so that shape alone does not count).
bool isImageCapable(const core::Cell& cell)
{
    if (!cell.isVertex())
        return false;
    return cell.style().has("image") || effectiveShape(cell) == "image";
}

} // namespace

// --- Edit Data ---------------------------------------------------------------------

std::vector<std::string> dataTargets(const DocumentSession& session)
{
    std::vector<std::string> ids = session.selection().cells();
    const core::Page* page = session.currentPage();
    if (!ids.empty() || page == nullptr)
        return ids;
    if (const core::Cell* root = page->rootCell())
        ids.push_back(root->id());
    return ids;
}

bool canEditData(const DocumentSession& session)
{
    return !dataTargets(session).empty();
}

DataSheet readDataSheet(const DocumentSession& session)
{
    DataSheet sheet;
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return sheet;
    sheet.isDiagram = session.selection().isEmpty();
    const std::vector<const core::Cell*> cells = cellsOf(*page, dataTargets(session));
    if (cells.empty())
        return sheet;
    for (const core::Cell* cell : cells)
        sheet.cells.push_back(cell->id());
    if (cells.size() == 1 && !sheet.isDiagram)
        sheet.id = cells.front()->id();

    // The union of the property names, each with the value all cells share.
    std::map<std::string, DataRow> rows;
    for (const core::Cell* cell : cells) {
        for (const auto& [name, value] : dataRowsOf(*cell)) {
            DataRow& row = rows[name];
            row.name = name;
            (void)value;
        }
    }
    for (auto& [name, row] : rows) {
        const auto pick = [&name](const core::Cell& cell) { return dataAttribute(cell, name).value_or(std::string()); };
        const std::optional<std::string> common = commonValue(cells, pick);
        row.value = common.value_or(std::string());
        row.mixed = !common.has_value();
        sheet.rows.push_back(std::move(row));
    }

    const auto tagsOf = [](const core::Cell& cell) { return dataAttribute(cell, "tags").value_or(std::string()); };
    const std::optional<std::string> tags = commonValue(cells, tagsOf);
    sheet.tags = tags.value_or(std::string());
    sheet.tagsMixed = !tags.has_value();

    sheet.placeholdersAvailable = !sheet.isDiagram && (cells.front()->isVertex() || cells.front()->isEdge());
    const auto flag = [](const core::Cell& cell) { return dataAttribute(cell, "placeholders") == std::optional<std::string>("1"); };
    const std::size_t on = static_cast<std::size_t>(std::count_if(cells.begin(), cells.end(), [&](const core::Cell* c) { return flag(*c); }));
    sheet.placeholders = on == 0 ? Qt::Unchecked : (on == cells.size() ? Qt::Checked : Qt::PartiallyChecked);
    return sheet;
}

std::string normalizeTags(const std::string& text)
{
    std::string result;
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
            ++pos;
        const std::size_t start = pos;
        while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos])))
            ++pos;
        if (pos > start) {
            if (!result.empty())
                result += ' ';
            result.append(text, start, pos - start);
        }
    }
    return result;
}

bool editData(DocumentSession& session, const DataSheet& sheet, const DataChange& change)
{
    std::vector<ops::DataEdit> edits;
    for (const std::string& name : change.removed)
        edits.push_back({name, std::nullopt});
    for (const DataRow& row : change.written)
        edits.push_back({row.name, row.value});
    if (change.placeholders)
        edits.push_back({"placeholders", *change.placeholders ? std::optional<std::string>("1") : std::nullopt});
    if (change.tags) {
        const std::string tags = normalizeTags(*change.tags);
        edits.push_back({"tags", tags.empty() ? std::nullopt : std::optional<std::string>(tags)});
    }
    if (edits.empty() || sheet.cells.empty())
        return false;
    const std::size_t pageIndex = session.currentPageIndex();
    return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Edit Data"), [&](Transaction& tx) {
        return ops::editCellData(tx, pageIndex, sheet.cells, edits, "object");
    });
}

// --- Edit Link ---------------------------------------------------------------------

bool canEditLink(const DocumentSession& session)
{
    // Stops at the first unlocked cell: this runs on every selection change, also for 10,000 selected cells.
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return false;
    for (const std::string& id : session.selection().cells()) {
        if (page->findCell(id) != nullptr && !isCellLocked(*page, id))
            return true;
    }
    return false;
}

LinkSheet readLinkSheet(const DocumentSession& session)
{
    LinkSheet sheet;
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return sheet;
    sheet.cells = editableSelection(session);
    const std::vector<const core::Cell*> cells = cellsOf(*page, sheet.cells);
    if (cells.empty())
        return sheet;
    const auto linkOf = [](const core::Cell& cell) { return dataAttribute(cell, "link").value_or(std::string()); };
    const auto targetOf = [](const core::Cell& cell) {
        return dataAttribute(cell, "linkTarget") == std::optional<std::string>(std::string(kNewWindowTarget)) ? std::string("1")
                                                                                                           : std::string();
    };
    const std::optional<std::string> link = commonValue(cells, linkOf);
    sheet.link = link.value_or(std::string());
    sheet.linkMixed = !link.has_value();
    const std::optional<std::string> target = commonValue(cells, targetOf);
    sheet.newWindow = target == std::optional<std::string>("1");
    sheet.newWindowMixed = !target.has_value();
    return sheet;
}

bool editLink(DocumentSession& session, const LinkSheet& sheet, const LinkEdit& edit)
{
    if ((!edit.setLink && !edit.setNewWindow) || sheet.cells.empty())
        return false;
    const std::size_t pageIndex = session.currentPageIndex();
    return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Edit Link"), [&](Transaction& tx) {
        for (const std::string& id : sheet.cells) {
            const core::Cell* cell = tx.page(pageIndex)->findCell(id);
            if (cell == nullptr)
                continue;
            std::vector<ops::DataEdit> edits;
            // A cell that has no value and is given none stays as it is: nothing is wrapped for nothing.
            if (edit.setLink) {
                const bool had = dataAttribute(*cell, "link").has_value();
                if (!edit.link.empty() || had)
                    edits.push_back({"link", edit.link.empty() ? std::nullopt : std::optional<std::string>(edit.link)});
            }
            if (edit.setNewWindow) {
                const bool had = dataAttribute(*cell, "linkTarget").has_value();
                if (edit.newWindow || had) {
                    edits.push_back({"linkTarget", edit.newWindow ? std::optional<std::string>(std::string(kNewWindowTarget))
                                                                  : std::nullopt});
                }
            }
            if (edits.empty())
                continue;
            if (Result r = ops::editCellData(tx, pageIndex, {id}, edits, kAttributeWrapper); !r)
                return r;
        }
        return Result::success();
    });
}

std::string pageLink(const std::string& pageId)
{
    return "data:page/id," + pageId;
}

std::optional<std::string> pageIdOfLink(const std::string& link)
{
    constexpr std::string_view prefix = "data:page/id,";
    if (!link.starts_with(prefix) || link.size() == prefix.size())
        return std::nullopt;
    return link.substr(prefix.size());
}

LinkTarget resolveLink(const DocumentSession& session, const std::string& link)
{
    LinkTarget target;
    const std::string text = QString::fromStdString(link).trimmed().toStdString();
    if (text.empty())
        return target;
    target.kind = LinkTarget::Kind::Blocked;
    if (const std::optional<std::string> pageId = pageIdOfLink(text)) {
        const core::Document& document = session.document();
        for (std::size_t i = 0; i < document.pageCount(); ++i) {
            if (document.page(i)->id() == *pageId) {
                target.kind = LinkTarget::Kind::Page;
                target.page = i;
                break;
            }
        }
        return target;
    }
    // Only what a person opens in a browser or a mail program: scripts, files and relative paths are refused.
    const QUrl url(QString::fromStdString(text), QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (url.isValid() && (scheme == QLatin1String("http") || scheme == QLatin1String("https") ||
                          scheme == QLatin1String("mailto"))) {
        target.kind = LinkTarget::Kind::External;
        target.url = url;
    }
    return target;
}

bool followLink(DocumentSession& session, const std::string& link, const std::function<bool(const QUrl&)>& open)
{
    const LinkTarget target = resolveLink(session, link);
    switch (target.kind) {
    case LinkTarget::Kind::Page:
        session.setCurrentPage(target.page);
        return true;
    case LinkTarget::Kind::External:
        return open ? open(target.url) : QDesktopServices::openUrl(target.url);
    case LinkTarget::Kind::Blocked:
        emit session.statusMessage(QCoreApplication::translate("qraw::ui::actions", "This link cannot be opened: %1").arg(QString::fromStdString(link).left(80)));
        return false;
    case LinkTarget::Kind::None:
        break;
    }
    return false;
}

std::optional<std::string> selectedLink(const DocumentSession& session)
{
    const core::Page* page = session.currentPage();
    if (page == nullptr || session.selection().size() != 1)
        return std::nullopt;
    const core::Cell* cell = page->findCell(session.selection().first());
    if (cell == nullptr)
        return std::nullopt;
    std::optional<std::string> link = dataAttribute(*cell, "link");
    if (link && link->empty())
        return std::nullopt;
    return link;
}

QString linkDisplayText(const DocumentSession& session, const std::string& link)
{
    if (const std::optional<std::string> pageId = pageIdOfLink(link)) {
        const core::Document& document = session.document();
        for (std::size_t i = 0; i < document.pageCount(); ++i) {
            if (document.page(i)->id() == *pageId)
                return QCoreApplication::translate("qraw::ui::actions", "Page: %1").arg(QString::fromStdString(document.page(i)->name()));
        }
    }
    return QString::fromStdString(link);
}

// --- Edit Tooltip ------------------------------------------------------------------

bool canEditTooltip(const DocumentSession& session)
{
    return session.selection().size() == 1 && canEditLink(session);
}

std::optional<TooltipSheet> readTooltipSheet(const DocumentSession& session)
{
    if (!canEditTooltip(session))
        return std::nullopt;
    const core::Page* page = session.currentPage();
    const std::string id = session.selection().first();
    const core::Cell* cell = page != nullptr ? page->findCell(id) : nullptr;
    if (cell == nullptr)
        return std::nullopt;
    return TooltipSheet{id, dataAttribute(*cell, "tooltip").value_or(std::string()), isHtmlLabel(*page, id)};
}

bool editTooltip(DocumentSession& session, const std::string& cellId, const std::string& text)
{
    const std::size_t pageIndex = session.currentPageIndex();
    return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Edit Tooltip"), [&](Transaction& tx) {
        return ops::setCellData(tx, pageIndex, cellId, "tooltip", text.empty() ? std::nullopt : std::optional<std::string>(text),
                                kAttributeWrapper);
    });
}

// --- Edit Image --------------------------------------------------------------------

bool canEditImage(const DocumentSession& session)
{
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return false;
    // Stops at the first cell that qualifies; the raw style text rules most cells out without parsing it.
    for (const std::string& id : session.selection().cells()) {
        const core::Cell* cell = page->findCell(id);
        if (cell != nullptr && cell->styleText().find("image") != std::string::npos && isImageCapable(*cell) &&
            !isCellLocked(*page, id))
            return true;
    }
    return false;
}

ImageSheet readImageSheet(const DocumentSession& session)
{
    ImageSheet sheet;
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return sheet;
    std::optional<std::string> first;
    for (const std::string& id : editableSelection(session)) {
        const core::Cell* cell = page->findCell(id);
        if (cell == nullptr || cell->styleText().find("image") == std::string::npos || !isImageCapable(*cell))
            continue;
        sheet.cells.push_back(id);
        const core::Style style = cell->style(); // findValue points into it
        const core::SharedValue* value = style.findValue("image");
        const std::string current = value != nullptr ? std::string(*value) : std::string();
        if (!first)
            first = current;
        else if (current != *first)
            sheet.mixed = true;
    }
    sheet.image = sheet.mixed ? std::string() : first.value_or(std::string());
    return sheet;
}

bool editImage(DocumentSession& session, const ImageEdit& edit)
{
    const ImageSheet sheet = readImageSheet(session);
    if (sheet.cells.empty())
        return false;
    const std::size_t pageIndex = session.currentPageIndex();
    return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Edit Image"), [&](Transaction& tx) {
        std::optional<std::string> value;
        if (edit.image) {
            if (!edit.image->empty())
                value = *edit.image;
            if (Result r = ops::setStyle(tx, pageIndex, sheet.cells, "image", value); !r)
                return r;
        }
        for (const std::string& id : sheet.cells) {
            const core::Cell* cell = tx.page(pageIndex)->findCell(id);
            if (cell == nullptr)
                continue;
            // Sets the shape only where it is not already one that shows an image (label or image);
            // clearing the image of an image shape turns it back into a plain shape.
            const std::string shape = effectiveShape(*cell);
            std::optional<ops::StyleEdit> shapeEdit;
            if (edit.image && shape != "image" && shape != "label" && value)
                shapeEdit = ops::StyleEdit{"shape", "image"};
            else if (edit.image && shape == "image" && !value)
                shapeEdit = ops::StyleEdit{"shape", std::nullopt};
            if (shapeEdit) {
                if (Result r = ops::setStyles(tx, pageIndex, {id}, {*shapeEdit}); !r)
                    return r;
            }
            // Graph's 'expand=0' keeps a cell at its size when its content changes.
            const core::Cell* current = tx.page(pageIndex)->findCell(id);
            if (edit.size && current != nullptr && current->geometry() != nullptr && !current->geometry()->relative() &&
                current->style().value("expand", "1") != "0") {
                core::Rect bounds = current->geometry()->bounds();
                bounds.width = edit.size->first;
                bounds.height = edit.size->second;
                if (Result r = ops::resizeCells(tx, pageIndex, {id}, {bounds}); !r)
                    return r;
            }
        }
        return Result::success();
    });
}

} // namespace qraw::ui::actions
