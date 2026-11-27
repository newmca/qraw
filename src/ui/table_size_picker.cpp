#include "qraw/ui/table_size_picker.h"

#include <QAccessible>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>

#include <algorithm>

namespace qraw::ui {

namespace {

struct Text {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::TableSizePicker)
};

constexpr int kMargin = 8;
constexpr int kLabelGap = 6;

} // namespace

TableSizePicker::TableSizePicker(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("TableSizePicker"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(Text::tr("Table Size"));
    setAccessibleDescription(sizeText());
    setToolTip(Text::tr("Choose the number of columns and rows of the table"));
}

QString TableSizePicker::sizeText() const
{
    const QString columns = columns_ == 1 ? Text::tr("1 column") : Text::tr("%1 columns").arg(columns_);
    const QString rows = rows_ == 1 ? Text::tr("1 row") : Text::tr("%1 rows").arg(rows_);
    return Text::tr("%1 x %2").arg(columns, rows);
}

void TableSizePicker::setSize(int rows, int columns)
{
    rows = std::clamp(rows, 1, kMaxSize);
    columns = std::clamp(columns, 1, kMaxSize);
    if (rows == rows_ && columns == columns_)
        return;
    rows_ = rows;
    columns_ = columns;
    setAccessibleDescription(sizeText());
    if (QAccessible::isActive()) {
        QAccessibleEvent event(this, QAccessible::DescriptionChanged);
        QAccessible::updateAccessibility(&event);
    }
    update();
    emit sizeChanged(rows_, columns_);
}

int TableSizePicker::cellSize() const
{
    return std::max(12, static_cast<int>(fontMetrics().height() * 0.85));
}

QRect TableSizePicker::labelRect() const
{
    const int grid = kMargin + kMaxSize * cellSize();
    return QRect(kMargin, grid + kLabelGap, kMaxSize * cellSize(), fontMetrics().height() + 4);
}

QSize TableSizePicker::sizeHint() const
{
    const QRect label = labelRect();
    return {2 * kMargin + kMaxSize * cellSize(), label.bottom() + kMargin};
}

QPoint TableSizePicker::cellCenter(int row, int column) const
{
    const int size = cellSize();
    return {kMargin + column * size + size / 2, kMargin + row * size + size / 2};
}

QSize TableSizePicker::sizeAt(QPoint pos) const
{
    const int size = cellSize();
    const int column = (pos.x() - kMargin) / size;
    const int row = (pos.y() - kMargin) / size;
    if (pos.x() < kMargin || pos.y() < kMargin || column >= kMaxSize || row >= kMaxSize)
        return {};
    return {column + 1, row + 1};
}

void TableSizePicker::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const int size = cellSize();
    const QPalette& colors = palette();
    const QColor line = colors.color(QPalette::Mid);
    QColor fill = colors.color(QPalette::Highlight);
    fill.setAlpha(110);
    for (int row = 0; row < kMaxSize; ++row) {
        for (int column = 0; column < kMaxSize; ++column) {
            const QRect cell(kMargin + column * size, kMargin + row * size, size, size);
            const bool chosen = row < rows_ && column < columns_;
            painter.setPen(chosen ? colors.color(QPalette::Highlight) : line);
            painter.setBrush(chosen ? QBrush(fill) : QBrush(Qt::NoBrush));
            painter.drawRect(cell.adjusted(0, 0, -1, -1));
        }
    }
    painter.setPen(colors.color(QPalette::WindowText));
    painter.drawText(labelRect(), Qt::AlignCenter, sizeText());
    if (hasFocus()) {
        // The focus frame of the whole grid, so that keyboard users can see where the focus is.
        painter.setPen(QPen(colors.color(QPalette::Highlight), 1, Qt::DotLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRect(kMargin - 3, kMargin - 3, kMaxSize * size + 5, kMaxSize * size + 5));
    }
}

void TableSizePicker::mouseMoveEvent(QMouseEvent* event)
{
    const QSize size = sizeAt(event->position().toPoint());
    if (size.isValid())
        setSize(size.height(), size.width());
    event->accept();
}

void TableSizePicker::mousePressEvent(QMouseEvent* event)
{
    const QSize size = sizeAt(event->position().toPoint());
    if (event->button() != Qt::LeftButton || !size.isValid()) {
        event->accept(); // a click beside the grid must not close the menu
        return;
    }
    setSize(size.height(), size.width());
    emit sizeChosen(rows_, columns_);
    event->accept();
}

void TableSizePicker::keyPressEvent(QKeyEvent* event)
{
    int dRows = 0;
    int dColumns = 0;
    switch (event->key()) {
    case Qt::Key_Up: dRows = -1; break;
    case Qt::Key_Down: dRows = 1; break;
    case Qt::Key_Left: dColumns = -1; break;
    case Qt::Key_Right: dColumns = 1; break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        emit sizeChosen(rows_, columns_);
        event->accept();
        return;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    const int rows = rows_ + dRows;
    const int columns = columns_ + dColumns;
    // At the edge of the grid the key is the menu's: it moves to another item.
    if (rows < 1 || rows > kMaxSize || columns < 1 || columns > kMaxSize) {
        event->ignore();
        return;
    }
    setSize(rows, columns);
    event->accept();
}

void TableSizePicker::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    setSize(1, 1); // every opening of the menu starts again
    // QMenu does not hand its keys to a widget action: the picker asks for them, so that the
    // arrow keys, Enter and Space work in the open submenu.
    setFocus(Qt::PopupFocusReason);
}

TableSizeAction::TableSizeAction(QObject* parent) : QWidgetAction(parent)
{
    setObjectName(QStringLiteral("action.insertTable"));
}

QWidget* TableSizeAction::createWidget(QWidget* parent)
{
    auto* picker = new TableSizePicker(parent);
    connect(picker, &TableSizePicker::sizeChosen, this, [this, picker](int rows, int columns) {
        // Close every menu the picker is in, then insert (the canvas gets its focus back first).
        for (QWidget* widget = picker; widget != nullptr; widget = widget->parentWidget()) {
            if (auto* menu = qobject_cast<QMenu*>(widget))
                menu->close();
        }
        emit tableChosen(rows, columns);
    });
    return picker;
}

} // namespace qraw::ui
