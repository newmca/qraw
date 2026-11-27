#include "palette_flow_layout.h"

#include <QWidget>

#include <algorithm>

namespace qraw::ui {

FlowLayout::FlowLayout(QWidget* parent, int margin, int spacing) : QLayout(parent), spacing_(spacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem* item = takeAt(0))
        delete item;
}

void FlowLayout::addItem(QLayoutItem* item)
{
    items_.append(item);
}

int FlowLayout::count() const
{
    return static_cast<int>(items_.size());
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return items_.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    return index >= 0 && index < items_.size() ? items_.takeAt(index) : nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return {};
}

bool FlowLayout::hasHeightForWidth() const
{
    return true;
}

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, width, 0), true);
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const
{
    return minimumSize();
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* item : items_)
        size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
    const QMargins m = contentsMargins();
    const QRect area = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = area.x();
    int y = area.y();
    int rowHeight = 0;
    for (QLayoutItem* item : items_) {
        if (item->isEmpty())
            continue;
        const QSize hint = item->sizeHint();
        int nextX = x + hint.width() + spacing_;
        if (nextX - spacing_ > area.right() + 1 && rowHeight > 0) {
            x = area.x();
            y += rowHeight + spacing_;
            nextX = x + hint.width() + spacing_;
            rowHeight = 0;
        }
        if (!testOnly)
            item->setGeometry(QRect(QPoint(x, y), hint));
        x = nextX;
        rowHeight = std::max(rowHeight, hint.height());
    }
    return y + rowHeight - rect.y() + m.bottom();
}

} // namespace qraw::ui
