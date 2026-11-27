// The Layers panel (LayersWindow, js/grapheditor/Dialogs.js:5298).

#include "qraw/ui/layers_panel.h"

#include "qraw/commands/ops_layers.h"
#include "qraw/commands/ops_structure.h"
#include "qraw/commands/queries.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/cell.h"
#include "qraw/core/page.h"
#include "qraw/ui/document_session.h"
#include "qraw/ui/selection_model.h"

#include <QCoreApplication>
#include <QFont>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace qraw::ui {

namespace {

constexpr int kVisibleColumn = 0;
constexpr int kLockColumn = 1;
constexpr int kNameColumn = 2;
constexpr int kIdRole = Qt::UserRole;

using LayerEdit = std::function<commands::Result(commands::Transaction&)>;

// Runs a layer operation as one undo step. Layers are not selectable cells,
// so the cell ids an operation reports are not put into the selection; the
// callers that need one of them (the new or the next layer) read it from the
// operation's result themselves.
commands::Result editLayers(DocumentSession& session, const QString& name, const LayerEdit& edit)
{
    return session.edit(name, [edit](commands::Transaction& tx) {
        commands::Result result = edit(tx);
        result.cells.clear();
        return result;
    });
}

// Position of the layer among the page's layers (bottom-most is 0).
std::optional<std::size_t> layerIndex(const core::Page& page, const std::string& id)
{
    const auto layers = page.layers();
    for (std::size_t i = 0; i < layers.size(); ++i) {
        if (layers[i]->id() == id)
            return i;
    }
    return std::nullopt;
}

// The page's layers, topmost first (draw.io lists layers in reverse z-order).
std::vector<const core::Cell*> layersTopFirst(const core::Page* page)
{
    if (page == nullptr)
        return {};
    const auto layers = page->layers();
    return std::vector<const core::Cell*>(layers.rbegin(), layers.rend());
}

QString rowLayerId(const QTreeWidgetItem* item)
{
    return item->data(kVisibleColumn, kIdRole).toString();
}

QString layerTitle(const core::Cell& layer, bool holdsSelection)
{
    QString name = QString::fromStdString(layer.value());
    if (name.isEmpty())
        name = QCoreApplication::translate("qraw::ui::LayersPanel", "Background");
    return holdsSelection ? name + QStringLiteral(" •") : name;
}

bool isLocked(const core::Cell& layer)
{
    return layer.style().value("locked", "0") == "1";
}

// The layers that contain at least one of the cells.
std::set<std::string> layersHoldingCells(const core::Page& page, const std::vector<std::string>& cells)
{
    std::set<std::string> layers;
    for (const std::string& id : cells) {
        const std::string layer = commands::layerOf(page, id);
        if (!layer.empty())
            layers.insert(layer);
    }
    return layers;
}

QTreeWidgetItem* makeRow(const core::Cell& layer)
{
    auto* item = new QTreeWidgetItem;
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    item->setData(kVisibleColumn, kIdRole, QString::fromStdString(layer.id()));
    return item;
}

void updateRow(QTreeWidgetItem* item, const core::Cell& layer, bool current, bool holdsSelection)
{
    item->setCheckState(kVisibleColumn, layer.isVisible() ? Qt::Checked : Qt::Unchecked);
    item->setCheckState(kLockColumn, isLocked(layer) ? Qt::Checked : Qt::Unchecked);
    item->setText(kNameColumn, layerTitle(layer, holdsSelection));
    QFont font = item->font(kNameColumn);
    font.setBold(current);
    item->setFont(kNameColumn, font);
}

// True when the tree already shows exactly these layers in this order, so
// rows can be updated in place (no item is deleted while Qt may be signalling it).
bool showsLayers(const QTreeWidget& tree, const std::vector<const core::Cell*>& layers)
{
    if (tree.topLevelItemCount() != static_cast<int>(layers.size()))
        return false;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        if (rowLayerId(tree.topLevelItem(static_cast<int>(i))).toStdString() != layers[i]->id())
            return false;
    }
    return true;
}

} // namespace

struct LayersPanel::Impl {
    Impl(LayersPanel& owner, DocumentSession& session) : owner_(&owner), session_(session) {}

    LayersPanel* owner_;
    DocumentSession& session_;
    QTreeWidget* tree = nullptr;
    QToolButton* deleteButton = nullptr;
    QToolButton* duplicateButton = nullptr;
    QToolButton* renameButton = nullptr;
    QToolButton* upButton = nullptr;
    QToolButton* downButton = nullptr;
    QToolButton* moveToButton = nullptr;
    QMenu* moveToMenu = nullptr;
    bool syncing = false;

    // Brings the rows and the buttons in line with the model and selection.
    void sync()
    {
        const core::Page* page = session_.currentPage();
        const auto rows = layersTopFirst(page);
        const std::string current = session_.currentLayer();
        const std::set<std::string> holding =
            page != nullptr ? layersHoldingCells(*page, session_.selection().cells()) : std::set<std::string>{};

        syncing = true;
        if (!showsLayers(*tree, rows)) {
            tree->clear();
            for (const core::Cell* layer : rows)
                tree->addTopLevelItem(makeRow(*layer));
        }
        for (std::size_t i = 0; i < rows.size(); ++i) {
            QTreeWidgetItem* item = tree->topLevelItem(static_cast<int>(i));
            const bool isCurrent = rows[i]->id() == current;
            updateRow(item, *rows[i], isCurrent, holding.count(rows[i]->id()) > 0);
            if (isCurrent)
                tree->setCurrentItem(item);
        }
        syncing = false;
        updateButtons();
    }

    void updateButtons()
    {
        const core::Page* page = session_.currentPage();
        const std::size_t count = page != nullptr ? page->layers().size() : 0;
        const auto index = page != nullptr ? layerIndex(*page, session_.currentLayer()) : std::nullopt;
        const bool hasLayer = index.has_value();
        upButton->setEnabled(hasLayer && *index + 1 < count);
        downButton->setEnabled(hasLayer && *index > 0);
        deleteButton->setEnabled(hasLayer);
        duplicateButton->setEnabled(hasLayer);
        renameButton->setEnabled(hasLayer);
        moveToButton->setEnabled(count > 0 && !session_.selection().isEmpty());
    }

    // The "Move Selection To" menu lists the layers, topmost first.
    void fillMoveToMenu()
    {
        moveToMenu->clear();
        for (const core::Cell* layer : layersTopFirst(session_.currentPage())) {
            const std::string id = layer->id();
            moveToMenu->addAction(layerTitle(*layer, false), owner_, [owner = owner_, id] {
                owner->moveSelectionToLayer(id);
            });
        }
    }

    void promptRename(const std::string& id)
    {
        const core::Page* page = session_.currentPage();
        const core::Cell* layer = page != nullptr ? page->findCell(id) : nullptr;
        if (layer == nullptr)
            return;
        bool accepted = false;
        const QString name = QInputDialog::getText(owner_, LayersPanel::tr("Rename Layer"),
                                                   LayersPanel::tr("Layer name:"), QLineEdit::Normal,
                                                   QString::fromStdString(layer->value()), &accepted);
        if (accepted)
            owner_->renameLayer(id, name);
    }

    // Moves the current layer by `delta` positions in the stack (+1 = up).
    void moveCurrentLayer(int delta)
    {
        const core::Page* page = session_.currentPage();
        const std::string id = session_.currentLayer();
        const auto index = page != nullptr ? layerIndex(*page, id) : std::nullopt;
        if (!index)
            return;
        const long long target = static_cast<long long>(*index) + delta;
        if (target < 0 || target >= static_cast<long long>(page->layers().size()))
            return;
        owner_->moveLayer(id, static_cast<std::size_t>(target));
    }
};

LayersPanel::LayersPanel(DocumentSession& session, QWidget* parent)
    : QWidget(parent), d(std::make_unique<Impl>(*this, session))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    d->tree = new QTreeWidget(this);
    d->tree->setAccessibleName(tr("Layers"));
    d->tree->setColumnCount(3);
    d->tree->setHeaderHidden(true);
    d->tree->setRootIsDecorated(false);
    d->tree->setUniformRowHeights(true);
    d->tree->setSelectionMode(QAbstractItemView::SingleSelection);
    d->tree->header()->setSectionResizeMode(kVisibleColumn, QHeaderView::ResizeToContents);
    d->tree->header()->setSectionResizeMode(kLockColumn, QHeaderView::ResizeToContents);
    d->tree->header()->setSectionResizeMode(kNameColumn, QHeaderView::Stretch);
    layout->addWidget(d->tree, 1);

    const auto makeButton = [this](const QString& text, const QString& tooltip) {
        auto* button = new QToolButton(this);
        button->setText(text);
        button->setToolTip(tooltip);
        button->setAutoRaise(true);
        return button;
    };
    auto* addButton = makeButton(tr("Add"), tr("Add Layer"));
    d->deleteButton = makeButton(tr("Delete"), tr("Delete Layer"));
    d->duplicateButton = makeButton(tr("Duplicate"), tr("Duplicate Layer"));
    d->renameButton = makeButton(tr("Rename"), tr("Rename Layer"));
    d->upButton = makeButton(tr("Up"), tr("Move Layer Up"));
    d->downButton = makeButton(tr("Down"), tr("Move Layer Down"));
    d->moveToButton = makeButton(tr("Move To"), tr("Move Selection To Layer"));
    d->moveToMenu = new QMenu(d->moveToButton);
    d->moveToButton->setMenu(d->moveToMenu);
    d->moveToButton->setPopupMode(QToolButton::InstantPopup);

    auto* buttons = new QGridLayout;
    buttons->addWidget(addButton, 0, 0);
    buttons->addWidget(d->deleteButton, 0, 1);
    buttons->addWidget(d->duplicateButton, 0, 2);
    buttons->addWidget(d->renameButton, 0, 3);
    buttons->addWidget(d->upButton, 1, 0);
    buttons->addWidget(d->downButton, 1, 1);
    buttons->addWidget(d->moveToButton, 1, 2, 1, 2);
    layout->addLayout(buttons);

    connect(addButton, &QToolButton::clicked, this, [this] { addLayer(); });
    connect(d->deleteButton, &QToolButton::clicked, this,
            [this] { removeLayer(d->session_.currentLayer()); });
    connect(d->duplicateButton, &QToolButton::clicked, this,
            [this] { duplicateLayer(d->session_.currentLayer()); });
    connect(d->renameButton, &QToolButton::clicked, this, [this] { d->promptRename(d->session_.currentLayer()); });
    connect(d->upButton, &QToolButton::clicked, this, [this] { d->moveCurrentLayer(1); });
    connect(d->downButton, &QToolButton::clicked, this, [this] { d->moveCurrentLayer(-1); });
    connect(d->moveToMenu, &QMenu::aboutToShow, this, [this] { d->fillMoveToMenu(); });

    // Checkbox clicks: the row already shows the new state; a refused edit puts it back.
    connect(d->tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (d->syncing || item == nullptr)
            return;
        const std::string id = rowLayerId(item).toStdString();
        bool applied = true;
        if (column == kVisibleColumn)
            applied = setLayerVisible(id, item->checkState(kVisibleColumn) == Qt::Checked);
        else if (column == kLockColumn)
            applied = setLayerLocked(id, item->checkState(kLockColumn) == Qt::Checked);
        if (!applied)
            d->sync();
    });
    connect(d->tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem* /*previous*/) {
                if (!d->syncing && current != nullptr)
                    d->session_.setCurrentLayer(rowLayerId(current).toStdString());
            });
    connect(d->tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (column == kNameColumn)
            d->promptRename(rowLayerId(item).toStdString());
    });

    connect(&session, &DocumentSession::documentChanged, this, [this] { d->sync(); });
    connect(&session, &DocumentSession::currentPageChanged, this, [this] { d->sync(); });
    connect(&session, &DocumentSession::currentLayerChanged, this, [this] { d->sync(); });
    connect(&session.selection(), &SelectionModel::selectionChanged, this, [this] { d->sync(); });

    d->sync();
}

LayersPanel::~LayersPanel() = default;

QTreeWidget* LayersPanel::list() const
{
    return d->tree;
}

bool LayersPanel::addLayer()
{
    const core::Page* page = d->session_.currentPage();
    if (page == nullptr)
        return false;
    const std::size_t pageIndex = d->session_.currentPageIndex();
    // Above the current layer (draw.io adds the layer right after the current one).
    std::optional<std::size_t> insertAt;
    if (const auto current = layerIndex(*page, d->session_.currentLayer()))
        insertAt = *current + 1;

    std::string created;
    const commands::Result result =
        editLayers(d->session_, tr("Add Layer"), [&](commands::Transaction& tx) {
            commands::Result added = commands::ops::addLayer(tx, pageIndex, insertAt);
            if (added.ok && !added.cells.empty())
                created = added.cells.front();
            return added;
        });
    if (result.ok && !created.empty())
        d->session_.setCurrentLayer(created);
    return result.ok;
}

bool LayersPanel::removeLayer(const std::string& layerId)
{
    const std::size_t pageIndex = d->session_.currentPageIndex();
    std::string next;
    const commands::Result result = editLayers(d->session_, tr("Delete Layer"), [&](commands::Transaction& tx) {
        commands::Result removed = commands::ops::removeLayers(tx, pageIndex, {layerId});
        if (removed.ok && !removed.cells.empty())
            next = removed.cells.front();
        return removed;
    });
    if (result.ok && !next.empty())
        d->session_.setCurrentLayer(next);
    return result.ok;
}

bool LayersPanel::duplicateLayer(const std::string& layerId)
{
    const std::size_t pageIndex = d->session_.currentPageIndex();
    std::string clone;
    const commands::Result result =
        editLayers(d->session_, tr("Duplicate Layer"), [&](commands::Transaction& tx) {
            commands::Result copied = commands::ops::duplicateLayer(tx, pageIndex, layerId);
            if (copied.ok && !copied.cells.empty())
                clone = copied.cells.front();
            return copied;
        });
    if (result.ok && !clone.empty())
        d->session_.setCurrentLayer(clone);
    return result.ok;
}

bool LayersPanel::renameLayer(const std::string& layerId, const QString& name)
{
    const std::size_t pageIndex = d->session_.currentPageIndex();
    const std::string text = name.toStdString();
    return editLayers(d->session_, tr("Rename Layer"), [pageIndex, &layerId, &text](commands::Transaction& tx) {
               return commands::ops::renameLayer(tx, pageIndex, layerId, text);
           }).ok;
}

bool LayersPanel::moveLayer(const std::string& layerId, std::size_t toIndex)
{
    const std::size_t pageIndex = d->session_.currentPageIndex();
    const core::Page* page = d->session_.currentPage();
    if (page == nullptr || layerIndex(*page, layerId) == toIndex)
        return false;
    return editLayers(d->session_, tr("Move Layer"), [pageIndex, &layerId, toIndex](commands::Transaction& tx) {
               return commands::ops::moveLayer(tx, pageIndex, layerId, toIndex);
           }).ok;
}

bool LayersPanel::setLayerVisible(const std::string& layerId, bool visible)
{
    const std::size_t pageIndex = d->session_.currentPageIndex();
    return editLayers(d->session_, visible ? tr("Show Layer") : tr("Hide Layer"),
                      [pageIndex, &layerId, visible](commands::Transaction& tx) {
                          return commands::ops::setLayerVisible(tx, pageIndex, layerId, visible);
                      })
        .ok;
}

bool LayersPanel::setLayerLocked(const std::string& layerId, bool locked)
{
    const std::size_t pageIndex = d->session_.currentPageIndex();
    return editLayers(d->session_, locked ? tr("Lock Layer") : tr("Unlock Layer"),
                      [pageIndex, &layerId, locked](commands::Transaction& tx) {
                          return commands::ops::setLayerLocked(tx, pageIndex, layerId, locked);
                      })
        .ok;
}

bool LayersPanel::moveSelectionToLayer(const std::string& layerId)
{
    const std::vector<std::string> cells = d->session_.selection().cells();
    if (cells.empty())
        return false;
    const std::size_t pageIndex = d->session_.currentPageIndex();
    return d->session_
        .edit(tr("Move Selection to Layer"),
              [pageIndex, cells, &layerId](commands::Transaction& tx) {
                  return commands::ops::reparentCells(tx, pageIndex, cells, layerId);
              })
        .ok;
}

} // namespace qraw::ui
