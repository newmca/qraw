// "Qraw closed unexpectedly": the dialog, the start-up decision in the main window, recovering into
// more than one window, and Help > Open Log Folder (D-049).
#include "support/shell_env.h"
#include "support/test_support.h"

#include "qraw/diag/data_locations.h"
#include "qraw/diag/session_marker.h"
#include "qraw/ui/app_settings.h"
#include "qraw/ui/main_window.h"
#include "qraw/ui/recovery_dialog.h"

#include <QAbstractButton>
#include <QDesktopServices>
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QtTest>

#include <functional>

using namespace qraw;
using namespace qraw::ui;
namespace id = qraw::ui::action_id;

namespace {

RecoveryEntry entry(const QString& id, const QString& originalPath, int minutesAgo)
{
    return {id, QStringLiteral("/nowhere/%1.drawio").arg(id), originalPath, QDateTime::currentDateTime().addSecs(-60 * minutesAgo)};
}

QPushButton* button(QWidget* parent, const char* name)
{
    return parent->findChild<QPushButton*>(QString::fromLatin1(name));
}

// The watchers of withModalDialog() that never saw a dialog (the test expected none).
QList<QPointer<QTimer>>& pendingWatchers()
{
    static QList<QPointer<QTimer>> watchers;
    return watchers;
}

void dropWatchers()
{
    for (const QPointer<QTimer>& watcher : pendingWatchers())
        delete watcher.data();
    pendingWatchers().clear();
}

// Calls `fn` with the dialog that is open and modal, as soon as there is one.
void withModalDialog(std::function<void(RecoveryDialog&)> fn, int* shown = nullptr)
{
    auto* timer = new QTimer;
    pendingWatchers().push_back(timer);
    timer->setInterval(10);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, fn = std::move(fn), shown] {
        if (auto* dialog = qobject_cast<RecoveryDialog*>(QApplication::activeModalWidget())) {
            timer->stop();
            timer->deleteLater();
            if (shown != nullptr)
                ++*shown;
            fn(*dialog);
        }
    });
    timer->start();
}

// A snapshot of a modified document, left behind as a crashed session leaves it.
void leaveSnapshot(const QString& sessionId, const QString& originalPath)
{
    RecoveryStore store(diag::recoveryDirectory());
    QVERIFY(store.write(sessionId, FileService::blankDocument(), originalPath));
} // the store lets go of the lock: the snapshot is an orphan now

void clearRecoveryFolder()
{
    QDir(diag::recoveryDirectory()).removeRecursively();
}

QList<MainWindow*> mainWindows()
{
    QList<MainWindow*> windows;
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (auto* window = qobject_cast<MainWindow*>(widget))
            windows << window;
    }
    return windows;
}

} // namespace

class TestRecoveryDialog : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { clearRecoveryFolder(); }
    void init() { clearRecoveryFolder(); }
    void cleanup()
    {
        dropWatchers();
        clearRecoveryFolder();
    }

    void listsTheAutosavedDocuments()
    {
        RecoveryDialog dialog({entry("a", "/home/me/network.drawio", 2), entry("b", QString(), 10)}, QString());
        dialog.show();
        auto* list = dialog.findChild<QListWidget*>("recoveryList");
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 2);
        QVERIFY(list->item(0)->text().startsWith(QStringLiteral("network.drawio")));
        QVERIFY(list->item(1)->text().startsWith(QStringLiteral("Untitled Diagram")));
        QCOMPARE(list->item(0)->toolTip(), QStringLiteral("/home/me/network.drawio"));
        QCOMPARE(list->item(1)->toolTip(), QStringLiteral("Never saved"));
        QCOMPARE(list->item(0)->checkState(), Qt::Checked);
        QCOMPARE(dialog.selectedEntries().size(), static_cast<std::size_t>(2));
        QVERIFY(dialog.findChild<QLabel*>("recoveryHeading")->text().contains(QStringLiteral("Qraw closed unexpectedly")));
        QVERIFY(button(&dialog, "showReportsButton") == nullptr); // no crash report, no button
        QCOMPARE(dialog.windowTitle(), QStringLiteral("Qraw Closed Unexpectedly"));
    }

    void onlyTheTickedOnesAreSelected()
    {
        RecoveryDialog dialog({entry("a", "/x/a.drawio", 1), entry("b", "/x/b.drawio", 2)}, QString());
        dialog.show();
        auto* list = dialog.findChild<QListWidget*>("recoveryList");
        list->item(0)->setCheckState(Qt::Unchecked);
        const auto selected = dialog.selectedEntries();
        QCOMPARE(selected.size(), static_cast<std::size_t>(1));
        QCOMPARE(selected[0].id, QStringLiteral("b"));
        QVERIFY(button(&dialog, "recoverButton")->isEnabled());
        list->item(1)->setCheckState(Qt::Unchecked);
        QVERIFY(!button(&dialog, "recoverButton")->isEnabled());
        QVERIFY(!button(&dialog, "discardButton")->isEnabled());
        QVERIFY(button(&dialog, "laterButton")->isEnabled());
    }

    void theButtonsChoose()
    {
        for (const char* name : {"recoverButton", "discardButton", "laterButton"}) {
            RecoveryDialog dialog({entry("a", "/x/a.drawio", 1)}, QString());
            dialog.show();
            QCOMPARE(dialog.choice(), RecoveryDialog::Choice::Later);
            QTest::mouseClick(button(&dialog, name), Qt::LeftButton);
            const auto expected = std::string(name) == "recoverButton" ? RecoveryDialog::Choice::Recover
                : std::string(name) == "discardButton"                 ? RecoveryDialog::Choice::Discard
                                                                       : RecoveryDialog::Choice::Later;
            QCOMPARE(dialog.choice(), expected);
            QCOMPARE(dialog.result(), std::string(name) == "laterButton" ? int(QDialog::Rejected) : int(QDialog::Accepted));
        }
    }

    void withoutDocumentsItOnlyInforms()
    {
        RecoveryDialog dialog({}, QDir::tempPath());
        dialog.show();
        QVERIFY(!dialog.findChild<QListWidget*>("recoveryList")->isVisible());
        QVERIFY(!button(&dialog, "recoverButton")->isVisible());
        QVERIFY(!button(&dialog, "discardButton")->isVisible());
        QVERIFY(button(&dialog, "laterButton")->text().contains(QStringLiteral("Close")));
        QVERIFY(dialog.findChild<QLabel*>("recoveryExplanation")->text().contains(QStringLiteral("No unsaved changes")));
        QVERIFY(button(&dialog, "showReportsButton") != nullptr);
    }

    void showCrashReportsOpensTheFolder()
    {
        lastUrl.clear();
        QDesktopServices::setUrlHandler(QStringLiteral("file"), this, "folderOpened");
        QTemporaryDir folder;
        RecoveryDialog dialog({}, folder.path());
        dialog.show();
        QTest::mouseClick(button(&dialog, "showReportsButton"), Qt::LeftButton);
        QDesktopServices::unsetUrlHandler(QStringLiteral("file"));
        QCOMPARE(lastUrl, QUrl::fromLocalFile(folder.path()));
        QVERIFY(dialog.isVisible()); // the dialog stays: the user still has to decide
    }

    void aCleanStartAsksNothing()
    {
        MainWindow window;
        int shown = 0;
        withModalDialog([](RecoveryDialog& dialog) { dialog.reject(); }, &shown);
        QTimer::singleShot(300, &window, [] { // the welcome dialog is modal too: leave it
            if (QWidget* modal = QApplication::activeModalWidget())
                modal->close();
        });
        window.startup({});
        QCOMPARE(shown, 0);
    }

    void recoversTheCopiesIntoOneWindowEach()
    {
        leaveSnapshot("one", "/home/me/first.drawio");
        leaveSnapshot("two", QString());
        AppSettings().setShowWelcomeOnStart(false);

        MainWindow window;
        window.setPrompts(&prompts);
        diag::PreviousRun crashed;
        crashed.staleSessions = 1;
        window.setPreviousRun(crashed);
        int shown = 0;
        withModalDialog(
            [](RecoveryDialog& dialog) {
                QCOMPARE(dialog.selectedEntries().size(), static_cast<std::size_t>(2));
                QTest::mouseClick(button(&dialog, "recoverButton"), Qt::LeftButton);
            },
            &shown);
        window.startup({});
        QCOMPARE(shown, 1);

        QVERIFY(window.controller().isModified()); // unsaved, modified
        QList<MainWindow*> windows = mainWindows();
        QCOMPARE(windows.size(), 2); // the second document got a window of its own
        MainWindow* other = windows.first() == &window ? windows.last() : windows.first();
        QVERIFY(other->controller().isModified());
        // Newest first: the untitled one was written last.
        const QStringList paths{window.controller().filePath(), other->controller().filePath()};
        QVERIFY2(paths.contains(QStringLiteral("/home/me/first.drawio")) && paths.contains(QString()), qPrintable(paths.join('|')));
        // The orphans were taken over; nothing is offered a second time.
        QVERIFY(RecoveryStore(diag::recoveryDirectory()).orphans().empty());

        other->controller().closeSession();
        delete other;
        window.controller().closeSession();
        AppSettings().setShowWelcomeOnStart(true);
    }

    void discardDeletesOnlyWhatIsTicked()
    {
        leaveSnapshot("one", "/home/me/first.drawio");
        leaveSnapshot("two", "/home/me/second.drawio");
        AppSettings().setShowWelcomeOnStart(false);

        MainWindow window;
        diag::PreviousRun crashed;
        crashed.staleSessions = 1;
        window.setPreviousRun(crashed);
        withModalDialog([](RecoveryDialog& dialog) {
            dialog.findChild<QListWidget*>("recoveryList")->item(0)->setCheckState(Qt::Unchecked);
            QTest::mouseClick(button(&dialog, "discardButton"), Qt::LeftButton);
        });
        window.startup({});

        QVERIFY(!window.controller().isModified()); // nothing was opened
        const auto left = RecoveryStore(diag::recoveryDirectory()).orphans();
        QCOMPARE(left.size(), static_cast<std::size_t>(1)); // the unticked copy stays on disk
        AppSettings().setShowWelcomeOnStart(true);
    }

    void laterKeepsEverythingAndTheQuestionComesBack()
    {
        leaveSnapshot("one", "/home/me/first.drawio");
        AppSettings().setShowWelcomeOnStart(false);
        for (int start = 0; start < 2; ++start) { // no stale marker either time: the copy alone asks
            MainWindow window;
            int shown = 0;
            withModalDialog([](RecoveryDialog& dialog) { QTest::mouseClick(button(&dialog, "laterButton"), Qt::LeftButton); },
                            &shown);
            window.startup({});
            QCOMPARE(shown, 1);
            QVERIFY(!window.controller().isModified());
            QCOMPARE(RecoveryStore(diag::recoveryDirectory()).orphans().size(), static_cast<std::size_t>(1));
        }
        AppSettings().setShowWelcomeOnStart(true);
    }

    void aKilledSessionWithNothingToRecoverIsStillReportedOnce()
    {
        AppSettings().setShowWelcomeOnStart(false);
        MainWindow window;
        diag::PreviousRun killed;
        killed.staleSessions = 1;
        window.setPreviousRun(killed);
        int shown = 0;
        withModalDialog(
            [](RecoveryDialog& dialog) {
                QVERIFY(!dialog.findChild<QListWidget*>("recoveryList")->isVisible());
                dialog.reject();
            },
            &shown);
        window.startup({});
        QCOMPARE(shown, 1);
        // Asked once: the previous run is consumed.
        window.startup({});
        QCOMPARE(shown, 1);
        AppSettings().setShowWelcomeOnStart(true);
    }

    void aShownCrashReportIsNotNewAnyMore()
    {
        QTemporaryDir reports; // stands in for the crash folder through the settings only
        AppSettings settings;
        QVERIFY(!settings.crashReportsSeenUntil().isValid());
        const QDateTime time = QDateTime::fromString(QStringLiteral("2026-11-26T12:34:56.789"), Qt::ISODateWithMs);
        settings.setCrashReportsSeenUntil(time);
        QCOMPARE(settings.crashReportsSeenUntil().toMSecsSinceEpoch(), time.toMSecsSinceEpoch());
    }

    void helpMenuOpensTheLogFolder()
    {
        MainWindow window;
        QDir(diag::logDirectory()).removeRecursively();
        lastUrl.clear();
        QDesktopServices::setUrlHandler(QStringLiteral("file"), this, "folderOpened");
        QAction* action = window.actions()[id::kOpenLogFolder];
        QVERIFY(action != nullptr);
        QCOMPARE(action->text(), QStringLiteral("Open Log Folder")); // no "...": it opens no dialog
        action->trigger();
        QDesktopServices::unsetUrlHandler(QStringLiteral("file"));
        QCOMPARE(lastUrl, QUrl::fromLocalFile(diag::logDirectory()));
        QVERIFY(QDir(diag::logDirectory()).exists()); // made on demand so that the file manager has something to show
        // It is in the Help menu, above About.
        QStringList help;
        for (QAction* top : window.menuBar()->actions()) {
            if (top->menu() != nullptr && top->menu()->objectName() == QLatin1String("menu.help")) {
                for (QAction* item : top->menu()->actions())
                    help << item->objectName();
            }
        }
        QVERIFY2(help.indexOf(QString::fromLatin1(id::kOpenLogFolder)) >= 0, qPrintable(help.join(',')));
        QVERIFY(help.indexOf(QString::fromLatin1(id::kOpenLogFolder)) < help.indexOf(QString::fromLatin1(id::kAbout)));
    }

public slots:
    void folderOpened(const QUrl& url) { lastUrl = url; }

private:
    class SilentPrompts final : public DocumentPrompts {
    public:
        SaveChoice askSaveChanges(const QString&) override { return SaveChoice::Discard; }
        std::optional<QString> askOpenPath(const QString&) override { return std::nullopt; }
        std::optional<QString> askSavePath(const QString&, const QString&) override { return std::nullopt; }
        void showError(const QString&, const QString&) override {}
    };

    SilentPrompts prompts;
    QUrl lastUrl;
};

SHELL_TEST_MAIN(TestRecoveryDialog)
#include "tst_recovery_dialog.moc"
