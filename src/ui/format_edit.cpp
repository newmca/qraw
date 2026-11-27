// Write helpers of the Format panel (see format_edit.h).
#include "format_edit.h"

#include "qraw/commands/transaction.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/document_session.h"

namespace qraw::ui {

std::vector<std::string> selectedCells(const DocumentSession& session, CellFilter filter)
{
    std::vector<std::string> ids;
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return ids;

    for (const std::string& id : session.selection().cells()) {
        const core::Cell* cell = page->findCell(id);
        if (cell == nullptr)
            continue;
        const bool keep = filter == CellFilter::All || (filter == CellFilter::Edges) == cell->isEdge();
        if (keep)
            ids.push_back(id);
    }
    return ids;
}

StyleSummary summarizeSelection(const DocumentSession& session)
{
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return {};
    return StyleSummary::of(*page, session.selection().cells(), stylesheetOf(session));
}

const render::Stylesheet& stylesheetOf(const DocumentSession& session)
{
    const render::Stylesheet* stylesheet = session.renderContext().stylesheet;
    return stylesheet != nullptr ? *stylesheet : render::Stylesheet::drawioDefault();
}

std::string formatMergeKey(std::string_view key, const std::vector<std::string>& ids)
{
    std::string merge = "format:";
    merge += key;
    merge += ':';
    for (const std::string& id : ids) {
        merge += id;
        merge += ',';
    }
    return merge;
}

commands::Result editCurrentPage(DocumentSession& session, const QString& name, const PageEdit& edit,
                                 const std::string& mergeKey)
{
    const std::size_t page = session.currentPageIndex();
    return session.edit(
        name, [&edit, page](commands::Transaction& tx) { return edit(tx, page); }, mergeKey);
}

commands::Result applyStyles(DocumentSession& session, const QString& name, const std::vector<std::string>& ids,
                             std::vector<commands::ops::StyleEdit> edits, const std::string& mergeKey)
{
    return editCurrentPage(
        session, name,
        [&ids, &edits](commands::Transaction& tx, std::size_t page) {
            return commands::ops::setStyles(tx, page, ids, edits);
        },
        mergeKey);
}

commands::Result applyStyle(DocumentSession& session, const QString& name, const std::vector<std::string>& ids,
                            std::string_view key, std::optional<std::string> value, const std::string& mergeKey)
{
    return applyStyles(session, name, ids, {commands::ops::StyleEdit{std::string(key), std::move(value)}}, mergeKey);
}

commands::Result applyPageProperties(DocumentSession& session, const QString& name,
                                     const commands::ops::PageProperties& properties, const std::string& mergeKey)
{
    return editCurrentPage(
        session, name,
        [&properties](commands::Transaction& tx, std::size_t page) {
            return commands::ops::setPageProperties(tx, page, properties);
        },
        mergeKey);
}

std::string flagText(bool on)
{
    return on ? "1" : "0";
}

} // namespace qraw::ui
