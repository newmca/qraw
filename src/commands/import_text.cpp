// Insert > Advanced > From Text. The list type is EditorUi.parseOutline
// (js/diagramly/EditorUi.js:34433, draw.io, Apache-2.0), modified: the nodes
// become vertices and connectors laid out by our tree layouts instead of
// containers with an ELK layout. ParseDialog (the dialog with the other text
// types) is not in the pruned upstream clone; the diagram type is written from
// its documented behaviour.
#include "import_support.h"

#include <algorithm>
#include <unordered_map>

namespace qraw::commands::imports {

TextType textTypeOf(TextLayout layout)
{
    return layout == TextLayout::VerticalFlow || layout == TextLayout::HorizontalFlow ? TextType::Diagram : TextType::List;
}

namespace {

using core::Cell;
using detail::Arrangement;

constexpr char kVertexStyle[] = "rounded=1;whiteSpace=wrap;html=1;";
constexpr char kFlowEdgeStyle[] = "edgeStyle=orthogonalEdgeStyle;rounded=0;orthogonalLoop=1;jettySize=auto;html=1;";
constexpr double kMinWidth = 80;
constexpr double kMinHeight = 40;

Cell makeNode(const Options& options, const std::string& id, const std::string& text)
{
    const std::string label = detail::escapeHtml(text);
    const core::Point size = detail::measureLabel(options, kVertexStyle, label);
    return Cell::makeVertex(id, "", label, kVertexStyle,
                            {0, 0, std::max(kMinWidth, size.x), std::max(kMinHeight, size.y)});
}

Result tooMany()
{
    Result result;
    result.errors.push_back({0, 0, "The text makes more than " + std::to_string(detail::kMaxVertices) + " shapes."});
    return result;
}

Result noContent()
{
    Result result;
    result.errors.push_back({0, 0, "There is nothing to import: the text has no lines."});
    return result;
}

// --- list -------------------------------------------------------------------------------

Result importList(std::string_view text, TextLayout layout, const Options& options)
{
    struct Open {
        int indent;
        std::string id;
    };
    Result result;
    std::vector<Open> stack;
    std::vector<Cell> edges;
    int vertices = 0;
    const char* elbow = layout == TextLayout::HorizontalTree ? "horizontal" : "vertical";
    const std::string edgeStyle = layout == TextLayout::RadialTree
                                      ? std::string("html=1;endArrow=none;")
                                      : std::string("edgeStyle=elbowEdgeStyle;elbow=") + elbow + ";html=1;endArrow=none;";

    for (const std::string_view line : detail::splitLines(text)) {
        const std::string_view label = detail::trim(line);
        if (label.empty())
            continue;
        if (static_cast<std::size_t>(vertices) >= detail::kMaxVertices)
            return tooMany();
        // The indentation: a tab counts as 4 spaces (EditorUi.parseOutline).
        int indent = 0;
        for (const char c : line) {
            if (c == ' ')
                ++indent;
            else if (c == '\t')
                indent += 4;
            else
                break;
        }
        const std::string id = "n" + std::to_string(++vertices);
        result.cells.push_back(makeNode(options, id, label == "\"\"" ? std::string() : std::string(label)));
        while (!stack.empty() && stack.back().indent >= indent)
            stack.pop_back();
        if (!stack.empty())
            edges.push_back(Cell::makeEdge("e" + std::to_string(vertices), "", "", edgeStyle, stack.back().id, id));
        stack.push_back({indent, id});
    }
    if (vertices == 0)
        return noContent();
    for (Cell& edge : edges)
        result.cells.push_back(std::move(edge));

    const Arrangement arrangement = layout == TextLayout::HorizontalTree ? Arrangement::HorizontalTree
                                    : layout == TextLayout::RadialTree   ? Arrangement::RadialTree
                                                                         : Arrangement::VerticalTree;
    detail::Spacing spacing;
    spacing.level = arrangement == Arrangement::RadialTree ? 60 : 40;
    detail::arrange(result.cells, arrangement, spacing);
    return result;
}

// --- diagram -----------------------------------------------------------------------------

struct Arrow {
    bool toStart = false; // an arrowhead at the first name
    bool toEnd = false;   // an arrowhead at the second name
};

// The arrow at the start of `text`: optional '<', dashes (two or more, or one
// followed by '>'), optional '>'. Length 0 when there is none.
std::size_t arrowAt(std::string_view text, Arrow& arrow)
{
    std::size_t i = 0;
    const bool head = !text.empty() && text[0] == '<';
    if (head)
        ++i;
    const std::size_t dashesStart = i;
    while (i < text.size() && text[i] == '-')
        ++i;
    const std::size_t dashes = i - dashesStart;
    const bool tail = i < text.size() && text[i] == '>' && dashes > 0;
    if (tail)
        ++i;
    if (dashes == 0 || (dashes == 1 && !head && !tail))
        return 0;
    arrow.toStart = head;
    arrow.toEnd = tail;
    return i;
}

struct DiagramLine {
    std::vector<std::string> names;
    std::vector<Arrow> arrows; // between names[i] and names[i + 1]
    std::string label;
    bool hasLabel = false;
};

// Parses one line. Returns an error message (and the 1-based column) in `error`.
bool parseDiagramLine(std::string_view line, DiagramLine& out, std::string& error, int& column)
{
    std::size_t pos = 0;
    const auto at = [&](std::size_t p) { return static_cast<int>(p) + 1; };
    for (;;) {
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
            ++pos;
        std::string name;
        if (pos < line.size() && line[pos] == '"') {
            const std::size_t close = line.find('"', pos + 1);
            if (close == std::string_view::npos) {
                error = "The name in quotes is not closed.";
                column = at(pos);
                return false;
            }
            name = std::string(line.substr(pos + 1, close - pos - 1));
            pos = close + 1;
            while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
                ++pos;
            Arrow ignored;
            if (pos < line.size() && line[pos] != ':' && arrowAt(line.substr(pos), ignored) == 0) {
                error = "Unexpected text after the name in quotes.";
                column = at(pos);
                return false;
            }
        } else {
            const std::size_t start = pos;
            Arrow ignored;
            while (pos < line.size() && line[pos] != ':' && arrowAt(line.substr(pos), ignored) == 0)
                ++pos;
            name = std::string(detail::trim(line.substr(start, pos - start)));
        }
        const bool more = pos < line.size() && line[pos] != ':';
        if (name.empty()) {
            error = out.names.empty() ? "The line starts without a name." : "A connection needs a name after the arrow.";
            column = at(pos);
            return false;
        }
        out.names.push_back(std::move(name));
        if (!more)
            break;
        Arrow arrow;
        const std::size_t length = arrowAt(line.substr(pos), arrow);
        out.arrows.push_back(arrow);
        pos += length;
    }
    if (pos < line.size() && line[pos] == ':') {
        out.hasLabel = true;
        out.label = std::string(detail::trim(line.substr(pos + 1)));
    }
    return true;
}

Result importDiagram(std::string_view text, TextLayout layout, const Options& options)
{
    Result result;
    std::unordered_map<std::string, std::string> vertexOf; // name -> id
    std::vector<Cell> edges;
    int vertices = 0;
    int number = 0;

    const auto vertex = [&](const std::string& name) -> const std::string& {
        const auto found = vertexOf.find(name);
        if (found != vertexOf.end())
            return found->second;
        const std::string id = "n" + std::to_string(++vertices);
        result.cells.push_back(makeNode(options, id, name));
        return vertexOf.emplace(name, id).first->second;
    };

    int lineNumber = 0;
    for (std::string_view line : detail::splitLines(text)) {
        ++lineNumber;
        std::size_t lead = 0;
        while (lead < line.size() && (line[lead] == ' ' || line[lead] == '\t'))
            ++lead;
        line = detail::trim(line);
        if (line.empty() || detail::startsWith(line, "//"))
            continue;
        if (static_cast<std::size_t>(vertices) >= detail::kMaxVertices)
            return tooMany();
        DiagramLine parsed;
        std::string error;
        int column = 0;
        if (!parseDiagramLine(line, parsed, error, column)) {
            result.cells.clear();
            result.errors.push_back({lineNumber, column + static_cast<int>(lead), error});
            return result;
        }
        if (parsed.names.empty())
            continue;
        for (const std::string& name : parsed.names)
            vertex(name);
        if (parsed.hasLabel && parsed.arrows.empty()) {
            result.warnings.push_back({lineNumber, 0, "The label is ignored: the line has no connection."});
        }
        for (std::size_t i = 0; i < parsed.arrows.size(); ++i) {
            const Arrow& arrow = parsed.arrows[i];
            std::string style = kFlowEdgeStyle;
            if (!arrow.toStart && !arrow.toEnd)
                style += "endArrow=none;";
            else if (arrow.toStart && !arrow.toEnd)
                style += "startArrow=classic;endArrow=none;"; // "<-": the arrowhead is at the first name
            else if (arrow.toStart && arrow.toEnd)
                style += "startArrow=classic;";
            const std::string label = parsed.hasLabel ? detail::escapeHtml(parsed.label) : std::string();
            edges.push_back(Cell::makeEdge("e" + std::to_string(++number), "", label, style, vertexOf[parsed.names[i]],
                                           vertexOf[parsed.names[i + 1]]));
        }
    }
    if (vertices == 0)
        return noContent();
    for (Cell& edge : edges)
        result.cells.push_back(std::move(edge));

    detail::Spacing spacing;
    spacing.node = 30;
    spacing.level = 60;
    detail::arrange(result.cells, layout == TextLayout::HorizontalFlow ? Arrangement::HorizontalFlow : Arrangement::VerticalFlow, spacing);
    return result;
}

} // namespace

Result importText(std::string_view text, TextType type, TextLayout layout, const Options& options)
{
    if (textTypeOf(layout) != type)
        layout = type == TextType::List ? TextLayout::VerticalTree : TextLayout::VerticalFlow;
    return type == TextType::List ? importList(text, layout, options) : importDiagram(text, layout, options);
}

} // namespace qraw::commands::imports
