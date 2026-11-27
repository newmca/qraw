#include "qraw/ui/recovery_dialog.h"

#include <QAbstractButton>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace qraw::ui {

struct RecoveryDialog::Impl {
    std::vector<RecoveryEntry> entries;
    QListWidget* list = nullptr;
    QPushButton* recover = nullptr;
    QPushButton* discard = nullptr;
    Choice choice = Choice::Later;

    [[nodiscard]] std::vector<RecoveryEntry> selected() const
    {
        std::vector<RecoveryEntry> chosen;
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->checkState() == Qt::Checked)
                chosen.push_back(entries[static_cast<std::size_t>(row)]);
        }
        return chosen;
    }
};

namespace {

QString displayName(const RecoveryEntry& entry)
{
    return entry.originalPath.isEmpty() ? RecoveryDialog::tr("Untitled Diagram")
                                        : QFileInfo(entry.originalPath).fileName();
}

} // namespace

RecoveryDialog::RecoveryDialog(std::vector<RecoveryEntry> entries, const QString& crashReportFolder, QWidget* parent)
    : QDialog(parent), d(std::make_unique<Impl>())
{
    d->entries = std::move(entries);
    setObjectName(QStringLiteral("RecoveryDialog"));
    setWindowTitle(tr("Qraw Closed Unexpectedly"));
    setMinimumWidth(460);

    auto* layout = new QVBoxLayout(this);
    auto* heading = new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Qraw closed unexpectedly.")), this);
    heading->setObjectName(QStringLiteral("recoveryHeading"));
    layout->addWidget(heading);

    const bool hasEntries = !d->entries.empty();
    auto* explanation = new QLabel(
        hasEntries ? tr("These documents had unsaved changes that Qraw saved automatically. Recover opens the "
                        "ticked ones as unsaved documents; Discard deletes their autosaved copies.")
                   : tr("No unsaved changes were found."),
        this);
    explanation->setObjectName(QStringLiteral("recoveryExplanation"));
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    d->list = new QListWidget(this);
    d->list->setObjectName(QStringLiteral("recoveryList"));
    d->list->setAccessibleName(tr("Autosaved documents"));
    for (const RecoveryEntry& entry : d->entries) {
        auto* item = new QListWidgetItem(
            tr("%1 (autosaved %2)").arg(displayName(entry), QLocale().toString(entry.savedAt, QLocale::ShortFormat)),
            d->list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setToolTip(entry.originalPath.isEmpty() ? tr("Never saved") : entry.originalPath);
    }
    d->list->setVisible(hasEntries);
    layout->addWidget(d->list);

    auto* buttons = new QDialogButtonBox(this);
    d->recover = buttons->addButton(tr("&Recover"), QDialogButtonBox::AcceptRole);
    d->recover->setObjectName(QStringLiteral("recoverButton"));
    d->recover->setToolTip(tr("Open the ticked documents as unsaved documents"));
    d->discard = buttons->addButton(tr("&Discard"), QDialogButtonBox::DestructiveRole);
    d->discard->setObjectName(QStringLiteral("discardButton"));
    d->discard->setToolTip(tr("Delete the autosaved copies of the ticked documents"));
    QPushButton* later = buttons->addButton(hasEntries ? tr("&Later") : tr("&Close"), QDialogButtonBox::RejectRole);
    later->setObjectName(QStringLiteral("laterButton"));
    d->recover->setVisible(hasEntries);
    d->discard->setVisible(hasEntries);
    d->recover->setDefault(hasEntries);
    later->setDefault(!hasEntries);

    if (!crashReportFolder.isEmpty()) {
        QPushButton* reports = buttons->addButton(tr("Show Crash &Reports"), QDialogButtonBox::ActionRole);
        reports->setObjectName(QStringLiteral("showReportsButton"));
        reports->setToolTip(tr("Open the folder with the crash reports. They hold version and stack information, "
                               "not the content of your documents."));
        connect(reports, &QPushButton::clicked, this,
                [crashReportFolder] { QDesktopServices::openUrl(QUrl::fromLocalFile(crashReportFolder)); });
    }
    layout->addWidget(buttons);

    auto updateButtons = [this] {
        const bool any = !d->selected().empty();
        d->recover->setEnabled(any);
        d->discard->setEnabled(any);
    };
    connect(d->list, &QListWidget::itemChanged, this, updateButtons);
    updateButtons();

    connect(d->recover, &QPushButton::clicked, this, [this] {
        d->choice = Choice::Recover;
        accept();
    });
    connect(d->discard, &QPushButton::clicked, this, [this] {
        d->choice = Choice::Discard;
        accept();
    });
    connect(later, &QPushButton::clicked, this, &QDialog::reject);
}

RecoveryDialog::~RecoveryDialog() = default;

RecoveryDialog::Choice RecoveryDialog::choice() const
{
    return d->choice;
}

std::vector<RecoveryEntry> RecoveryDialog::selectedEntries() const
{
    return d->selected();
}

} // namespace qraw::ui
