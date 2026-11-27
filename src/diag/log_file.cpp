#include "qraw/diag/log_file.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QTextStream>

#include <atomic>
#include <cstdio>

namespace qraw::diag {

LogFile::LogFile(QString path, qint64 maxBytes) : path_(std::move(path)), maxBytes_(maxBytes), file_(path_) {}

LogFile::~LogFile() = default;

bool LogFile::open()
{
    const QMutexLocker lock(&mutex_);
    if (file_.isOpen())
        return true;
    if (!QDir().mkpath(QFileInfo(path_).absolutePath()))
        return false;
    if (QFileInfo::exists(path_)) {
        QFile::remove(previousPath(path_));
        QFile::rename(path_, previousPath(path_));
    }
    // Unbuffered: every line reaches the operating system when it is written.
    return file_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered);
}

bool LogFile::isOpen() const
{
    const QMutexLocker lock(&mutex_);
    return file_.isOpen();
}

void LogFile::write(const QString& line)
{
    const QByteArray bytes = line.toUtf8() + '\n';
    const QMutexLocker lock(&mutex_);
    if (!file_.isOpen())
        return;
    if (file_.size() + bytes.size() > maxBytes_ && file_.size() > 0) {
        file_.resize(0);
        file_.write(formatLine(QDateTime::currentDateTimeUtc(), QtInfoMsg, "qraw.app.log",
                               QStringLiteral("log file reached %1 bytes: earlier lines of this run were dropped")
                                   .arg(maxBytes_)).toUtf8() + '\n');
    }
    file_.write(bytes);
}

QString LogFile::formatLine(const QDateTime& when, QtMsgType type, const char* category, const QString& message)
{
    QChar severity = u'I';
    switch (type) {
    case QtDebugMsg: severity = u'D'; break;
    case QtInfoMsg: severity = u'I'; break;
    case QtWarningMsg: severity = u'W'; break;
    case QtCriticalMsg: severity = u'C'; break;
    case QtFatalMsg: severity = u'F'; break;
    }
    // Messages without a category (plain qDebug) arrive as "default".
    const QString name = category != nullptr ? QString::fromLatin1(category) : QStringLiteral("default");
    return QStringLiteral("%1 %2 %3: %4")
        .arg(when.toUTC().toString(Qt::ISODateWithMs), QString(severity), name, message);
}

namespace {

std::atomic<LogFile*> activeFile{nullptr};
QtMessageHandler previousHandler = nullptr;

void handleMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (LogFile* file = activeFile.load(std::memory_order_acquire))
        file->write(LogFile::formatLine(QDateTime::currentDateTimeUtc(), type, context.category, message));
    if (previousHandler != nullptr) {
        previousHandler(type, context, message);
        return;
    }
    // Qt's own handler is not reachable once replaced: print what it would.
    const QByteArray text = qFormatLogMessage(type, context, message).toLocal8Bit();
    std::fprintf(stderr, "%s\n", text.constData());
    std::fflush(stderr);
}

} // namespace

void installLogHandler(LogFile& file)
{
    const bool first = activeFile.load() == nullptr;
    activeFile.store(&file, std::memory_order_release);
    if (first)
        previousHandler = qInstallMessageHandler(handleMessage);
}

void uninstallLogHandler()
{
    if (activeFile.exchange(nullptr) == nullptr)
        return;
    qInstallMessageHandler(previousHandler);
    previousHandler = nullptr;
}

} // namespace qraw::diag
