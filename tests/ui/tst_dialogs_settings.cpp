// Preferences dialog (settings round trip) and the Keyboard Shortcuts dialog.

#include "support/test_support.h"

#include "qraw/ui/preferences_dialog.h"
#include "qraw/ui/shortcuts_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QKeySequence>
#include <QSignalSpy>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

class TstDialogsSettings : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qraw::ui::test::isolateSettings(); }

    void preferencesApplyPersistsValues()
    {
        AppSettings settings;
        settings.setTheme(ThemeKind::System);
        settings.setGuidesEnabled(true);
        settings.setAutosaveIntervalSeconds(30);

        PreferencesDialog dialog(settings);
        QSignalSpy applied(&dialog, &PreferencesDialog::settingsApplied);
        dialog.findChild<QComboBox*>(QStringLiteral("prefs.theme"))->setCurrentIndex(
            dialog.findChild<QComboBox*>(QStringLiteral("prefs.theme"))->findData(static_cast<int>(ThemeKind::Dark)));
        auto* appearance = dialog.findChild<QComboBox*>(QStringLiteral("prefs.diagramAppearance"));
        QCOMPARE(appearance->currentData().toInt(), static_cast<int>(DiagramAppearance::FollowTheme));
        appearance->setCurrentIndex(appearance->findData(static_cast<int>(DiagramAppearance::Light)));
        dialog.findChild<QDoubleSpinBox*>(QStringLiteral("prefs.gridSize"))->setValue(20);
        dialog.findChild<QCheckBox*>(QStringLiteral("prefs.guides"))->setChecked(false);
        auto* autosave = dialog.findChild<QComboBox*>(QStringLiteral("prefs.autosave"));
        autosave->setCurrentIndex(autosave->findData(300));
        grabToPng(dialog, QStringLiteral("dialogs-preferences"));

        dialog.apply();
        QCOMPARE(applied.count(), 1);

        const AppSettings reloaded;
        QCOMPARE(reloaded.theme(), ThemeKind::Dark);
        QCOMPARE(reloaded.diagramAppearance(), DiagramAppearance::Light); // a light canvas under a dark UI
        QCOMPARE(reloaded.defaultGridSize(), 20.0);
        QVERIFY(!reloaded.guidesEnabled());
        QCOMPARE(reloaded.autosaveIntervalSeconds(), 300);

        settings.setTheme(ThemeKind::System);
        settings.setDiagramAppearance(DiagramAppearance::FollowTheme);
        settings.setGuidesEnabled(true);
        settings.setDefaultGridSize(10);
        settings.setAutosaveIntervalSeconds(30);
    }

    void shortcutsFilterByActionAndShortcut()
    {
        ActionRegistry registry;
        registry.add(action_id::kSave, QStringLiteral("File"), QStringLiteral("&Save"),
                     {QKeySequence(QKeySequence::Save)});
        registry.add(action_id::kUndo, QStringLiteral("Edit"), QStringLiteral("&Undo"),
                     {QKeySequence(QKeySequence::Undo)});

        ShortcutsDialog dialog(registry);
        grabToPng(dialog, QStringLiteral("dialogs-shortcuts"));
        const int all = dialog.visibleRowCount();
        QVERIFY(all >= 2 + int(ActionRegistry::canvasKeys().size()));

        dialog.setFilter(QStringLiteral("save"));
        QCOMPARE(dialog.visibleRowCount(), 1);

        dialog.setFilter(QStringLiteral("CTRL+Z"));
        QCOMPARE(dialog.visibleRowCount(), 1);

        dialog.setFilter(QStringLiteral("no such shortcut"));
        QCOMPARE(dialog.visibleRowCount(), 0);

        dialog.setFilter({});
        QCOMPARE(dialog.visibleRowCount(), all);
    }
};

QTEST_MAIN(TstDialogsSettings)
#include "tst_dialogs_settings.moc"
