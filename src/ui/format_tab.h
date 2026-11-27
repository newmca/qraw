#pragma once
// Base class of the Format panel tabs and their factories (draw.io's
// DiagramFormatPanel, StyleFormatPanel, TextFormatPanel and ArrangePanel,
// Format.js).

#include "qraw/ui/style_summary.h"

#include <QList>

#include <memory>

class QAction;
class QScrollArea;
class QVBoxLayout;
class QWidget;

namespace qraw::ui {

class DocumentSession;

// One tab of the panel: a scroll page whose controls show the session's
// values. Controls are built once (by the subclass constructor); refresh()
// only fills them, with signals blocked, and never writes. Lambdas attached
// to controls capture the tab, so the panel keeps a replaced tab alive for
// its own lifetime (its pages are deleted with deleteLater).
class FormatTab {
public:
    explicit FormatTab(DocumentSession& session);
    virtual ~FormatTab();

    FormatTab(const FormatTab&) = delete;
    FormatTab& operator=(const FormatTab&) = delete;

    // The scroll page for the panel's tab widget (ownership passes to it).
    [[nodiscard]] QScrollArea* page() const { return page_; }

    // Shows the values of the selection described by `summary`.
    virtual void refresh(const StyleSummary& summary) = 0;

protected:
    [[nodiscard]] DocumentSession& session() const { return session_; }
    [[nodiscard]] QVBoxLayout* contentLayout() const { return layout_; }

private:
    DocumentSession& session_;
    QScrollArea* page_ = nullptr;
    QVBoxLayout* layout_ = nullptr;
};

// The tab of the panel shown when nothing is selected.
// `dataActions` (Edit Data, Edit Tooltip, Edit Link, Edit Image): the Diagram tab
// gets a button for the first (the diagram's own data), the Arrange tab a Data
// group with all of them; none when empty.
std::unique_ptr<FormatTab> makeDiagramTab(DocumentSession& session, const QList<QAction*>& dataActions = {});
// The tabs shown when cells are selected.
// `styleActions` (Copy Style, Paste Style, Set as Default Style) become the
// buttons below the style controls; none when empty.
std::unique_ptr<FormatTab> makeStyleTab(DocumentSession& session, const QList<QAction*>& styleActions = {});
std::unique_ptr<FormatTab> makeTextTab(DocumentSession& session);
std::unique_ptr<FormatTab> makeArrangeTab(DocumentSession& session, const QList<QAction*>& dataActions = {});

} // namespace qraw::ui
