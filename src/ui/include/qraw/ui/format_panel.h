#pragma once
// The Format panel (right dock), draw.io's Format.js.
// (src/ui/format_panel.cpp, src/ui/format_*.cpp).

#include <QList>
#include <QWidget>

#include <memory>

class QAction;
class QTabWidget;

namespace qraw::ui {

class DocumentSession;

// Tabs depend on the selection (Format.prototype.refresh, Format.js):
//  * nothing selected: "Diagram" (DiagramFormatPanel, Format.js:9560) --
//    grid on/off + size, page view on/off, page size presets + custom +
//    portrait/landscape, background color, adaptive colors, shadow. (No math
//    typesetting box: `math=1` is kept in the file, never rendered.)
//  * vertices and/or edges: "Style" (StyleFormatPanel, :6836), "Text"
//    (TextFormatPanel, :4683), "Arrange" (ArrangePanel, :2240).
// Every control writes through DocumentSession::edit with ops::setStyles /
// ops::setPageProperties / ops::resizeCells / ops::moveCells (one undo
// entry per user action; spin boxes and
// sliders coalesce with a merge key and end it on editingFinished /
// sliderReleased). The panel refreshes on selectionChanged,
// documentChanged and currentPageChanged; a refresh never writes. Mixed
// values (StyleSummary) show an empty/indeterminate control.
class FormatPanel : public QWidget {
    Q_OBJECT

public:
    explicit FormatPanel(DocumentSession& session, QWidget* parent = nullptr);
    ~FormatPanel() override;

    // The actions that become the buttons of the Style tab (Copy Style, Paste
    // Style, Set as Default Style): the main window's, so that the buttons
    // follow the same enable rules as the menu items. Without them the tab has
    // no such buttons.
    void setStyleActions(const QList<QAction*>& actions);
    // The actions of the Data buttons (Edit Data, Edit Tooltip, Edit Link, Edit
    // Image, in that order): Edit Data on the Diagram tab, all four in a Data
    // group of the Arrange tab.
    void setDataActions(const QList<QAction*>& actions);

    // Rebuilds the tabs for the current selection (also called internally).
    void refresh();
    // The tab widget, for tests and the screenshot harness.
    [[nodiscard]] QTabWidget* tabs() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::ui
