#pragma once
// The write side of the Format panel. Every control calls one of these
// helpers, which runs one DocumentSession::edit (one undo entry) built from
// commands::ops.

#include "qraw/commands/ops_pages.h"
#include "qraw/commands/ops_properties.h"
#include "qraw/commands/result.h"
#include "qraw/ui/style_summary.h"

#include <QString>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::commands {
class Transaction;
}

namespace qraw::ui {

class DocumentSession;

enum class CellFilter { All, Vertices, Edges };

// Ids of the current selection in selection order, filtered by kind, on the
// current page.
[[nodiscard]] std::vector<std::string> selectedCells(const DocumentSession& session,
                                                     CellFilter filter = CellFilter::All);

// The summary of the current selection (empty when nothing is selected or
// there is no page).
[[nodiscard]] StyleSummary summarizeSelection(const DocumentSession& session);

// The stylesheet the session resolves styles with.
[[nodiscard]] const render::Stylesheet& stylesheetOf(const DocumentSession& session);

// Merge key shared by the spin boxes of one control: "format:<key>:<ids>".
[[nodiscard]] std::string formatMergeKey(std::string_view key, const std::vector<std::string>& ids);

// One undo step running `edit` on the current page index.
using PageEdit = std::function<commands::Result(commands::Transaction& tx, std::size_t page)>;
commands::Result editCurrentPage(DocumentSession& session, const QString& name, const PageEdit& edit,
                                 const std::string& mergeKey = {});

// One undo step setting style keys (ops::setStyles) on `ids`.
commands::Result applyStyles(DocumentSession& session, const QString& name, const std::vector<std::string>& ids,
                             std::vector<commands::ops::StyleEdit> edits, const std::string& mergeKey = {});
// The one-key form: value nullopt removes the key.
commands::Result applyStyle(DocumentSession& session, const QString& name, const std::vector<std::string>& ids,
                            std::string_view key, std::optional<std::string> value, const std::string& mergeKey = {});

// One undo step setting page properties (ops::setPageProperties).
commands::Result applyPageProperties(DocumentSession& session, const QString& name,
                                     const commands::ops::PageProperties& properties,
                                     const std::string& mergeKey = {});

// Style value helpers: "1" / "0" for flags, draw.io's numbers otherwise.
[[nodiscard]] std::string flagText(bool on);

} // namespace qraw::ui
