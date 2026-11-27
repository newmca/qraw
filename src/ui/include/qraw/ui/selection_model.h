#pragma once
// The selected cells of the current page (draw.io's mxGraphSelectionModel).
// Implemented in src/ui/selection_model.cpp.

#include <QObject>

#include <string>
#include <string_view>
#include <vector>

namespace qraw::ui {

// Ordered cell ids on the session's current page. The order is the order of
// selection (draw.io keeps it too: the first cell drives the Format panel
// for "copy size" and the align reference). Duplicates are never stored.
//
// The model never validates ids against the page itself; DocumentSession
// prunes ids that a ChangeSet removed and clears the selection when the
// current page changes or the document is reset.
class SelectionModel : public QObject {
    Q_OBJECT

public:
    explicit SelectionModel(QObject* parent = nullptr);

    [[nodiscard]] const std::vector<std::string>& cells() const { return cells_; }
    [[nodiscard]] bool isEmpty() const { return cells_.empty(); }
    [[nodiscard]] std::size_t size() const { return cells_.size(); }
    [[nodiscard]] bool contains(std::string_view id) const;
    // The first selected cell or "" (mxGraph.getSelectionCell).
    [[nodiscard]] std::string first() const { return cells_.empty() ? std::string() : cells_.front(); }

    // Each mutator emits selectionChanged() once, and only when the content
    // or order actually changed.
    void set(std::vector<std::string> ids);
    void add(const std::vector<std::string>& ids);
    void remove(const std::vector<std::string>& ids);
    // Removes the id if present, appends it otherwise (shift/ctrl click,
    // mxGraph.selectCellForEvent with a toggle event).
    void toggle(const std::string& id);
    void clear();

signals:
    void selectionChanged();

private:
    void assign(std::vector<std::string> ids);

    std::vector<std::string> cells_;
};

} // namespace qraw::ui
