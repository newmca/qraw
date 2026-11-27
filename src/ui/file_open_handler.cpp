#include "qraw/ui/file_open_handler.h"

#include "qraw/ui/main_window.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFileOpenEvent>

#include <utility>

namespace qraw::ui {

FileOpenHandler::FileOpenHandler(QObject* parent) : QObject(parent) {}

void FileOpenHandler::setWindow(MainWindow* window)
{
    window_ = window;
    if (window_ && !pending_.isEmpty())
        open(std::exchange(pending_, {}));
}

bool FileOpenHandler::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::FileOpen || watched != QCoreApplication::instance())
        return QObject::eventFilter(watched, event);
    // file() is empty for URLs that are not local files (e.g. a custom scheme): not ours.
    const QString path = static_cast<QFileOpenEvent*>(event)->file();
    if (path.isEmpty())
        return false;
    if (window_)
        open({path});
    else
        pending_ << path;
    return true;
}

void FileOpenHandler::open(const QStringList& files)
{
    window_->openFiles(files);
    window_->setWindowState(window_->windowState() & ~Qt::WindowMinimized);
    window_->raise();
    window_->activateWindow();
}

} // namespace qraw::ui
