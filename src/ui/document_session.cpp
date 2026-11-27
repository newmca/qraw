#include "qraw/ui/document_session.h"

#include "qraw/commands/ops_layout.h"
#include "qraw/ui/cell_queries.h"

#include <algorithm>

namespace qraw::ui {

// Adapts the Editor's observer interface to the session's private slots, so
// the session's public signals can keep the natural names.
class DocumentSession::Observer final : public commands::DocumentObserver {
public:
    explicit Observer(DocumentSession& session) : session_(session) {}
    void documentChanged(const commands::ChangeSet& changes, commands::ChangeOrigin origin) override
    {
        session_.onDocumentChanged(changes, origin);
    }
    void undoStateChanged() override { session_.onUndoStateChanged(); }

private:
    DocumentSession& session_;
};

DocumentSession::DocumentSession(core::Document document, QObject* parent)
    : QObject(parent), editor_(std::move(document)), renderContext_(render::RenderContext::defaults()),
      observer_(std::make_unique<Observer>(*this))
{
    editor_.addObserver(observer_.get());
    lastModified_ = editor_.isModified();
}

DocumentSession::~DocumentSession()
{
    editor_.removeObserver(observer_.get());
}

const core::Page* DocumentSession::currentPage() const
{
    return document().page(currentPage_);
}

void DocumentSession::setCurrentPage(std::size_t index)
{
    const std::size_t count = document().pageCount();
    index = count == 0 ? 0 : std::min(index, count - 1);
    if (index == currentPage_)
        return;
    currentPage_ = index;
    selection_.clear();
    currentLayer_.clear();
    const bool hadGroup = !enteredGroup_.empty();
    enteredGroup_.clear();
    emit currentPageChanged(currentPage_);
    emit currentLayerChanged();
    if (hadGroup)
        emit enteredGroupChanged();
}

std::string DocumentSession::currentLayer() const
{
    const core::Page* page = currentPage();
    if (page == nullptr)
        return {};
    if (!currentLayer_.empty() && isLayer(*page, currentLayer_))
        return currentLayer_;
    const auto layers = page->layers();
    return layers.empty() ? std::string() : layers.front()->id();
}

void DocumentSession::setCurrentLayer(const std::string& layerId)
{
    if (layerId == currentLayer_)
        return;
    currentLayer_ = layerId;
    emit currentLayerChanged();
}

void DocumentSession::enterGroup(const std::string& id)
{
    const core::Page* page = currentPage();
    const core::Cell* cell = page != nullptr ? page->findCell(id) : nullptr;
    if (cell == nullptr || !cell->isVertex() || id == enteredGroup_)
        return;
    enteredGroup_ = id;
    selection_.clear();
    emit enteredGroupChanged();
}

void DocumentSession::exitGroup()
{
    if (enteredGroup_.empty())
        return;
    // Graph.exitGroup: go up one level; selecting the group that was left.
    const core::Page* page = currentPage();
    const core::Cell* group = page != nullptr ? page->findCell(enteredGroup_) : nullptr;
    const std::string left = enteredGroup_;
    enteredGroup_.clear();
    if (group != nullptr) {
        const core::Cell* parent = page->findCell(group->parent());
        if (parent != nullptr && parent != page->rootCell() && !isLayer(*page, parent->id()))
            enteredGroup_ = parent->id();
    }
    selection_.set({left});
    emit enteredGroupChanged();
}

std::string DocumentSession::insertionParent() const
{
    return enteredGroup_.empty() ? currentLayer() : enteredGroup_;
}

commands::Result DocumentSession::edit(const QString& name, const commands::Editor::EditFunction& fn,
                                       std::string mergeKey)
{
    // The layout manager (mxLayoutManager) arranges the children of the
    // layout parents the edit touched inside the same transaction, so the
    // edit and its layout are one undo entry. Loading, saving and the CLI
    // never get here.
    const commands::Editor::EditFunction withLayouts = [&fn](commands::Transaction& tx) {
        commands::Result result = fn(tx);
        if (!result)
            return result;
        const commands::ChangeSet changes = tx.changes();
        for (std::size_t page = 0; page < tx.document().pageCount(); ++page) {
            if (changes.pages().contains(tx.document().page(page)->id())) {
                if (commands::Result laidOut = commands::ops::applyLayouts(tx, page, changes); !laidOut)
                    return laidOut;
            }
        }
        return result;
    };
    commands::Result result = editor_.edit(name.toStdString(), withLayouts, std::move(mergeKey));
    if (!result) {
        emit statusMessage(QString::fromStdString(result.error));
        return result;
    }
    if (result.page)
        setCurrentPage(*result.page);
    if (!result.cells.empty())
        selection_.set(result.cells);
    return result;
}

void DocumentSession::endMerge()
{
    editor_.undoStack().mergeBreak();
}

bool DocumentSession::undo()
{
    return editor_.undo();
}

bool DocumentSession::redo()
{
    return editor_.redo();
}

bool DocumentSession::canUndo() const
{
    return editor_.undoStack().canUndo();
}

bool DocumentSession::canRedo() const
{
    return editor_.undoStack().canRedo();
}

QString DocumentSession::undoText() const
{
    const std::string_view text = editor_.undoStack().undoText();
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString DocumentSession::redoText() const
{
    const std::string_view text = editor_.undoStack().redoText();
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

void DocumentSession::markSaved()
{
    editor_.markSaved();
}

void DocumentSession::resetDocument(core::Document document)
{
    selection_.clear();
    currentLayer_.clear();
    const bool hadGroup = !enteredGroup_.empty();
    enteredGroup_.clear();
    const std::size_t oldPage = currentPage_;
    currentPage_ = 0;
    editor_.resetDocument(std::move(document)); // notifies documentChanged(Reset)
    if (oldPage != currentPage_)
        emit currentPageChanged(currentPage_);
    emit currentLayerChanged();
    if (hadGroup)
        emit enteredGroupChanged();
}

void DocumentSession::setBaseDirectory(const QString& directory)
{
    baseDirectory_ = directory;
    renderContext_.images.baseDirectory = directory;
}

void DocumentSession::setDiagramDark(bool dark)
{
    if (renderContext_.theme.dark == dark)
        return;
    renderContext_.theme = dark ? render::Theme::darkScheme() : render::Theme();
    emit renderContextChanged();
}

render::RenderContext DocumentSession::pageRenderContext() const
{
    render::RenderContext context = renderContext_;
    if (const core::Page* page = currentPage()) {
        context.placeholders.page = page->name();
        context.placeholders.pageNumber = static_cast<int>(currentPage_) + 1;
        context.placeholders.pageCount = static_cast<int>(document().pageCount());
    }
    return context;
}

void DocumentSession::onDocumentChanged(const commands::ChangeSet& changes, commands::ChangeOrigin origin)
{
    // Pages may have been removed or reordered: keep the index valid.
    const std::size_t count = document().pageCount();
    if (count > 0 && currentPage_ >= count) {
        currentPage_ = count - 1;
        selection_.clear();
        emit currentPageChanged(currentPage_);
    }
    pruneSelection();
    if (origin == commands::ChangeOrigin::Undo || origin == commands::ChangeOrigin::Redo)
        selectChangedCells(changes);
    emit documentChanged(changes, origin);
}

void DocumentSession::onUndoStateChanged()
{
    emit undoStateChanged();
    const bool modified = editor_.isModified();
    if (modified != lastModified_) {
        lastModified_ = modified;
        emit modifiedChanged(modified);
    }
}

void DocumentSession::pruneSelection()
{
    const core::Page* page = currentPage();
    if (page == nullptr) {
        selection_.clear();
        enteredGroup_.clear();
        return;
    }
    std::vector<std::string> gone;
    for (const std::string& id : selection_.cells()) {
        if (page->findCell(id) == nullptr)
            gone.push_back(id);
    }
    if (!gone.empty())
        selection_.remove(gone);
    if (!enteredGroup_.empty() && page->findCell(enteredGroup_) == nullptr) {
        enteredGroup_.clear();
        emit enteredGroupChanged();
    }
}

void DocumentSession::selectChangedCells(const commands::ChangeSet& changes)
{
    const core::Page* page = currentPage();
    if (page == nullptr)
        return;
    const auto it = changes.pages().find(page->id());
    if (it == changes.pages().end())
        return;
    std::vector<std::string> cells;
    for (const auto* set : {&it->second.added, &it->second.changed}) {
        for (const std::string& id : *set) {
            if (isCellSelectable(*page, id))
                cells.push_back(id);
        }
    }
    selection_.set(std::move(cells));
}

} // namespace qraw::ui
