// QRAW_VERSION and QRAW_GIT_COMMIT are defined for this file only (CMakeLists.txt).
#include "qraw/diag/build_info.h"

#include <QSysInfo>
#include <QtGlobal>

namespace qraw::diag {

QString versionString()
{
    return QStringLiteral(QRAW_VERSION);
}

QString gitCommit()
{
    return QStringLiteral(QRAW_GIT_COMMIT);
}

QString qtVersionString()
{
    const QString runtime = QString::fromLatin1(qVersion());
    const QString built = QStringLiteral(QT_VERSION_STR);
    return runtime == built ? runtime : QStringLiteral("%1 (built with %2)").arg(runtime, built);
}

QString systemDescription()
{
    return QStringLiteral("%1, kernel %2, %3")
        .arg(QSysInfo::prettyProductName(), QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture());
}

} // namespace qraw::diag
