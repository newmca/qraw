// FormatTab base: the scroll page and its content layout (see format_tab.h).
#include "format_tab.h"

#include <QFrame>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

namespace qraw::ui {

FormatTab::FormatTab(DocumentSession& session) : session_(session)
{
    auto* content = new QWidget;
    layout_ = new QVBoxLayout(content);
    layout_->setContentsMargins(8, 8, 8, 8);

    page_ = new QScrollArea;
    page_->setWidgetResizable(true);
    page_->setFrameShape(QFrame::NoFrame);
    page_->setWidget(content);
}

FormatTab::~FormatTab() = default;

} // namespace qraw::ui
