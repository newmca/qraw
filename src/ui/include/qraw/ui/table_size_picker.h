#pragma once
// The grid of Insert > Table (Menus.addInsertTableItem, js/grapheditor/Menus.js:805):
// hover or arrow over the cells to choose the number of columns and rows
// (1 to 20 each), click or press Enter or Space to insert. The label under
// the grid says what is chosen ("4 columns x 3 rows"). Colours come from the
// palette. In a menu the arrow keys move inside the grid and, at its edge,
// leave it (Up from the first row goes to the item above, Left from the first
// column closes the submenu).

#include <QWidget>
#include <QWidgetAction>

namespace qraw::ui {

class TableSizePicker : public QWidget {
    Q_OBJECT

public:
    static constexpr int kMaxSize = 20;

    explicit TableSizePicker(QWidget* parent = nullptr);

    [[nodiscard]] int rows() const { return rows_; }
    [[nodiscard]] int columns() const { return columns_; }
    // Clamped to 1..kMaxSize. Emits sizeChanged when the size differs.
    void setSize(int rows, int columns);
    // What the label says and what a screen reader reads: "4 columns x 3 rows".
    [[nodiscard]] QString sizeText() const;
    // The centre of the cell in `row`, `column` (zero-based), for tests and hit testing.
    [[nodiscard]] QPoint cellCenter(int row, int column) const;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

signals:
    void sizeChanged(int rows, int columns);
    // A click, or Enter or Space.
    void sizeChosen(int rows, int columns);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    [[nodiscard]] int cellSize() const;
    // The 1-based size that the cell at `pos` selects; 0, 0 outside the grid.
    [[nodiscard]] QSize sizeAt(QPoint pos) const;
    [[nodiscard]] QRect labelRect() const;

    int rows_ = 1;
    int columns_ = 1;
};

// The picker as a menu item. Every menu that gets the action shows its own picker
// (QWidgetAction::createWidget); choosing a size closes the menus and emits
// tableChosen.
class TableSizeAction : public QWidgetAction {
    Q_OBJECT

public:
    explicit TableSizeAction(QObject* parent = nullptr);

signals:
    void tableChosen(int rows, int columns);

protected:
    QWidget* createWidget(QWidget* parent) override;
};

} // namespace qraw::ui
