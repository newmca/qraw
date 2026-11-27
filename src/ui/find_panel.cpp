// Find / Replace (draw.io's FindWindow, Ctrl+F).
#include "qraw/ui/find_panel.h"

#include "docks_find_engine.h"

#include "qraw/commands/ops_properties.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/cell.h"
#include "qraw/core/document.h"
#include "qraw/core/page.h"
#include "qraw/ui/document_session.h"
#include "qraw/ui/selection_model.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace qraw::ui {

namespace {

constexpr char kLabelAttribute[] = "label";

// Two matches name the same value (page, cell and attribute).
bool sameTarget(const FindMatch& a, const FindMatch& b)
{
    return a.page == b.page && a.cellId == b.cellId && a.attribute == b.attribute;
}

// The index of the first match at or after `start`, wrapping around, that
// is not `skip`.
std::optional<std::size_t> nextMatchIndex(const std::vector<FindMatch>& matches, std::size_t start,
                                          const std::optional<FindMatch>& skip)
{
    for (std::size_t offset = 0; offset < matches.size(); ++offset) {
        const std::size_t index = (start + offset) % matches.size();
        if (skip && sameTarget(matches[index], *skip))
            continue;
        return index;
    }
    return std::nullopt;
}

// The label or the custom attribute value a match refers to.
std::string matchedValue(const core::Cell& cell, const std::string& attribute)
{
    if (attribute == kLabelAttribute)
        return cell.value();
    return cell.wrapper() != nullptr ? cell.wrapper()->attributes.value(attribute) : std::string();
}

struct Change {
    FindMatch match;
    std::string after;
};

} // namespace

struct FindPanel::Impl {
    explicit Impl(DocumentSession& session) : session_(session), origin(session.currentPageIndex()) {}

    DocumentSession& session_;
    // The page the search started from ("All pages" lists it first). It is
    // fixed while the query and options stay the same, so the order of the
    // matches does not change as the selection moves between pages.
    std::size_t origin;
    QLineEdit* searchEdit = nullptr;
    QLineEdit* replaceEdit = nullptr;
    QCheckBox* matchCase = nullptr;
    QCheckBox* wholeWord = nullptr;
    QCheckBox* regex = nullptr;
    QCheckBox* allPages = nullptr;
    QCheckBox* includeData = nullptr;
    QCheckBox* shapes = nullptr;
    QLabel* status = nullptr;
    std::vector<FindMatch> matches;     // the matches of the last selection
    std::optional<std::size_t> current; // index into `matches` of the selected match

    [[nodiscard]] FindOptions options() const
    {
        FindOptions options;
        options.matchCase = matchCase->isChecked();
        options.wholeWord = wholeWord->isChecked();
        options.regex = regex->isChecked();
        options.allPages = allPages->isChecked();
        options.includeData = includeData->isChecked();
        options.shapes = shapes->isChecked();
        return options;
    }

    [[nodiscard]] std::vector<FindMatch> findMatches() const
    {
        return FindEngine::findAll(session_.document(), origin, searchEdit->text(), options());
    }

    // The query or an option changed: the next search starts afresh from the current page.
    void restartCycle()
    {
        current.reset();
        origin = session_.currentPageIndex();
    }

    // Searches afresh and selects the first match at or after `start` (not
    // `skip`), switching page when needed. Returns the selected match.
    std::optional<FindMatch> selectFrom(std::size_t start, const std::optional<FindMatch>& skip)
    {
        current.reset();
        const QString text = searchEdit->text();
        if (text.isEmpty()) {
            status->clear();
            return std::nullopt;
        }
        if (!docks::findPattern(text, options()).isValid()) {
            status->setText(FindPanel::tr("Invalid regular expression"));
            return std::nullopt;
        }
        matches = findMatches();
        const std::optional<std::size_t> index = nextMatchIndex(matches, start, skip);
        if (!index) {
            status->setText(FindPanel::tr("No results"));
            return std::nullopt;
        }
        current = index;
        const FindMatch match = matches[*index];
        if (session_.currentPageIndex() != match.page)
            session_.setCurrentPage(match.page);
        session_.selection().set({match.cellId});
        status->setText(FindPanel::tr("%1 of %2")
                            .arg(static_cast<int>(*index + 1))
                            .arg(static_cast<int>(matches.size())));
        return match;
    }

    // Replaces the value of every target that changes, as one undo step.
    // Returns the number of values replaced.
    int replaceMatches(const std::vector<FindMatch>& targets, const QString& name)
    {
        const QString text = searchEdit->text();
        const QString replacement = replaceEdit->text();
        const FindOptions searchOptions = options();
        std::vector<Change> changes;
        for (const FindMatch& target : targets) {
            if (target.attribute == kShapeMatchAttribute)
                continue; // styles are never edited by Replace
            const core::Page* page = session_.document().page(target.page);
            const core::Cell* cell = page != nullptr ? page->findCell(target.cellId) : nullptr;
            if (cell == nullptr)
                continue;
            const std::string before = matchedValue(*cell, target.attribute);
            const bool html = target.attribute == kLabelAttribute && docks::isHtmlLabel(*cell);
            std::string after = FindEngine::replaceIn(before, html, text, replacement, searchOptions);
            if (after != before)
                changes.push_back(Change{target, std::move(after)});
        }
        if (changes.empty())
            return 0;

        const commands::Result result = session_.edit(name, [changes](commands::Transaction& tx) {
            for (const Change& change : changes) {
                const FindMatch& match = change.match;
                const commands::Result written =
                    match.attribute == kLabelAttribute
                        ? commands::ops::setLabel(tx, match.page, match.cellId, change.after)
                        : commands::ops::setCellData(tx, match.page, match.cellId, match.attribute, change.after);
                if (!written.ok)
                    return written;
            }
            return commands::Result::success();
        });
        return result.ok ? static_cast<int>(changes.size()) : 0;
    }
};

FindPanel::FindPanel(DocumentSession& session, QWidget* parent) : QWidget(parent), d(std::make_unique<Impl>(session))
{
    auto* root = new QVBoxLayout(this);

    d->searchEdit = new QLineEdit(this);
    d->searchEdit->setPlaceholderText(tr("Find"));
    d->searchEdit->setAccessibleName(tr("Find"));
    d->replaceEdit = new QLineEdit(this);
    d->replaceEdit->setPlaceholderText(tr("Replace"));
    d->replaceEdit->setAccessibleName(tr("Replace With"));
    auto* findButton = new QPushButton(tr("Find"), this);
    auto* replaceButton = new QPushButton(tr("Replace"), this);
    auto* replaceAllButton = new QPushButton(tr("Replace All"), this);
    auto* selectAllButton = new QPushButton(tr("Select All"), this);

    d->matchCase = new QCheckBox(tr("Match case"), this);
    d->wholeWord = new QCheckBox(tr("Whole words"), this);
    d->regex = new QCheckBox(tr("Regular expression"), this);
    d->allPages = new QCheckBox(tr("All pages"), this);
    d->includeData = new QCheckBox(tr("Include data"), this);
    d->includeData->setChecked(true);
    d->shapes = new QCheckBox(tr("Shape types"), this);
    d->status = new QLabel(this);

    auto* findRow = new QHBoxLayout;
    findRow->addWidget(d->searchEdit, 1);
    findRow->addWidget(findButton);
    auto* replaceRow = new QHBoxLayout;
    replaceRow->addWidget(d->replaceEdit, 1);
    replaceRow->addWidget(replaceButton);
    replaceRow->addWidget(replaceAllButton);
    replaceRow->addWidget(selectAllButton);
    auto* optionRow = new QGridLayout;
    optionRow->addWidget(d->matchCase, 0, 0);
    optionRow->addWidget(d->wholeWord, 0, 1);
    optionRow->addWidget(d->regex, 1, 0);
    optionRow->addWidget(d->allPages, 1, 1);
    optionRow->addWidget(d->includeData, 2, 0);
    optionRow->addWidget(d->shapes, 2, 1);

    root->addLayout(findRow);
    root->addLayout(replaceRow);
    root->addLayout(optionRow);
    root->addWidget(d->status);
    root->addStretch(1);

    connect(d->searchEdit, &QLineEdit::returnPressed, this, [this] { findNext(); });
    connect(findButton, &QPushButton::clicked, this, [this] { findNext(); });
    connect(replaceButton, &QPushButton::clicked, this, [this] { replaceCurrent(); });
    connect(replaceAllButton, &QPushButton::clicked, this, [this] { replaceAll(); });
    connect(selectAllButton, &QPushButton::clicked, this, [this] { selectAll(); });
    // Changing the query or the options starts the cycle again.
    connect(d->searchEdit, &QLineEdit::textChanged, this, [this] { d->restartCycle(); });
    for (QCheckBox* box : {d->matchCase, d->wholeWord, d->regex, d->allPages, d->includeData, d->shapes}) {
        connect(box, &QCheckBox::toggled, this, [this] { d->restartCycle(); });
    }
}

FindPanel::~FindPanel() = default;

void FindPanel::setSearchText(const QString& text)
{
    d->searchEdit->setText(text);
}

void FindPanel::setReplaceText(const QString& text)
{
    d->replaceEdit->setText(text);
}

void FindPanel::setOptions(const FindOptions& options)
{
    d->matchCase->setChecked(options.matchCase);
    d->wholeWord->setChecked(options.wholeWord);
    d->regex->setChecked(options.regex);
    d->allPages->setChecked(options.allPages);
    d->includeData->setChecked(options.includeData);
    d->shapes->setChecked(options.shapes);
}

bool FindPanel::findNext()
{
    const std::size_t start = d->current ? *d->current + 1 : 0;
    const std::optional<FindMatch> match = d->selectFrom(start, std::nullopt);
    if (!match)
        return false;
    emit revealCell(QString::fromStdString(match->cellId));
    return true;
}

bool FindPanel::replaceCurrent()
{
    if (!d->current || *d->current >= d->matches.size())
        return findNext();
    const FindMatch target = d->matches[*d->current];
    if (target.attribute == kShapeMatchAttribute)
        return findNext(); // nothing to replace here
    if (d->replaceMatches({target}, tr("Replace")) == 0)
        return false;
    // The replaced match is gone (or is skipped if it still matches), so the
    // search continues at the same index.
    if (const std::optional<FindMatch> next = d->selectFrom(*d->current, target))
        emit revealCell(QString::fromStdString(next->cellId));
    return true;
}

int FindPanel::replaceAll()
{
    d->restartCycle();
    const int count = d->replaceMatches(d->findMatches(), tr("Replace All"));
    d->status->setText(tr("Replaced %1").arg(count));
    return count;
}

int FindPanel::selectAll()
{
    d->current.reset();
    const std::size_t page = d->session_.currentPageIndex();
    std::vector<std::string> ids;
    for (const FindMatch& match : d->findMatches()) {
        if (match.page == page && std::find(ids.begin(), ids.end(), match.cellId) == ids.end())
            ids.push_back(match.cellId);
    }
    const int count = static_cast<int>(ids.size());
    d->session_.selection().set(std::move(ids));
    d->status->setText(tr("%1 selected").arg(count));
    return count;
}

void FindPanel::activate()
{
    d->searchEdit->setFocus();
    d->searchEdit->selectAll();
}

} // namespace qraw::ui
