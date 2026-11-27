#pragma once

// Thread-local caches that hold Qt objects (laid-out QTextDocuments, QSvgRenderers)
// must be emptied before the application object goes: a font engine or a
// renderer destroyed by the main thread's exit-time thread_local destructors,
// after QGuiApplication, is a use after free of Qt's own state.

#include <QCoreApplication>
#include <QThread>

namespace qraw::render {

// Call once per thread when its cache is created. When that thread is the
// application's main thread, `cleanup` (which must empty that thread's cache)
// runs when the event loop quits (the application object is still whole then)
// and again from QCoreApplication's destructor, for programs that never ran one.
inline void clearOnApplicationExit(void (*cleanup)())
{
    QCoreApplication* app = QCoreApplication::instance();
    if (app == nullptr || QThread::currentThread() != app->thread())
        return;
    QObject::connect(app, &QCoreApplication::aboutToQuit, app, [cleanup] { cleanup(); });
    qAddPostRoutine(cleanup);
}

} // namespace qraw::render
