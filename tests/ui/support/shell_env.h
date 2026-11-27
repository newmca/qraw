#pragma once
// Test entry point of the shell tests: settings and recovery snapshots go
// to a throw-away directory instead of the developer's real profile (AppSettings
// writes QSettings; MainWindow autosaves into the app data directory).

#include "test_profile.h"

#include <QApplication>
#include <QByteArray>
#include <QTemporaryDir>
#include <QtTest>

#include <cstdlib>
#include <memory>

namespace qraw::ui::test {

inline QTemporaryDir& shellProfileDir()
{
    static QTemporaryDir dir;
    return dir;
}

// Must run before the first QSettings / QStandardPaths use.
inline void isolateProfile()
{
    isolateSettings(); // QSettings: an INI file under its own temporary directory on every platform
    const QByteArray root = shellProfileDir().path().toLocal8Bit();
    qputenv("XDG_CONFIG_HOME", root + "/config");
    qputenv("XDG_DATA_HOME", root + "/data");
    qputenv("XDG_CACHE_HOME", root + "/cache");
}

} // namespace qraw::ui::test

// QTEST_MAIN with an isolated profile.
#define SHELL_TEST_MAIN(TestClass)                                                                                  \
    int main(int argc, char* argv[])                                                                                \
    {                                                                                                               \
        qraw::ui::test::isolateProfile();                                                                         \
        QApplication app(argc, argv);                                                                               \
        QCoreApplication::setApplicationName(QStringLiteral("QrawShellTest"));                                    \
        QCoreApplication::setOrganizationName(QStringLiteral("QrawShellTest"));                                   \
        TestClass test;                                                                                             \
        return QTest::qExec(&test, argc, argv);                                                                     \
    }
