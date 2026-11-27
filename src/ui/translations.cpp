// Translator installation (app_setup.h).
#include "qraw/ui/app_setup.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace qraw::ui {

namespace {

// Translators of the previous call, so a second call (tests, a language
// change) replaces them instead of stacking.
struct Installed {
    QTranslator* app = nullptr;
    QTranslator* qt = nullptr;
};

Installed& installed()
{
    static Installed translators;
    return translators;
}

QTranslator* install(QCoreApplication& app, const QString& baseName, const QString& directory)
{
    auto* translator = new QTranslator(&app);
    if (!translator->load(baseName, directory) || !app.installTranslator(translator)) {
        delete translator;
        return nullptr;
    }
    return translator;
}

void removeInstalled(QCoreApplication& app, QTranslator*& translator)
{
    if (translator == nullptr)
        return;
    app.removeTranslator(translator);
    delete translator;
    translator = nullptr;
}

} // namespace

QString installTranslations(QCoreApplication& app, const QString& language)
{
    const QString lang = language.isEmpty() ? QLocale::system().name().left(2) : language;
    Installed& current = installed();
    removeInstalled(app, current.app);
    removeInstalled(app, current.qt);

    current.app = install(app, QStringLiteral("qraw_") + lang, QStringLiteral(":/qraw/i18n"));
    current.qt = install(app, QStringLiteral("qtbase_") + lang, QLibraryInfo::path(QLibraryInfo::TranslationsPath));
    return current.app != nullptr || current.qt != nullptr ? lang : QStringLiteral("en");
}

} // namespace qraw::ui
