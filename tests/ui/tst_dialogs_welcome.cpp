// Template catalog and the Welcome dialog (recent files, template picking).

#include "support/test_support.h"

#include "qraw/ui/template_catalog.h"
#include "qraw/ui/welcome_dialog.h"

#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

namespace {

QListWidgetItem* findItem(QListWidget* list, const QString& text)
{
    const auto items = list->findItems(text, Qt::MatchExactly);
    return items.isEmpty() ? nullptr : items.first();
}

} // namespace

class TstDialogsWelcome : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qraw::ui::test::isolateSettings(); }

    void catalogScansCompiledInTemplates()
    {
        const auto templates = TemplateCatalog::scan();
        QVERIFY(templates.size() > 100);
        const auto categories = TemplateCatalog::categories(templates);
        for (const char* expected : {"basic", "flowcharts", "cloud/aws"})
            QVERIFY(std::find(categories.begin(), categories.end(), QString::fromLatin1(expected)) != categories.end());

        const auto flowchart = std::find_if(templates.begin(), templates.end(), [](const TemplateInfo& info) {
            return info.path.endsWith(QStringLiteral("/flowcharts/flowchart_1.xml"));
        });
        QVERIFY(flowchart != templates.end());
        QCOMPARE(flowchart->title, QStringLiteral("Flowchart 1"));
        QCOMPARE(flowchart->category, QStringLiteral("flowcharts"));
    }

    void recentFileCanBeCreated()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString recentPath = dir.filePath(QStringLiteral("recent.drawio"));
        QFile recent(recentPath);
        QVERIFY(recent.open(QIODevice::WriteOnly));
        recent.write("<mxfile/>");
        recent.close();

        AppSettings settings;
        settings.clearRecentFiles();
        settings.addRecentFile(recentPath);

        WelcomeDialog dialog(settings);
        auto* list = dialog.findChild<QListWidget*>(QStringLiteral("welcome.recent"));
        QVERIFY(list != nullptr);
        QListWidgetItem* item = findItem(list, QStringLiteral("recent.drawio"));
        QVERIFY(item != nullptr);
        QCOMPARE(item->toolTip(), recentPath);
        list->setCurrentItem(item);
        item->setSelected(true);
        dialog.findChild<QPushButton*>(QStringLiteral("welcome.create"))->click();
        QCOMPARE(dialog.choice(), WelcomeDialog::Choice::OpenRecent);
        QCOMPARE(dialog.path(), recentPath);
        grabToPng(dialog, QStringLiteral("dialogs-welcome-recent"));
        settings.clearRecentFiles();
    }

    void templateCanBeCreated()
    {
        AppSettings settings;
        WelcomeDialog dialog(settings);
        auto* categories = dialog.findChild<QListWidget*>(QStringLiteral("welcome.categories"));
        auto* grid = dialog.findChild<QListWidget*>(QStringLiteral("welcome.templates"));
        QVERIFY(categories != nullptr && grid != nullptr);
        categories->setCurrentItem(findItem(categories, QStringLiteral("flowcharts")));
        QVERIFY(grid->count() > 0);
        QListWidgetItem* item = findItem(grid, QStringLiteral("Flowchart 1"));
        QVERIFY(item != nullptr);
        QVERIFY(!item->icon().isNull());
        // The thumbnail has the device pixels of its logical box (it fills the box in one direction): sharp on a
        // scaled screen, not a 1x picture stretched.
        const qreal dpr = dialog.devicePixelRatioF();
        const QSize logical = grid->iconSize();
        const QSize pixels = item->icon().availableSizes().first();
        QVERIFY2(qAbs(pixels.width() - logical.width() * dpr) <= 2 || qAbs(pixels.height() - logical.height() * dpr) <= 2,
                 qPrintable(QStringLiteral("%1x%2 px at ratio %3").arg(pixels.width()).arg(pixels.height()).arg(dpr)));
        QCOMPARE(item->icon().pixmap(logical).devicePixelRatio(), dpr);
        grid->setCurrentItem(item);
        item->setSelected(true);
        dialog.findChild<QPushButton*>(QStringLiteral("welcome.create"))->click();
        QCOMPARE(dialog.choice(), WelcomeDialog::Choice::Template);
        QVERIFY(dialog.path().endsWith(QStringLiteral("/flowcharts/flowchart_1.xml")));
    }

    void blankAndCancelChoices()
    {
        AppSettings settings;
        WelcomeDialog blank(settings);
        blank.findChild<QPushButton*>(QStringLiteral("welcome.blank"))->click();
        QCOMPARE(blank.choice(), WelcomeDialog::Choice::Blank);

        WelcomeDialog cancelled(settings);
        cancelled.findChild<QPushButton*>(QStringLiteral("welcome.cancel"))->click();
        QCOMPARE(cancelled.choice(), WelcomeDialog::Choice::None);
    }
};

QTEST_MAIN(TstDialogsWelcome)
#include "tst_dialogs_welcome.moc"
