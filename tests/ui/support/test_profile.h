#pragma once
// Throw-away settings and standard paths for UI tests. AppSettings reads and
// writes QSettings with the default format; without isolation a test would use
// the developer's real profile (and on Windows the registry, which has no
// storage at all while the organization name is empty, so nothing persists).

#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

namespace qraw::ui::test {

// Settings go to an INI file in a temporary directory on every platform.
// Safe to call before a QCoreApplication exists; the organization name is
// filled in only when the test has not chosen one.
inline void isolateSettings()
{
    static QTemporaryDir dir;
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    if (QCoreApplication::organizationName().isEmpty() && QCoreApplication::instance())
        QCoreApplication::setOrganizationName(QStringLiteral("QrawTest"));
}

} // namespace qraw::ui::test
