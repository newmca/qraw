#pragma once
// The Cells panel: a list of every cell of the current page as a tree (layers, then the cells
// in them, children nested), for keyboard and screen reader users who cannot point at the canvas
// (D-050). draw.io has no such panel.

#include <QAbstractItemModel>
#include <QWidget>

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class QTreeView;

namespace qraw::core {
class Page;
}
namespace qraw::commands {
class ChangeSet;
}

namespace qraw::ui {

class CanvasView;
class DocumentSession;

// A read-only model of the session's current page. Rows are layers at the top and below each the
// cells of it in document order (a container's children nested under it). The model follows the
// session: an edit that keeps the tree (only the content of cells changed) is reported as dataChanged
// for the rows concerned, which includes the connectors attached to a changed cell because their text
// names their ends; anything else rebuilds, as DiagramScene decides for the projection. While the model
// is inactive (the panel is hidden) it does no work at all: a change only marks it stale and the next
// activation rebuilds.
class CellTreeModel final : public QAbstractItemModel {
    Q_OBJECT

public:
    enum Role {
        CellIdRole = Qt::UserRole, // the cell id (QString)
    };

    explicit CellTreeModel(DocumentSession& session, QObject* parent = nullptr);
    ~CellTreeModel() override;

    // Inactive models ignore the session; becoming active catches up when something changed meanwhile.
    void setActive(bool active);
    [[nodiscard]] bool isActive() const { return active_; }
    // How many times the whole tree was rebuilt (tests: an edit that keeps the tree must not add to it).
    [[nodiscard]] std::size_t rebuildCount() const { return rebuilds_; }

    [[nodiscard]] QModelIndex indexOfCell(const std::string& id) const;
    [[nodiscard]] std::string cellId(const QModelIndex& index) const;

    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& index) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    struct Node {
        std::string id;
        int parent = -1; // index in nodes_; 0 is the page's root cell, which is not a row
        int row = 0;     // position among the parent's children
        std::vector<int> children;
    };

    void rebuild();
    void onDocumentChanged(const commands::ChangeSet& changes);
    [[nodiscard]] const core::Page* page() const;

    DocumentSession& session_;
    std::vector<Node> nodes_;
    std::unordered_map<std::string, int> byId_;
    bool active_ = false;
    bool stale_ = true;
    std::size_t rebuilds_ = 0;
};

class CellsPanel : public QWidget {
    Q_OBJECT

public:
    explicit CellsPanel(CanvasView& canvas, QWidget* parent = nullptr);
    ~CellsPanel() override;

    [[nodiscard]] QTreeView* tree() const;
    [[nodiscard]] CellTreeModel* model() const;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::ui
