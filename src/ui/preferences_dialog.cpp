#include "qraw/ui/preferences_dialog.h"

#include "qraw/ui/app_settings.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

#include <algorithm>

namespace qraw::ui {
namespace {

// Compiled-in translations: :/qraw/i18n/qraw_<lang>.qm (CMake resource).
// Language codes of those files, sorted.
QStringList availableLanguages()
{
    const QString prefix = QStringLiteral("qraw_");
    const QString suffix = QStringLiteral(".qm");
    QStringList languages;
    const QStringList files = QDir(QStringLiteral(":/qraw/i18n")).entryList({prefix + QLatin1Char('*') + suffix},
                                                                           QDir::Files);
    for (const QString& file : files)
        languages.push_back(file.mid(prefix.size()).chopped(suffix.size()));
    std::sort(languages.begin(), languages.end());
    return languages;
}

QString nativeNameOf(const QString& language)
{
    const QString name = QLocale(language).nativeLanguageName();
    return name.isEmpty() ? language : name;
}

// Autosave intervals offered in the combo box, in seconds (0 = off).
const std::vector<int> kAutosaveChoices{0, 10, 30, 60, 300};

QString autosaveText(int seconds)
{
    if (seconds == 0)
        return QCoreApplication::translate("qraw::ui::PreferencesDialog", "Off");
    if (seconds < 60)
        return QCoreApplication::translate("qraw::ui::PreferencesDialog", "%1 s").arg(seconds);
    if (seconds == 60)
        return QCoreApplication::translate("qraw::ui::PreferencesDialog", "1 min");
    return QCoreApplication::translate("qraw::ui::PreferencesDialog", "%1 min").arg(seconds / 60);
}

} // namespace

struct PreferencesDialog::Impl {
    Impl(PreferencesDialog& owner, AppSettings& appSettings) : dialog(owner), settings(appSettings) {}

    PreferencesDialog& dialog;
    AppSettings& settings;
    QComboBox* theme = nullptr;
    QComboBox* language = nullptr;
    QComboBox* autosave = nullptr;
    QDoubleSpinBox* gridSize = nullptr;
    QCheckBox* guides = nullptr;
    QCheckBox* welcome = nullptr;

    void build()
    {
        dialog.setWindowTitle(dialog.tr("Preferences"));
        auto* form = new QFormLayout;

        theme = new QComboBox(&dialog);
        theme->setObjectName(QStringLiteral("prefs.theme"));
        theme->addItem(dialog.tr("System"), static_cast<int>(ThemeKind::System));
        theme->addItem(dialog.tr("Light"), static_cast<int>(ThemeKind::Light));
        theme->addItem(dialog.tr("Dark"), static_cast<int>(ThemeKind::Dark));
        theme->addItem(dialog.tr("High Contrast"), static_cast<int>(ThemeKind::HighContrast));
        theme->setCurrentIndex(theme->findData(static_cast<int>(settings.theme())));
        form->addRow(dialog.tr("Theme:"), theme);

        language = new QComboBox(&dialog);
        language->setObjectName(QStringLiteral("prefs.language"));
        language->addItem(dialog.tr("System"), QString());
        for (const QString& code : availableLanguages())
            language->addItem(nativeNameOf(code), code);
        language->setCurrentIndex(std::max(0, language->findData(settings.language())));
        auto* restartNote = new QLabel(dialog.tr("A restart is required for a language change to take effect."),
                                       &dialog);
        restartNote->setWordWrap(true);
        form->addRow(dialog.tr("Language:"), language);
        form->addRow(QString(), restartNote);

        autosave = new QComboBox(&dialog);
        autosave->setObjectName(QStringLiteral("prefs.autosave"));
        for (const int seconds : kAutosaveChoices)
            autosave->addItem(autosaveText(seconds), seconds);
        const int current = settings.autosaveIntervalSeconds();
        if (autosave->findData(current) < 0)
            autosave->addItem(autosaveText(current), current);
        autosave->setCurrentIndex(autosave->findData(current));
        form->addRow(dialog.tr("Autosave:"), autosave);

        gridSize = new QDoubleSpinBox(&dialog);
        gridSize->setObjectName(QStringLiteral("prefs.gridSize"));
        gridSize->setRange(1, 100);
        gridSize->setDecimals(1);
        gridSize->setValue(settings.defaultGridSize());
        form->addRow(dialog.tr("Default grid size:"), gridSize);

        guides = new QCheckBox(dialog.tr("Show guides"), &dialog);
        guides->setObjectName(QStringLiteral("prefs.guides"));
        guides->setChecked(settings.guidesEnabled());
        form->addRow(QString(), guides);

        welcome = new QCheckBox(dialog.tr("Show the welcome dialog at startup"), &dialog);
        welcome->setObjectName(QStringLiteral("prefs.welcome"));
        welcome->setChecked(settings.showWelcomeOnStart());
        form->addRow(QString(), welcome);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [this] {
            dialog.apply();
            dialog.accept();
        });
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        auto* root = new QVBoxLayout(&dialog);
        root->addLayout(form);
        root->addWidget(buttons);
    }

    void apply()
    {
        settings.setTheme(static_cast<ThemeKind>(theme->currentData().toInt()));
        settings.setLanguage(language->currentData().toString());
        settings.setAutosaveIntervalSeconds(autosave->currentData().toInt());
        settings.setDefaultGridSize(gridSize->value());
        settings.setGuidesEnabled(guides->isChecked());
        settings.setShowWelcomeOnStart(welcome->isChecked());
    }
};

PreferencesDialog::PreferencesDialog(AppSettings& settings, QWidget* parent)
    : QDialog(parent), d(std::make_unique<Impl>(*this, settings))
{
    d->build();
}

PreferencesDialog::~PreferencesDialog() = default;

void PreferencesDialog::apply()
{
    d->apply();
    emit settingsApplied();
}

} // namespace qraw::ui
