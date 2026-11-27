#pragma once

// Insert > Advanced: diagrams made from text (draw.io's From Text, CSV and
// Mermaid dialogs). Each importer is a pure function from text to detached
// cells, with the layout already applied and the top-left corner of the result
// at (0, 0): the UI only collects the text, calls the importer and inserts the
// cells as one edit (ui::actions::insertImported). Nothing here touches a
// document, so the importers are unit-tested without one and always give the
// same cells for the same text (DECISIONS.md D-040).
//
// Sources (upstream paths are relative to
// $DRAWIO_SRC/src/main/webapp/js/):
//  * importCsv is a port of EditorUi.prototype.doImportCsv
//    (diagramly/EditorUi.js:37190) with Editor.csvToArray (diagramly/Editor.js:5597)
//    and Graph.replacePlaceholders (grapheditor/Graph.js:16300).
//  * importText: the "list" type is EditorUi.parseOutline (diagramly/EditorUi.js:34433);
//    ParseDialog itself is not in the pruned clone, so the "diagram" type
//    (A -> B lines) is written from its documented behaviour.
//  * importMermaid: draw.io renders Mermaid with the mermaid JavaScript library;
//    this is our own parser of a documented subset (COMPATIBILITY.md).
//
// std-only: no Qt.

#include "qraw/core/cell.h"
#include "qraw/core/geometry.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::commands::imports {

// A problem in the input. `line` is 1-based, 0 when the problem is not about
// one line; `column` is 1-based, 0 when unknown.
struct Diagnostic {
    int line = 0;
    int column = 0;
    std::string message;

    friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

// "Line 3: message" ("Line 3, column 7: message", or only the message).
[[nodiscard]] std::string describe(const Diagnostic& diagnostic);

// The size of the shape that fits `label`, for a cell with `style` (the label is
// HTML when the style says html=1, with placeholders already replaced). The UI
// supplies the measurement of the raster canvas; without one an estimate from
// the number of characters is used, which is deterministic and good enough for tests.
using LabelMeasure = std::function<core::Point(std::string_view style, std::string_view label)>;

struct Options {
    LabelMeasure measure;
};

struct Result {
    // Parents before children; top-level cells have parent "". The ids are only
    // unique inside this result: the insertion gives the cells fresh ones.
    std::vector<core::Cell> cells;
    // Not empty: nothing was imported and `cells` is empty.
    std::vector<Diagnostic> errors;
    // Input that was understood but not used (an unknown directive, a Mermaid
    // line that has no counterpart): the import still happened.
    std::vector<Diagnostic> warnings;

    [[nodiscard]] bool ok() const { return errors.empty() && !cells.empty(); }
};

// --- From Text ------------------------------------------------------------------------

enum class TextType {
    List,    // an indented outline: one vertex per line, a line is the child of the line above with less indentation
    Diagram, // "A -> B" lines: vertices named by their text and the connections between them
};

// How the cells of a text import are arranged. The values of one type only.
enum class TextLayout {
    VerticalTree,   // List
    HorizontalTree, // List
    RadialTree,     // List
    VerticalFlow,   // Diagram
    HorizontalFlow, // Diagram
};

[[nodiscard]] TextType textTypeOf(TextLayout layout);

// List (EditorUi.parseOutline): each non-empty line is a node whose label is the
// trimmed line (`""` is an empty label), a tab counts as 4 spaces, and a line is
// the child of the nearest line above with a smaller indentation.
//
// Diagram: each non-empty line is `A -> B`, `A -> B -> C`, `A <- B`, `A <-> B`,
// `A -- B` (no arrowheads) with an optional `: label` for the connections of
// the line, or a single name for a vertex without connections. A name is the
// trimmed text, or text in double quotes (which may hold `->` and `:`); the
// same name is the same vertex. Lines starting with `//` are comments.
[[nodiscard]] Result importText(std::string_view text, TextType type, TextLayout layout, const Options& options = {});

// --- CSV ------------------------------------------------------------------------------

// Editor.defaultCsvValue (diagramly/Editor.js:1307): the example of draw.io's CSV dialog.
[[nodiscard]] std::string_view csvExample();

// The `# key: value` header (label, labelname, labels, style, parentstyle,
// unknownStyle, stylename, styles, vars, identity, parent, namespace, width,
// height, collapsed, left, top, ignore, connect, link, padding, edgespacing,
// nodespacing, levelspacing, layout), a row of column names and one row per
// cell. Each row becomes a vertex whose value carries the columns as properties
// (a UserObject with placeholders=1), connections from `connect`, and the cells are
// arranged by `layout`. Not supported (warning): container, list, JSON layouts, and
// updating existing cells by `identity`.
[[nodiscard]] Result importCsv(std::string_view text, const Options& options = {});

// --- Mermaid --------------------------------------------------------------------------

// A short example of each supported diagram type, for the dialog.
[[nodiscard]] std::string_view mermaidExample();

// `flowchart` / `graph` (TD TB BT LR RL) and `sequenceDiagram`; any other type
// is an error that names the supported ones. The grammar subset is in COMPATIBILITY.md.
[[nodiscard]] Result importMermaid(std::string_view text, const Options& options = {});

} // namespace qraw::commands::imports
