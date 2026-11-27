#pragma once

// Helpers shared by the importers of Insert > Advanced (import_text.cpp,
// import_csv.cpp, import_mermaid*.cpp). Internal to qraw_commands.

#include "qraw/commands/import_diagram.h"
#include "qraw/commands/layout_graph.h"

#include <string>
#include <string_view>
#include <vector>

namespace qraw::commands::imports::detail {

// The most vertices an import makes: the layouts and the insertion are meant for
// diagrams people read, and a runaway input should be an error, not a frozen window.
inline constexpr std::size_t kMaxVertices = 10000;

// --- text ----------------------------------------------------------------------------

[[nodiscard]] std::string_view trim(std::string_view text);
// The lines of `text` without their line breaks (\n, \r\n and a lone \r end a line).
[[nodiscard]] std::vector<std::string_view> splitLines(std::string_view text);
// mxUtils.htmlEntities(text, false): & < > and " for use in an HTML label.
[[nodiscard]] std::string escapeHtml(std::string_view text);
// The text of an HTML label as plain text (tags dropped, <br> as a line break, entities decoded).
[[nodiscard]] std::string plainText(std::string_view html);
[[nodiscard]] std::string lower(std::string_view text);
[[nodiscard]] bool startsWith(std::string_view text, std::string_view prefix);
[[nodiscard]] bool startsWithNoCase(std::string_view text, std::string_view prefix);
// Number of UTF-8 characters.
[[nodiscard]] std::size_t characterCount(std::string_view text);

// --- sizes ---------------------------------------------------------------------------

// The size of the label with `options.measure`, or an estimate (7 px per
// character, 18 px per line, and the 10 x 4 px buffer of
// Graph.getPreferredSizeForCell) when there is none.
[[nodiscard]] core::Point measureLabel(const Options& options, std::string_view style, std::string_view label);

// --- arranging -----------------------------------------------------------------------

enum class Arrangement {
    None,
    VerticalTree,
    HorizontalTree,
    RadialTree,
    OrgChart,
    VerticalFlow,
    HorizontalFlow,
    Organic,
    Circle,
};

struct Spacing {
    double node = 20;  // between neighbours of a level or rank
    double level = 30; // between levels or ranks
    double edge = 10;  // between parallel edges
};

// Lays out the top-level vertices of `cells` (parent "") and the top-level
// edges between them, the way Arrange > Layout does (ops::arrangeLayout):
// vertices move, edge waypoints are reset or set to the route of a flow, and the
// edge style follows the arrangement. Afterwards the top-left corner of the
// vertices and waypoints is at (0, 0).
void arrange(std::vector<core::Cell>& cells, Arrangement arrangement, const Spacing& spacing);

// Moves the top-level vertices and the waypoints of the top-level edges so that
// their top-left corner is at (0, 0). Cells with a parent move with it.
void moveToOrigin(std::vector<core::Cell>& cells);

// --- JSON ----------------------------------------------------------------------------

// A JSON value (RFC 8259), enough for the directives of the CSV header.
struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Json> array;
    std::vector<std::pair<std::string, Json>> object; // in document order

    [[nodiscard]] const Json* find(std::string_view key) const; // own member of an object, or nullptr
    // The value as text: strings as they are, numbers and booleans as JSON writes them, else "".
    [[nodiscard]] std::string text() const;
};

// Parses `text`. On failure returns false and says what is wrong in `error`
// (with the offset in `text`).
[[nodiscard]] bool parseJson(std::string_view text, Json& out, std::string& error);

} // namespace qraw::commands::imports::detail
