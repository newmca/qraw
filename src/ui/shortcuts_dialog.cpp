#include "qraw/ui/shortcuts_dialog.h"

#include "qraw/ui/action_registry.h"

#include <QAction>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFont>
#include <QKeySequence>
#include <QStringList>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace qraw::ui {
namespace {

constexpr int kFilterColumnCount = 2;

QString shortcutsText(const QList<QKeySequence>& shortcuts,
                      QKeySequence::SequenceFormat format = QKeySequence::NativeText)
{
    QStringList parts;
    for (const QKeySequence& sequence : shortcuts)
        parts.push_back(sequence.toString(format));
    return parts.join(QStringLiteral(", "));
}

// Extra text the filter matches besides the visible columns: the portable
// spelling ("Ctrl+Z"), because macOS shows native glyphs ("\u2318Z").
constexpr int kSearchTextRole = Qt::UserRole + 1;

// Menu texts carry mnemonics ("&Open"); the table shows the plain text.
QString plainText(QString text)
{
    return text.remove(QLatin1Char('&'));
}

QTreeWidgetItem* addCategory(QTreeWidget& tree, const QString& title)
{
    auto* category = new QTreeWidgetItem(&tree, {title});
    category->setFlags(category->flags() & ~Qt::ItemIsSelectable);
    QFont font = category->font(0);
    font.setBold(true);
    category->setFont(0, font);
    return category;
}

void addRow(QTreeWidgetItem& category, const QString& action, const QString& shortcuts,
            const QString& searchText = {})
{
    auto* row = new QTreeWidgetItem(&category, {action, shortcuts});
    row->setData(1, kSearchTextRole, searchText);
}

} // namespace

struct ShortcutsDialog::Impl {
    QLineEdit* filter = nullptr;
    QTreeWidget* table = nullptr;
    QString filterText; // lower case, trimmed

    void build(ShortcutsDialog& dialog, const ActionRegistry& registry)
    {
        dialog.setWindowTitle(dialog.tr("Keyboard Shortcuts"));
        dialog.resize(560, 640);

        filter = new QLineEdit(&dialog);
        filter->setObjectName(QStringLiteral("shortcuts.filter"));
        filter->setPlaceholderText(dialog.tr("Filter"));
        filter->setAccessibleName(dialog.tr("Filter Shortcuts"));
        filter->setClearButtonEnabled(true);

        table = new QTreeWidget(&dialog);
        table->setObjectName(QStringLiteral("shortcuts.table"));
        table->setAccessibleName(dialog.tr("Keyboard Shortcuts"));
        table->setHeaderLabels({dialog.tr("Action"), dialog.tr("Shortcut")});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->setRootIsDecorated(true);

        populate(registry);
        table->expandAll();
        table->resizeColumnToContents(0);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QObject::connect(filter, &QLineEdit::textChanged, &dialog, [this](const QString& text) {
            applyFilter(text);
        });

        auto* layout = new QVBoxLayout(&dialog);
        layout->addWidget(filter);
        layout->addWidget(table, 1);
        layout->addWidget(buttons);
    }

    // One group per registry category in registration order, then the
    // canvas-only keys (ActionRegistry::canvasKeys).
    void populate(const ActionRegistry& registry)
    {
        for (const QString& categoryTitle : registry.categories()) {
            QTreeWidgetItem* category = addCategory(*table, categoryTitle);
            for (const ActionInfo& info : registry.actions()) {
                if (info.category != categoryTitle || info.action == nullptr)
                    continue;
                addRow(*category, plainText(info.action->text()), shortcutsText(info.action->shortcuts()),
                       shortcutsText(info.action->shortcuts(), QKeySequence::PortableText));
            }
        }
        QTreeWidgetItem* canvas = addCategory(*table, QCoreApplication::translate("qraw::ui::ShortcutsDialog",
                                                                                  "Canvas"));
        for (const CanvasKey& key : ActionRegistry::canvasKeys())
            addRow(*canvas, key.description, key.keys);
    }

    // A row matches when the filter occurs in its action or shortcut text; a
    // category stays visible while one of its rows does.
    void applyFilter(const QString& text)
    {
        filterText = text.trimmed().toLower();
        for (int i = 0; i < table->topLevelItemCount(); ++i) {
            QTreeWidgetItem* category = table->topLevelItem(i);
            bool anyVisible = false;
            for (int row = 0; row < category->childCount(); ++row) {
                const bool match = rowMatches(*category->child(row));
                category->child(row)->setHidden(!match);
                anyVisible = anyVisible || match;
            }
            category->setHidden(!anyVisible);
        }
        table->expandAll();
    }

    bool rowMatches(const QTreeWidgetItem& row) const
    {
        if (filterText.isEmpty())
            return true;
        for (int column = 0; column < kFilterColumnCount; ++column) {
            if (row.text(column).toLower().contains(filterText))
                return true;
        }
        return row.data(1, kSearchTextRole).toString().toLower().contains(filterText);
    }

    int visibleRowCount() const
    {
        int count = 0;
        for (int i = 0; i < table->topLevelItemCount(); ++i) {
            const QTreeWidgetItem* category = table->topLevelItem(i);
            for (int row = 0; row < category->childCount(); ++row) {
                if (!category->child(row)->isHidden())
                    ++count;
            }
        }
        return count;
    }
};

ShortcutsDialog::ShortcutsDialog(const ActionRegistry& registry, QWidget* parent)
    : QDialog(parent), d(std::make_unique<Impl>())
{
    d->build(*this, registry);
}

ShortcutsDialog::~ShortcutsDialog() = default;

void ShortcutsDialog::setFilter(const QString& text)
{
    d->filter->setText(text);
}

int ShortcutsDialog::visibleRowCount() const
{
    return d->visibleRowCount();
}

} // namespace qraw::ui
