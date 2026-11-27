#pragma once
// The model side of the cell data actions (draw.io's editData, editLink,
// editTooltip, editImage; js/grapheditor/Actions.js:755-905, Dialogs.js
// EditDataDialog): what the dialogs show for the selection, and the edits their
// OK buttons make. Free functions on a DocumentSession like edit_actions.h: each
// edit is one DocumentSession::edit (one undo step) through commands::ops, and
// does nothing (returns false) when its preconditions fail.
//
// Custom data lives on the cell's wrapper element (core::CellWrapper). The first
// property wraps a plain cell as draw.io does (the label moves to `label`, the id
// to the wrapper); removing the last one keeps the wrapper.

#include <QString>
#include <QUrl>
#include <Qt>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace qraw::core {
class Cell;
class Page;
} // namespace qraw::core

namespace qraw::ui {

class DocumentSession;

namespace actions {

// --- Edit Data (Ctrl+M) ---------------------------------------------------------
// One property row of the dialog.
struct DataRow {
    std::string name;
    std::string value;
    // Several cells are edited and they differ: `value` is empty and an empty
    // field leaves each cell's own value alone.
    bool mixed = false;
};

// What Edit Data shows. The cells are the selection, or the page's root cell when
// nothing is selected (Actions.js 'editData' edits graph.getModel().getRoot(), the
// diagram's own data).
struct DataSheet {
    std::vector<std::string> cells;
    bool isDiagram = false;
    // The id of the only cell edited; "" for several cells and for the diagram.
    std::string id;
    // The properties: for several cells the union of their names, each with the
    // common value or `mixed`; sorted by name. `label`, `placeholders` and `tags`
    // are not rows (labels are edited in place, the other two have their own
    // controls), nor are draw.io's internal dataSource* attributes.
    std::vector<DataRow> rows;
    std::string tags; // space separated; blank where the cells differ
    bool tagsMixed = false;
    // Vertices and edges can use placeholders; layers and the diagram cannot.
    bool placeholdersAvailable = false;
    Qt::CheckState placeholders = Qt::Unchecked; // PartiallyChecked where the cells differ
};

// The ids Edit Data edits.
[[nodiscard]] std::vector<std::string> dataTargets(const DocumentSession& session);
// Always available while the page exists (EditorUi.js:39279: graph.isEnabled()).
[[nodiscard]] bool canEditData(const DocumentSession& session);
[[nodiscard]] DataSheet readDataSheet(const DocumentSession& session);

// What the user changed in the dialog.
struct DataChange {
    std::vector<std::string> removed; // properties deleted
    std::vector<DataRow> written;     // properties set (an empty value is a value), after `removed`
    std::optional<bool> placeholders; // set when the check box was toggled
    std::optional<std::string> tags;  // set when the field changed; "" removes the attribute
};
// Applies `change` to the sheet's cells in one edit. A plain cell that gains its
// first property becomes an <object>. The dialog checks the names of new
// properties with commands::ops::isValidDataName; the operation refuses anything
// that is not an XML name.
bool editData(DocumentSession& session, const DataSheet& sheet, const DataChange& change);

// `tags` as stored: tokens separated by single spaces.
[[nodiscard]] std::string normalizeTags(const std::string& text);

// --- Edit Link (Alt+Shift+L) ------------------------------------------------------
// What Edit Link shows: the cells it writes to are the unlocked selection
// (Actions.js:828 getEditableCells). Like upstream it prefills only what they all
// agree on.
struct LinkSheet {
    std::vector<std::string> cells;
    std::string link;
    bool linkMixed = false;
    bool newWindow = false; // linkTarget="_blank"
    bool newWindowMixed = false;
};
[[nodiscard]] bool canEditLink(const DocumentSession& session);
[[nodiscard]] LinkSheet readLinkSheet(const DocumentSession& session);

struct LinkEdit {
    bool setLink = false; // false: leave each cell's link (a mixed value the user did not touch)
    std::string link;     // "" removes the attribute
    bool setNewWindow = false;
    bool newWindow = false; // linkTarget="_blank" when set, removed otherwise
};
// Writes the link (and linkTarget) to the sheet's cells in one edit. A cell
// without a link that is given none stays as it is (no wrapper is created for
// nothing).
bool editLink(DocumentSession& session, const LinkSheet& sheet, const LinkEdit& edit);

// The `link` of a link to a page of this document: `data:page/id,<page id>`.
[[nodiscard]] std::string pageLink(const std::string& pageId);
// The page id of such a link; nullopt for any other text.
[[nodiscard]] std::optional<std::string> pageIdOfLink(const std::string& link);

// --- following links (Open Link) ---------------------------------------------------
struct LinkTarget {
    enum class Kind {
        None,     // empty
        Page,     // a page of this document
        External, // http, https or mailto: opened in the desktop's handler
        Blocked,  // anything else: scripts, files, relative paths, pages that do not exist
    };
    Kind kind = Kind::None;
    std::size_t page = 0; // Kind::Page
    QUrl url;             // Kind::External
};
[[nodiscard]] LinkTarget resolveLink(const DocumentSession& session, const std::string& link);
// Switches to the page or hands the URL to `open` (QDesktopServices::openUrl by
// default); a blocked link only reports it on the session's status line. Nothing
// is ever fetched. True when the link was followed.
bool followLink(DocumentSession& session, const std::string& link,
                const std::function<bool(const QUrl&)>& open = {});
// The link of the only selected cell (Open Link, enabled when there is one).
[[nodiscard]] std::optional<std::string> selectedLink(const DocumentSession& session);
// A link as a person reads it: the page name for a page link, else the text.
[[nodiscard]] QString linkDisplayText(const DocumentSession& session, const std::string& link);

// --- Edit Tooltip (Alt+Shift+T) -----------------------------------------------------
// Enabled for exactly one unlocked cell (EditorUi.js:8894).
[[nodiscard]] bool canEditTooltip(const DocumentSession& session);
// The `tooltip` of the selected cell ("" when it has none) and whether the cell's
// label is HTML (the tooltip is then HTML too).
struct TooltipSheet {
    std::string id;
    std::string text;
    bool html = false;
};
[[nodiscard]] std::optional<TooltipSheet> readTooltipSheet(const DocumentSession& session);
// Stores the tooltip ("" removes it) in one edit.
bool editTooltip(DocumentSession& session, const std::string& cellId, const std::string& text);

// --- Edit Image ------------------------------------------------------------------
// Enabled when an unlocked selected vertex has an image: an `image` style, or the
// image shape (EditorUi.js:39312; Insert > Image puts a first image on any cell).
[[nodiscard]] bool canEditImage(const DocumentSession& session);
struct ImageSheet {
    std::vector<std::string> cells;
    std::string image; // the first cell's `image` style value
    bool mixed = false;
};
[[nodiscard]] ImageSheet readImageSheet(const DocumentSession& session);

struct ImageEdit {
    std::optional<std::string> image; // the new value, "" removes it; nullopt leaves the images alone
    // Set the cells to this size (the image's own size): "Reset to Original Size".
    std::optional<std::pair<double, double>> size;
};
// Actions.js:2120 'image': sets `image` on every selected vertex, shape=image
// where the shape is neither image nor label (and removes shape=image when the
// image is cleared), optionally resizes. One edit.
bool editImage(DocumentSession& session, const ImageEdit& edit);

} // namespace actions
} // namespace qraw::ui
