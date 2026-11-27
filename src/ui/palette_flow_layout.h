#pragma once
// A layout that places its items left to right and wraps them into rows
// (the Qt "Flow Layout" example). Used for the thumbnail grids of the palette
// dock. Private to the palette package.

#include <QLayout>
#include <QList>
#include <QRect>

namespace qraw::ui {

class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr, int margin = 0, int spacing = 2);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    [[nodiscard]] int count() const override;
    [[nodiscard]] QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;
    [[nodiscard]] Qt::Orientations expandingDirections() const override;
    [[nodiscard]] bool hasHeightForWidth() const override;
    [[nodiscard]] int heightForWidth(int width) const override;
    [[nodiscard]] QSize minimumSize() const override;
    [[nodiscard]] QSize sizeHint() const override;
    void setGeometry(const QRect& rect) override;

private:
    // Lays the items out inside `rect`; moves them unless `testOnly`.
    int doLayout(const QRect& rect, bool testOnly) const;

    QList<QLayoutItem*> items_;
    int spacing_;
};

} // namespace qraw::ui
