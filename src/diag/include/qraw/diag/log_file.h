#pragma once
// The GUI's log file: timestamped lines with the logging category, written from any thread, bounded
// in size, and the previous run's file kept next to it. A Qt message handler feeds it and chains to
// the handler that was installed before it, so the messages still reach stderr (or the journal).

#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QString>
#include <QtLogging>

namespace qraw::diag {

class LogFile {
public:
    // The file is capped while running: when a write would take it past `maxBytes`, it is emptied and
    // continues with a note (so the log holds at most `maxBytes` of this run plus the whole file of
    // the previous run).
    static constexpr qint64 kDefaultMaxBytes = 2 * 1024 * 1024;

    explicit LogFile(QString path, qint64 maxBytes = kDefaultMaxBytes);
    ~LogFile();

    LogFile(const LogFile&) = delete;
    LogFile& operator=(const LogFile&) = delete;

    // Moves an existing file to "<path>.1" (replacing the older one) and starts a new one. False when
    // the folder or the file cannot be created: the application then runs without a log file.
    bool open();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] const QString& path() const { return path_; }
    [[nodiscard]] static QString previousPath(const QString& path) { return path + QStringLiteral(".1"); }

    // Appends one line (a newline is added). Thread-safe; flushed at once, so the end of the log is
    // there after a crash.
    void write(const QString& line);

    // "2026-11-26T12:34:56.789Z W qraw.ui.session: message": UTC time, one letter for the severity
    // (D debug, I info, W warning, C critical, F fatal), the category, the message.
    [[nodiscard]] static QString formatLine(const QDateTime& when, QtMsgType type, const char* category,
                                            const QString& message);

private:
    QString path_;
    qint64 maxBytes_;
    mutable QMutex mutex_;
    QFile file_;
};

// Routes every Qt message into `file` and on to the previously installed handler (Qt's default one
// prints to stderr, honouring QT_MESSAGE_PATTERN). `file` must outlive the call to
// uninstallLogHandler(). One file at a time; a second call replaces the first.
void installLogHandler(LogFile& file);
// Restores the previous handler. Call it before the file is destroyed: afterwards nothing is
// written to the file any more (a message that is being written by another thread at that moment
// may still reach it).
void uninstallLogHandler();

} // namespace qraw::diag
