#pragma once
// Opens the files the operating system asks the running application to open.

#include <QObject>
#include <QStringList>

#include <utility>

namespace qraw::ui {

class MainWindow;

// macOS hands documents to an application through QFileOpenEvent: a double
// click in Finder, "Open With", a file dropped on the Dock icon, in a running
// instance and also at launch (instead of a command-line argument, and
// possibly before the main window exists). Install the handler on the
// QApplication first thing; it queues the paths until a window is set, then
// opens them there (one document per window: the first path opens, the others
// are reported like extra command-line files). On platforms that never send
// the event it does nothing.
class FileOpenHandler : public QObject {
    Q_OBJECT

public:
    explicit FileOpenHandler(QObject* parent = nullptr);

    // The window that opens the files; flushes the queue. nullptr = queue again.
    void setWindow(MainWindow* window);

    // Paths received while no window was set, removed from the queue. main()
    // merges them into the startup files so a launch by double click does not
    // show the welcome dialog first.
    [[nodiscard]] QStringList takePendingFiles() { return std::exchange(pending_, {}); }

    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void open(const QStringList& files);

    MainWindow* window_ = nullptr;
    QStringList pending_;
};

} // namespace qraw::ui
