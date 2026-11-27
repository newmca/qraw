#pragma once
// Page tabs below the canvas (EditorUi.createTabContainer,
// js/grapheditor/EditorUi.js:8966, and the diagramly page menu). Implemented in
// src/ui/page_tabs.cpp.

#include <QWidget>

#include <memory>

class QTabBar;

namespace qraw::ui {

class DocumentSession;

// A QTabBar with one tab per page (Page::name(), "Page-N" fallback for
// unnamed pages) plus a "+" button. Behaviour:
//  * click a tab -> DocumentSession::setCurrentPage;
//  * "+" button / context menu "Insert Page" -> ops::addPage after the
//    current page (draw.io inserts after the current page) and switch to it;
//  * double-click a tab / context "Rename..." -> QInputDialog -> ops::renamePage;
//  * context "Duplicate" -> ops::duplicatePage; "Delete" -> ops::removePage
//    (disabled for the last page); "Move Left/Right" and drag-reordering
//    (QTabBar::setMovable, tabMoved) -> ops::movePage;
//  * all through DocumentSession::edit (Result.page switches the page);
//  * rebuilds the tabs on documentChanged (pagesAdded/Removed/Reordered,
//    page properties for names, Reset) and follows currentPageChanged.
class PageTabs : public QWidget {
    Q_OBJECT

public:
    explicit PageTabs(DocumentSession& session, QWidget* parent = nullptr);
    ~PageTabs() override;

    [[nodiscard]] QTabBar* tabBar() const;

    // The actions behind the buttons/menu, callable from menus and tests.
    // Each returns true when the model changed.
    bool insertPage();
    bool duplicateCurrentPage();
    bool removeCurrentPage();
    bool renamePage(std::size_t index, const QString& name);
    bool movePage(std::size_t from, std::size_t to);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::ui
