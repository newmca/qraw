#pragma once
// Where the application keeps what it writes about itself. All of it lives under one root so that
// the GUI, the command-line tools and the Flatpak sandbox (which moves XDG_DATA_HOME) agree.

#include <QString>

namespace qraw::diag {

// <GenericDataLocation>/Qraw/Qraw: the same folder as QStandardPaths::AppLocalDataLocation of the
// GUI (organisation and application are both "Qraw"), but also right for qraw-cli, whose
// application name differs. Linux: ~/.local/share/Qraw/Qraw, macOS: ~/Library/Application
// Support/Qraw/Qraw, Windows: %LOCALAPPDATA%/Qraw/Qraw.
[[nodiscard]] QString dataRoot();

[[nodiscard]] QString logDirectory();      // <root>/logs
[[nodiscard]] QString logFilePath();       // <root>/logs/qraw.log (the previous run: qraw.log.1)
[[nodiscard]] QString crashDirectory();    // <root>/crashes
[[nodiscard]] QString sessionDirectory();  // <root>/sessions
[[nodiscard]] QString recoveryDirectory(); // <root>/recovery (autosave snapshots, ui::RecoveryStore)

} // namespace qraw::diag
