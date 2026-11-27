#include "qraw/diag/data_locations.h"

#include <QStandardPaths>

namespace qraw::diag {

QString dataRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Qraw/Qraw");
}

QString logDirectory()
{
    return dataRoot() + QStringLiteral("/logs");
}

QString logFilePath()
{
    return logDirectory() + QStringLiteral("/qraw.log");
}

QString crashDirectory()
{
    return dataRoot() + QStringLiteral("/crashes");
}

QString sessionDirectory()
{
    return dataRoot() + QStringLiteral("/sessions");
}

QString recoveryDirectory()
{
    return dataRoot() + QStringLiteral("/recovery");
}

} // namespace qraw::diag
