// The Cells panel (cells_panel.h).
#include "qraw/ui/cells_panel.h"

#include "docks_find_engine.h"
#include "qraw/commands/change_set.h"
#include "qraw/core/page.h"
#include "qraw/ui/canvas_view.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/document_session.h"

#include <QCoreApplication>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace qraw::ui {

namespace {

struct Text {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::CellsPanel)
};

// Enter (or Return) on the tree runs `activate` with the current row, on every platform. The tree does not
// emit `activated` for the key everywhere: on macOS, Qt's item view tries to start an edit instead (the
// platform convention) and, with no edit trigger, does nothing.
class EnterKeyFilter : public QObject {
public:
    EnterKeyFilter(QTreeView& tree, std::function<void(const QModelIndex&)> activate)
        : QObject(&tree), tree_(tree), activate_(std::move(activate))
    {
        tree.installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched != &tree_ || event->type() != QEvent::KeyPress)
            return false;
        const auto* key = static_cast<const QKeyEvent*>(event);
        const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
        // The tree has no edit triggers, so no item editor can want the key.
        if (!enter || (key->modifiers() & ~Qt::KeypadModifier) != Qt::NoModifier || !tree_.currentIndex().isValid())
            return false;
        activate_(tree_.currentIndex());
        return true;
    }

private:
    QTreeView& tree_;
    std::function<void(const QModelIndex&)> activate_;
};

// --- what a row says --------------------------------------------------------------------

// The kind of shape a vertex is, for a reader who cannot see it: the stencil or shape name of the
// style ("mxgraph.flowchart.process" is "Process"), the first style name ("ellipse"), else Rectangle.
QString shapeKind(const core::Cell& cell)
{
    const core::Style style = cell.style();
    std::string name = style.value("shape", "");
    if (name.empty()) {
        const std::vector<std::string> names = style.names();
        // "text" and "group" are styles of their own; every other bare name is the shape (ellipse, rhombus).
        if (!names.empty())
            name = names.front();
    }
    if (name.empty())
        return Text::tr("Rectangle");
    if (const std::size_t dot = name.rfind('.'); dot != std::string::npos && dot + 1 < name.size())
        name = name.substr(dot + 1);
    std::replace(name.begin(), name.end(), '_', ' ');
    QString kind = QString::fromStdString(name);
    kind[0] = kind[0].toUpper();
    return kind;
}

// The label as one line of plain text.
QString plainLabel(const core::Cell& cell)
{
    const QString label = docks::plainLabelText(QString::fromStdString(cell.value()), docks::isHtmlLabel(cell));
    return label.simplified();
}

// What a terminal of a connector is called: its label, else its kind; none for a loose end.
QString terminalName(const core::Page& page, const std::string& id)
{
    const core::Cell* terminal = id.empty() ? nullptr : page.findCell(id);
    if (terminal == nullptr)
        return Text::tr("an open end");
    const QString label = plainLabel(*terminal);
    return label.isEmpty() ? shapeKind(*terminal) : label;
}

QString connectorText(const core::Page& page, const core::Cell& edge)
{
    const QString ends = Text::tr("Connector from %1 to %2")
                             .arg(terminalName(page, edge.source()), terminalName(page, edge.target()));
    const QString label = plainLabel(edge);
    return label.isEmpty() ? ends : Text::tr("%1: %2").arg(ends, label);
}

QString layerName(const core::Page& page, const core::Cell& layer)
{
    const QString name = QString::fromStdString(layer.value());
    if (!name.isEmpty())
        return name;
    const std::vector<const core::Cell*> layers = page.layers();
    const auto found = std::find(layers.begin(), layers.end(), &layer);
    const int position = found == layers.end() ? 1 : static_cast<int>(found - layers.begin()) + 1;
    return position == 1 ? Text::tr("Background") : Text::tr("Layer %1").arg(position);
}

QString cellText(const core::Page& page, const core::Cell& cell)
{
    if (isLayer(page, cell.id()))
        return layerName(page, cell);
    if (cell.isEdge())
        return connectorText(page, cell);
    const QString label = plainLabel(cell);
    return label.isEmpty() ? Text::tr("%1 (no label)").arg(shapeKind(cell)) : label;
}

// The absolute position of a vertex: its geometry plus those of the vertices it sits in.
core::Point absolutePosition(const core::Page& page, const core::Cell& cell)
{
    core::Point position;
    const core::Cell* current = &cell;
    for (std::size_t guard = 0; current != nullptr && guard <= page.cellCount(); ++guard) {
        if (current->isVertex() && current->geometry() != nullptr) {
            position.x += current->geometry()->x();
            position.y += current->geometry()->y();
        }
        current = current->parent().empty() ? nullptr : page.findCell(current->parent());
    }
    return position;
}

QString cellDescription(const core::Page& page, const core::Cell& cell)
{
    if (isLayer(page, cell.id()))
        return cell.isVisible() ? Text::tr("Layer") : Text::tr("Layer, hidden");
    QStringList parts;
    if (cell.isEdge()) {
        const std::size_t waypoints = cell.geometry() != nullptr ? cell.geometry()->points().size() : 0;
        parts << Text::tr("Connector")
              << (waypoints == 1 ? Text::tr("%n waypoint", nullptr, 1)
                                 : Text::tr("%n waypoints", nullptr, static_cast<int>(waypoints)));
    } else {
        parts << shapeKind(cell);
        if (cell.geometry() != nullptr) {
            const core::Point position = absolutePosition(page, cell);
            parts << Text::tr("at %1, %2").arg(std::lround(position.x)).arg(std::lround(position.y))
                  << Text::tr("size %1 by %2").arg(std::lround(cell.geometry()->width())).arg(std::lround(cell.geometry()->height()));
        }
    }
    if (isCellLocked(page, cell.id()))
        parts << Text::tr("locked");
    if (!cell.isVisible())
        parts << Text::tr("hidden");
    return parts.join(QStringLiteral(", "));
}

} // namespace

// --- the model ------------------------------------------------------------------------------

CellTreeModel::CellTreeModel(DocumentSession& session, QObject* parent) : QAbstractItemModel(parent), session_(session)
{
    connect(&session_, &DocumentSession::documentChanged, this,
            [this](const commands::ChangeSet& changes) { onDocumentChanged(changes); });
    connect(&session_, &DocumentSession::currentPageChanged, this, [this] {
        stale_ = true;
        if (active_)
            rebuild();
    });
}

CellTreeModel::~CellTreeModel() = default;

const core::Page* CellTreeModel::page() const
{
    return session_.currentPage();
}

void CellTreeModel::setActive(bool active)
{
    if (active == active_)
        return;
    active_ = active;
    if (active_ && stale_)
        rebuild();
}

void CellTreeModel::rebuild()
{
    beginResetModel();
    nodes_.clear();
    byId_.clear();
    if (const core::Page* current = page(); current != nullptr && current->rootCell() != nullptr) {
        nodes_.push_back({current->rootCell()->id(), -1, 0, {}});
        // Breadth first with indices only: nodes_ grows while it is walked.
        for (std::size_t next = 0; next < nodes_.size(); ++next) {
            const std::string parentId = nodes_[next].id; // a copy: nodes_ reallocates below
            int row = 0;
            for (const std::string& childId : current->childIds(parentId)) {
                const int index = static_cast<int>(nodes_.size());
                nodes_.push_back({childId, static_cast<int>(next), row++, {}});
                nodes_[next].children.push_back(index);
                byId_[childId] = index;
            }
        }
    }
    stale_ = false;
    ++rebuilds_;
    endResetModel();
}

void CellTreeModel::onDocumentChanged(const commands::ChangeSet& changes)
{
    if (!active_) {
        stale_ = true;
        return;
    }
    const core::Page* current = page();
    if (current == nullptr || !changes.pagesAdded().empty() || !changes.pagesRemoved().empty()
        || changes.pagesReorderedFlag()) {
        rebuild();
        return;
    }
    const auto found = changes.pages().find(current->id());
    if (found == changes.pages().end())
        return; // another page changed
    const commands::ChangeSet::PageChanges& pageChanges = found->second;
    // The tree is the same when no cell was added, removed, reordered or moved. A changed layer can change
    // which cells are selectable, which is not a change of one row.
    bool tree = pageChanges.added.empty() && pageChanges.removed.empty() && pageChanges.reordered.empty()
        && pageChanges.childrenChanged.empty() && !pageChanges.reset;
    for (const std::string& id : pageChanges.changed)
        tree = tree && !isLayer(*current, id);
    if (!tree) {
        rebuild();
        return;
    }
    // A connector's text names its ends: when an end changes the connector's row does too.
    std::vector<std::string> rows(pageChanges.changed.begin(), pageChanges.changed.end());
    for (const std::string& id : pageChanges.changed) {
        for (const core::Cell* edge : current->connectedEdges(id))
            rows.push_back(edge->id());
    }
    for (const std::string& id : rows) {
        const QModelIndex index = indexOfCell(id);
        if (index.isValid())
            emit dataChanged(index, index);
    }
}

QModelIndex CellTreeModel::indexOfCell(const std::string& id) const
{
    const auto found = byId_.find(id);
    if (found == byId_.end())
        return {};
    const Node& node = nodes_[static_cast<std::size_t>(found->second)];
    return createIndex(node.row, 0, static_cast<quintptr>(found->second));
}

std::string CellTreeModel::cellId(const QModelIndex& index) const
{
    if (!index.isValid() || index.internalId() >= nodes_.size())
        return {};
    return nodes_[index.internalId()].id;
}

QModelIndex CellTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (nodes_.empty() || column != 0 || row < 0)
        return {};
    const std::size_t parentNode = parent.isValid() ? static_cast<std::size_t>(parent.internalId()) : 0;
    if (parentNode >= nodes_.size() || static_cast<std::size_t>(row) >= nodes_[parentNode].children.size())
        return {};
    return createIndex(row, 0, static_cast<quintptr>(nodes_[parentNode].children[static_cast<std::size_t>(row)]));
}

QModelIndex CellTreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid() || child.internalId() >= nodes_.size())
        return {};
    const int parentNode = nodes_[child.internalId()].parent;
    if (parentNode <= 0)
        return {}; // a layer: the page's root cell is not a row
    return createIndex(nodes_[static_cast<std::size_t>(parentNode)].row, 0, static_cast<quintptr>(parentNode));
}

int CellTreeModel::rowCount(const QModelIndex& parent) const
{
    if (nodes_.empty() || parent.column() > 0)
        return 0;
    const std::size_t node = parent.isValid() ? static_cast<std::size_t>(parent.internalId()) : 0;
    return node < nodes_.size() ? static_cast<int>(nodes_[node].children.size()) : 0;
}

int CellTreeModel::columnCount(const QModelIndex&) const
{
    return 1;
}

QVariant CellTreeModel::data(const QModelIndex& index, int role) const
{
    const core::Page* current = page();
    if (current == nullptr || !index.isValid() || index.internalId() >= nodes_.size())
        return {};
    const std::string& id = nodes_[index.internalId()].id;
    if (role == CellIdRole)
        return QString::fromStdString(id);
    const core::Cell* cell = current->findCell(id);
    if (cell == nullptr)
        return {};
    switch (role) {
    case Qt::DisplayRole:
    case Qt::AccessibleTextRole:
        return cellText(*current, *cell);
    case Qt::ToolTipRole:
    case Qt::AccessibleDescriptionRole:
        return cellDescription(*current, *cell);
    default:
        return {};
    }
}

Qt::ItemFlags CellTreeModel::flags(const QModelIndex& index) const
{
    const core::Page* current = page();
    if (current == nullptr || !index.isValid() || index.internalId() >= nodes_.size())
        return Qt::NoItemFlags;
    Qt::ItemFlags result = Qt::ItemIsEnabled;
    // Cells of a hidden or locked layer, hidden cells and the layers are not selectable on the canvas either.
    if (isCellSelectable(*current, nodes_[index.internalId()].id))
        result |= Qt::ItemIsSelectable;
    return result;
}

// --- the panel -----------------------------------------------------------------------------------

struct CellsPanel::Impl {
    Impl(CellsPanel& owner, CanvasView& canvasView)
        : panel(owner), canvas(canvasView), session(canvasView.session()), model(canvasView.session())
    {
    }

    // The tree's selection to the session's.
    void treeSelectionChanged()
    {
        if (syncing)
            return;
        std::vector<std::string> ids;
        for (const QModelIndex& index : tree->selectionModel()->selectedRows())
            ids.push_back(model.cellId(index));
        syncing = true;
        session.selection().set(std::move(ids));
        syncing = false;
    }

    // The session's selection to the tree's. The tree's current row follows the first selected cell, so
    // that a screen reader announces it, but the keyboard focus stays where it is.
    void sessionSelectionChanged()
    {
        if (syncing || !model.isActive())
            return;
        QItemSelection selection;
        for (const std::string& id : session.selection().cells()) {
            const QModelIndex index = model.indexOfCell(id);
            if (index.isValid())
                selection.select(index, index);
        }
        syncing = true;
        tree->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        if (!session.selection().isEmpty()) {
            const QModelIndex first = model.indexOfCell(session.selection().first());
            if (first.isValid()) {
                tree->selectionModel()->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
                tree->scrollTo(first);
            }
        }
        syncing = false;
    }

    void expandLayers()
    {
        for (int row = 0; row < model.rowCount(); ++row)
            tree->expand(model.index(row, 0));
    }

    CellsPanel& panel;
    CanvasView& canvas;
    DocumentSession& session;
    CellTreeModel model;
    QTreeView* tree = nullptr;
    bool syncing = false;
};

CellsPanel::CellsPanel(CanvasView& canvas, QWidget* parent) : QWidget(parent), d(std::make_unique<Impl>(*this, canvas))
{
    setObjectName(QStringLiteral("cellsPanel"));
    d->tree = new QTreeView(this);
    d->tree->setObjectName(QStringLiteral("cellsTree"));
    d->tree->setModel(&d->model);
    d->tree->setHeaderHidden(true);
    d->tree->setUniformRowHeights(true); // 50 000 rows must not measure each one
    d->tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    d->tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    d->tree->setAccessibleName(tr("Cells"));
    d->tree->setAccessibleDescription(tr("The layers and cells of the page. Enter edits the label of the selected cell."));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(d->tree);

    connect(d->tree->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { d->treeSelectionChanged(); });
    connect(&d->session.selection(), &SelectionModel::selectionChanged, this, [this] { d->sessionSelectionChanged(); });
    // Enter (and a double click) on a row: edit the label through the canvas's own editor.
    const auto editLabelOfRow = [this](const QModelIndex& index) {
        const std::string id = d->model.cellId(index);
        if (!id.empty())
            d->canvas.editLabel(id);
    };
    connect(d->tree, &QAbstractItemView::activated, this, editLabelOfRow); // double click
    new EnterKeyFilter(*d->tree, editLabelOfRow);
    connect(&d->model, &QAbstractItemModel::modelReset, this, [this] {
        d->expandLayers();
        d->sessionSelectionChanged();
    });
}

CellsPanel::~CellsPanel() = default;

QTreeView* CellsPanel::tree() const
{
    return d->tree;
}

CellTreeModel* CellsPanel::model() const
{
    return &d->model;
}

void CellsPanel::showEvent(QShowEvent* event)
{
    d->model.setActive(true);
    QWidget::showEvent(event);
}

void CellsPanel::hideEvent(QHideEvent* event)
{
    d->model.setActive(false);
    QWidget::hideEvent(event);
}

} // namespace qraw::ui
