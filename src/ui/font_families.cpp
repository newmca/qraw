#include "font_families.h"

#include <QComboBox>
#include <QCompleter>
#include <QFont>
#include <QFontDatabase>
#include <QSet>
#include <QSignalBlocker>
#include <QStringListModel>

#include <algorithm>

namespace qraw::ui {

QStringList defaultFontFamilies()
{
    return {QStringLiteral("Helvetica"), QStringLiteral("Verdana"),       QStringLiteral("Times New Roman"),
            QStringLiteral("Garamond"),  QStringLiteral("Comic Sans MS"), QStringLiteral("Courier New"),
            QStringLiteral("Georgia"),   QStringLiteral("Lucida Console"), QStringLiteral("Tahoma")};
}

const QStringList& installedFontFamilies()
{
    static const QStringList families = [] {
        QStringList list = QFontDatabase::families();
        list.erase(std::remove_if(list.begin(), list.end(), [](const QString& f) { return QFontDatabase::isPrivateFamily(f); }),
                   list.end());
        std::sort(list.begin(), list.end(), [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
        list.erase(std::unique(list.begin(), list.end(),
                               [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) == 0; }),
                   list.end());
        return list;
    }();
    return families;
}

namespace {

void addFamilies(QComboBox* combo, const QStringList& families, bool ownFont)
{
    for (const QString& family : families) {
        combo->addItem(family);
        if (ownFont)
            combo->setItemData(combo->count() - 1, QFont(family), Qt::FontRole);
    }
}

} // namespace

void fillFontFamilyCombo(QComboBox* combo, const QStringList& recent)
{
    const QString text = combo->currentText();
    {
        QSignalBlocker blocker(combo);
        combo->clear();
        addFamilies(combo, defaultFontFamilies(), true);
        if (!recent.isEmpty()) {
            combo->insertSeparator(combo->count());
            addFamilies(combo, recent, true);
        }
        combo->insertSeparator(combo->count());
        addFamilies(combo, installedFontFamilies(), false);
        combo->setCurrentIndex(-1);
        combo->setEditText(text);
    }

    // One entry per family for the completer, whichever section has it.
    QStringList all;
    QSet<QString> seen;
    for (const QString& family : defaultFontFamilies() + recent + installedFontFamilies()) {
        if (!seen.contains(family.toLower())) {
            seen.insert(family.toLower());
            all.append(family);
        }
    }
    auto* completer = combo->findChild<QCompleter*>(QStringLiteral("fontFamilyCompleter"));
    if (completer == nullptr) {
        completer = new QCompleter(combo);
        completer->setObjectName(QStringLiteral("fontFamilyCompleter"));
        completer->setFilterMode(Qt::MatchContains);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setCompletionMode(QCompleter::PopupCompletion);
        combo->setCompleter(completer);
    }
    if (auto* model = qobject_cast<QStringListModel*>(completer->model()))
        model->setStringList(all);
    else
        completer->setModel(new QStringListModel(all, completer));
}

} // namespace qraw::ui
