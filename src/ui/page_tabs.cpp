// Page tabs (EditorUi.createTabContainer, js/grapheditor/EditorUi.js:8966, and
// the diagramly page menu).
#include "qraw/ui/action_registry.h"
#include "qraw/ui/page_tabs.h"

#include "qraw/commands/ops_pages.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/document.h"
#include "qraw/core/page.h"
#include "qraw/ui/document_session.h"

#include <QAction>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QStringList>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>

#include <functional>
#include <optional>

namespace qraw::ui {

namespace {

// QTabBar reporting double clicks on a tab (QTabBar has no signal for them) and the
// end of a mouse press, which ends a drag of a tab.
class PageTabBar final : public QTabBar {
public:
    using TabHandler = std::function<void(int)>;

    explicit PageTabBar(QWidget* parent) : QTabBar(parent) {}

    void setTabDoubleClickHandler(TabHandler handler) { onDoubleClick_ = std::move(handler); }
    void setPressHandlers(std::function<void()> pressed, std::function<void()> released)
    {
        onPress_ = std::move(pressed);
        onRelease_ = std::move(released);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        QTabBar::mousePressEvent(event); // a click selects its tab here
        if (onPress_)
            onPress_();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QTabBar::mouseReleaseEvent(event);
        if (onRelease_)
            onRelease_();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        const int index = tabAt(event->position().toPoint());
        if (index >= 0 && onDoubleClick_)
            onDoubleClick_(index);
        else
            QTabBar::mouseDoubleClickEvent(event);
    }

private:
    TabHandler onDoubleClick_;
    std::function<void()> onPress_;
    std::function<void()> onRelease_;
};

} // namespace

// The tabs mirror the document's pages. `syncing` is set while the tabs are
// updated from the model, so that the resulting signals are not turned into
// model edits again.
struct PageTabs::Impl {
    Impl(PageTabs& owner, DocumentSession& session) : owner_(&owner), session_(session) {}

    PageTabs* owner_;
    DocumentSession& session_;
    PageTabBar* bar = nullptr;
    QToolButton* addButton = nullptr;
    bool syncing = false;
    // A tab being dragged: the bar moves the tab live. Its tabMoved(from, to) names the tabs
    // that it moves out of the way, one per tab passed (some of them only when a slide
    // animation ends, which can be after the mouse release), so the dragged tab is followed by
    // being the current one. The model moves once, as one undo step, when the drag has ended
    // and no more tabMoved is due: `dragFrom` is the page index where the tab started.
    bool pressed = false;
    bool tabsMoved = false;
    int dragFrom = -1;
    QTimer* settle = nullptr;
    static constexpr int kSettleMs = 120;

    void finishDrag()
    {
        if (pressed || !tabsMoved)
            return;
        tabsMoved = false;
        const int to = bar->currentIndex();
        if (dragFrom >= 0 && to >= 0 && dragFrom != to
            && !owner_->movePage(static_cast<std::size_t>(dragFrom), static_cast<std::size_t>(to)))
            sync(); // refused: put the tabs back
    }

    // The tab title of every page: its name, or "Page-N" for an unnamed page.
    [[nodiscard]] QStringList titles() const
    {
        QStringList result;
        const auto pages = session_.document().pages();
        for (std::size_t i = 0; i < pages.size(); ++i) {
            const QString name = QString::fromStdString(pages[i]->name());
            result << (name.isEmpty() ? PageTabs::tr("Page-%1").arg(i + 1) : name);
        }
        return result;
    }

    [[nodiscard]] QStringList tabTitles() const
    {
        QStringList result;
        for (int i = 0; i < bar->count(); ++i)
            result << bar->tabText(i);
        return result;
    }

    // Brings the tabs in line with the model. The tabs are rebuilt only when
    // the titles differ, so a tab moved by the user stays where it is.
    void sync()
    {
        const QStringList wanted = titles();
        syncing = true;
        if (tabTitles() != wanted) {
            while (bar->count() > 0)
                bar->removeTab(0);
            for (const QString& title : wanted)
                bar->addTab(title);
        }
        const int current = session_.currentPage() != nullptr ? static_cast<int>(session_.currentPageIndex()) : -1;
        if (bar->currentIndex() != current)
            bar->setCurrentIndex(current);
        syncing = false;
    }

    // Runs one page edit as one undo step.
    bool edit(const QString& name, const commands::Editor::EditFunction& fn)
    {
        return session_.edit(name, fn).ok;
    }

    void promptRename(int index)
    {
        const core::Page* page = session_.document().page(static_cast<std::size_t>(index));
        if (page == nullptr)
            return;
        bool accepted = false;
        const QString name = QInputDialog::getText(owner_, PageTabs::tr("Rename Page"), PageTabs::tr("Page name:"),
                                                   QLineEdit::Normal, QString::fromStdString(page->name()), &accepted);
        if (accepted)
            owner_->renamePage(static_cast<std::size_t>(index), name);
    }

    void showTabMenu(const QPoint& pos)
    {
        const int clicked = bar->tabAt(pos);
        if (clicked >= 0 && static_cast<std::size_t>(clicked) != session_.currentPageIndex())
            session_.setCurrentPage(static_cast<std::size_t>(clicked));

        const std::size_t index = session_.currentPageIndex();
        const std::size_t count = session_.document().pageCount();
        QMenu menu(owner_);
        menu.addAction(PageTabs::tr("Insert Page"), owner_, [this] { owner_->insertPage(); });
        menu.addAction(PageTabs::tr("Duplicate"), owner_, [this] { owner_->duplicateCurrentPage(); });
        menu.addAction(withEllipsis(PageTabs::tr("Rename")), owner_,
                       [this, index] { promptRename(static_cast<int>(index)); });
        QAction* remove = menu.addAction(PageTabs::tr("Delete"), owner_, [this] { owner_->removeCurrentPage(); });
        remove->setEnabled(count > 1);
        menu.addSeparator();
        QAction* left = menu.addAction(PageTabs::tr("Move Left"), owner_,
                                       [this, index] { owner_->movePage(index, index - 1); });
        left->setEnabled(index > 0);
        QAction* right = menu.addAction(PageTabs::tr("Move Right"), owner_,
                                        [this, index] { owner_->movePage(index, index + 1); });
        right->setEnabled(index + 1 < count);
        menu.exec(bar->mapToGlobal(pos));
    }
};

PageTabs::PageTabs(DocumentSession& session, QWidget* parent)
    : QWidget(parent), d(std::make_unique<Impl>(*this, session))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    d->bar = new PageTabBar(this);
    d->bar->setDocumentMode(true);
    d->bar->setMovable(true);
    d->bar->setExpanding(false);
    d->bar->setElideMode(Qt::ElideRight);
    d->bar->setUsesScrollButtons(true);
    d->bar->setAccessibleName(tr("Pages"));
    d->bar->setContextMenuPolicy(Qt::CustomContextMenu);
    d->bar->setTabDoubleClickHandler([this](int index) { d->promptRename(index); });
    layout->addWidget(d->bar, 1);

    d->addButton = new QToolButton(this);
    d->addButton->setText(QStringLiteral("+"));
    d->addButton->setToolTip(tr("Insert Page"));
    d->addButton->setAccessibleName(tr("Insert Page")); // its text is only "+"
    d->addButton->setAutoRaise(true);
    layout->addWidget(d->addButton);

    connect(d->bar, &QTabBar::currentChanged, this, [this](int index) {
        // While a tab is held, the bar changes its current index as the tab passes others; that
        // is not a choice of page (the click that picked the tab has been handled already).
        if (!d->syncing && !d->pressed && index >= 0)
            d->session_.setCurrentPage(static_cast<std::size_t>(index));
    });
    d->settle = new QTimer(this);
    d->settle->setSingleShot(true);
    d->settle->setInterval(Impl::kSettleMs);
    connect(d->settle, &QTimer::timeout, this, [this] { d->finishDrag(); });
    d->bar->setPressHandlers(
        [this] {
            d->pressed = true;
            d->dragFrom = d->bar->currentIndex();
        },
        [this] {
            d->pressed = false;
            if (d->tabsMoved)
                d->settle->start();
        });
    connect(d->bar, &QTabBar::tabMoved, this, [this] {
        if (d->syncing)
            return;
        d->tabsMoved = true; // the move is committed when the drag has settled
        if (!d->pressed)
            d->settle->start();
    });
    connect(d->bar, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { d->showTabMenu(pos); });
    connect(d->addButton, &QToolButton::clicked, this, [this] { insertPage(); });
    connect(&session, &DocumentSession::documentChanged, this, [this] { d->sync(); });
    connect(&session, &DocumentSession::currentPageChanged, this, [this] { d->sync(); });

    d->sync();
}

PageTabs::~PageTabs() = default;

QTabBar* PageTabs::tabBar() const
{
    return d->bar;
}

bool PageTabs::insertPage()
{
    // draw.io inserts the new page after the current one (EditorUi.insertPage).
    const std::size_t index = d->session_.currentPageIndex() + 1;
    return d->edit(tr("Insert Page"), [index](commands::Transaction& tx) {
        return commands::ops::addPage(tx, index, std::nullopt);
    });
}

bool PageTabs::duplicateCurrentPage()
{
    const std::size_t index = d->session_.currentPageIndex();
    return d->edit(tr("Duplicate Page"),
                   [index](commands::Transaction& tx) { return commands::ops::duplicatePage(tx, index); });
}

bool PageTabs::removeCurrentPage()
{
    if (d->session_.document().pageCount() <= 1)
        return false; // draw.io offers Delete only with more than one page
    const std::size_t index = d->session_.currentPageIndex();
    return d->edit(tr("Delete Page"),
                   [index](commands::Transaction& tx) { return commands::ops::removePage(tx, index); });
}

bool PageTabs::renamePage(std::size_t index, const QString& name)
{
    const core::Page* page = d->session_.document().page(index);
    if (page == nullptr || QString::fromStdString(page->name()) == name)
        return false;
    const std::string text = name.toStdString();
    return d->edit(tr("Rename Page"),
                   [index, text](commands::Transaction& tx) { return commands::ops::renamePage(tx, index, text); });
}

bool PageTabs::movePage(std::size_t from, std::size_t to)
{
    if (from == to || from >= d->session_.document().pageCount())
        return false;
    return d->edit(tr("Move Page"),
                   [from, to](commands::Transaction& tx) { return commands::ops::movePage(tx, from, to); });
}

} // namespace qraw::ui
