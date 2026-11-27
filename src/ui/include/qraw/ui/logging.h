#pragma once
// Logging categories of qraw::ui: the editor, its canvas, and documents with autosave and recovery
// QT_LOGGING_RULES="qraw.*.debug=true" switches the debug output of every module on (BUILDING.md,
// "Logging"); informational messages and above are on by default.

#include <QLoggingCategory>

namespace qraw::ui {

Q_DECLARE_LOGGING_CATEGORY(lcUi) // qraw.ui
Q_DECLARE_LOGGING_CATEGORY(lcUiCanvas) // qraw.ui.canvas
Q_DECLARE_LOGGING_CATEGORY(lcUiSession) // qraw.ui.session

} // namespace qraw::ui
