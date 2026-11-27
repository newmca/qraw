#pragma once
// "Qraw closed unexpectedly": shown at start when the last session ended badly (stale session
// marker, new crash report) or autosaved documents are waiting. It lists the autosaved copies, lets
// the user recover or discard the ones that are ticked, and opens the folder with the crash reports.

#include "qraw/ui/file_service.h"

#include <QDialog>

#include <memory>
#include <vector>

namespace qraw::ui {

class RecoveryDialog : public QDialog {
    Q_OBJECT

public:
    // What the user decided. Later (also Escape and the close button) leaves every copy on disk: the
    // question comes back at the next start.
    enum class Choice { Later, Recover, Discard };

    // `crashReportFolder` is shown as "Show Crash Reports" when it is not empty.
    RecoveryDialog(std::vector<RecoveryEntry> entries, const QString& crashReportFolder, QWidget* parent = nullptr);
    ~RecoveryDialog() override;

    [[nodiscard]] Choice choice() const;
    // The ticked entries (all of them at first); what Recover opens and Discard deletes.
    [[nodiscard]] std::vector<RecoveryEntry> selectedEntries() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::ui
