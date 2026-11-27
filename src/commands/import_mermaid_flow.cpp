// Mermaid flowcharts (`graph` / `flowchart`). Our own reader of the subset in
// COMPATIBILITY.md; the shapes map to the draw.io shapes of the same look and
// the layout is the layered layout of layout_graph.h in the declared
// direction, laid out per subgraph (a subgraph is a container that is a node of
// the level above).
#include "import_mermaid.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <unordered_map>

namespace qraw::commands::imports::detail {

namespace {

using core::Cell;
using core::Point;

enum class Shape {
    Rectangle,
    Rounded,
    Stadium,
    Subroutine,
    Cylinder,
    Circle,
    DoubleCircle,
    Asymmetric,
    Rhombus,
    Hexagon,
    Parallelogram,
    ParallelogramAlt,
    Trapezoid,
    TrapezoidAlt,
};

// The draw.io style of each shape.
std::string shapeStyle(Shape shape)
{
    constexpr const char* common = "whiteSpace=wrap;html=1;";
    switch (shape) {
    case Shape::Rectangle: return std::string("rounded=0;") + common;
    case Shape::Rounded: return std::string("rounded=1;") + common;
    case Shape::Stadium: return std::string("rounded=1;arcSize=50;") + common;
    case Shape::Subroutine: return std::string("shape=process;backgroundOutline=1;") + common;
    case Shape::Cylinder: return std::string("shape=cylinder3;boundedLbl=1;backgroundOutline=1;size=15;") + common;
    case Shape::Circle: return std::string("ellipse;aspect=fixed;") + common;
    case Shape::DoubleCircle: return std::string("ellipse;shape=doubleEllipse;aspect=fixed;") + common;
    case Shape::Asymmetric: return std::string("shape=step;perimeter=stepPerimeter;fixedSize=1;size=15;") + common;
    case Shape::Rhombus: return std::string("rhombus;") + common;
    case Shape::Hexagon: return std::string("shape=hexagon;perimeter=hexagonPerimeter2;fixedSize=1;size=20;") + common;
    case Shape::Parallelogram: return std::string("shape=parallelogram;perimeter=parallelogramPerimeter;fixedSize=1;size=20;") + common;
    case Shape::ParallelogramAlt:
        return std::string("shape=parallelogram;perimeter=parallelogramPerimeter;fixedSize=1;size=20;flipH=1;") + common;
    case Shape::Trapezoid: return std::string("shape=trapezoid;perimeter=trapezoidPerimeter;fixedSize=1;size=20;") + common;
    case Shape::TrapezoidAlt: return std::string("shape=trapezoid;perimeter=trapezoidPerimeter;fixedSize=1;size=20;flipV=1;") + common;
    }
    return {};
}

constexpr char kSubgraphStyle[] =
    "rounded=1;arcSize=2;whiteSpace=wrap;html=1;container=1;collapsible=0;recursiveResize=0;verticalAlign=top;"
    "fillColor=#ffffde;strokeColor=#aaaa33;";

// The size of a node: the label plus the room its shape needs for it.
Point nodeSize(const Options& options, Shape shape, const std::string& style, const std::string& label)
{
    const Point text = measureLabel(options, style, label);
    double w = text.x + 20;
    double h = text.y + 12;
    switch (shape) {
    case Shape::Circle:
    case Shape::DoubleCircle: w = h = std::max(60.0, std::max(text.x, text.y) * 1.3); break;
    case Shape::Rhombus:
        w = std::max(90.0, text.x * 2);
        h = std::max(60.0, text.y * 2);
        break;
    case Shape::Hexagon:
    case Shape::Parallelogram:
    case Shape::ParallelogramAlt:
    case Shape::Trapezoid:
    case Shape::TrapezoidAlt: w += 30; break;
    case Shape::Cylinder: h += 24; break;
    case Shape::Stadium:
    case Shape::Subroutine:
    case Shape::Asymmetric: w += 16; break;
    default: break;
    }
    return {std::max(70.0, std::round(w)), std::max(40.0, std::round(h))};
}

// --- styles (classDef, class, style, linkStyle) ----------------------------------------------

// "rgb(1, 2, 3)" as #010203; other colours as written.
std::string color(std::string_view text)
{
    text = trim(text);
    if (startsWithNoCase(text, "rgb(") && text.back() == ')') {
        int channels[3] = {0, 0, 0};
        std::size_t index = 0;
        std::string number;
        for (const char c : text.substr(4, text.size() - 5)) {
            if (c == ',' || c == ' ') {
                if (!number.empty() && index < 3)
                    channels[index++] = std::atoi(number.c_str());
                number.clear();
            } else {
                number += c;
            }
        }
        if (!number.empty() && index < 3)
            channels[index++] = std::atoi(number.c_str());
        if (index == 3) {
            static constexpr char digits[] = "0123456789abcdef";
            std::string hex = "#";
            for (const int channel : channels) {
                const int v = std::clamp(channel, 0, 255);
                hex += digits[v >> 4];
                hex += digits[v & 15];
            }
            return hex;
        }
    }
    return std::string(text);
}

// "4px" as 4.
std::string pixels(std::string_view text)
{
    text = trim(text);
    if (text.size() > 2 && text.substr(text.size() - 2) == "px")
        text.remove_suffix(2);
    return std::string(text);
}

// The draw.io style tokens for the CSS properties of a classDef or style line.
// Properties without a counterpart are named in `unsupported`.
std::vector<std::pair<std::string, std::string>> cssToStyle(std::string_view css, std::vector<std::string>& unsupported)
{
    std::vector<std::pair<std::string, std::string>> tokens;
    // Split at commas outside parentheses: rgb(1,2,3) is one value.
    std::vector<std::string> properties;
    std::string current;
    int depth = 0;
    for (const char c : css) {
        if (c == '(')
            ++depth;
        else if (c == ')')
            --depth;
        if (c == ',' && depth == 0) {
            properties.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    properties.push_back(current);

    for (const std::string& property : properties) {
        const std::size_t colon = property.find(':');
        if (colon == std::string::npos) {
            if (!trim(property).empty())
                unsupported.emplace_back(trim(property));
            continue;
        }
        const std::string key = lower(trim(std::string_view(property).substr(0, colon)));
        std::string value(trim(std::string_view(property).substr(colon + 1)));
        if (!value.empty() && value.back() == ';')
            value.pop_back();
        if (key == "fill") {
            tokens.emplace_back("fillColor", value == "none" ? "none" : color(value));
        } else if (key == "stroke") {
            tokens.emplace_back("strokeColor", value == "none" ? "none" : color(value));
        } else if (key == "stroke-width") {
            tokens.emplace_back("strokeWidth", pixels(value));
        } else if (key == "color") {
            tokens.emplace_back("fontColor", color(value));
        } else if (key == "font-size") {
            tokens.emplace_back("fontSize", pixels(value));
        } else if (key == "font-weight") {
            tokens.emplace_back("fontStyle", lower(value) == "bold" || value == "700" ? "1" : "0");
        } else if (key == "font-style") {
            tokens.emplace_back("fontStyle", lower(value) == "italic" ? "2" : "0");
        } else if (key == "stroke-dasharray") {
            tokens.emplace_back("dashed", "1");
            tokens.emplace_back("dashPattern", value);
        } else if (key == "opacity") {
            const double opacity = std::atof(value.c_str());
            tokens.emplace_back("opacity", std::to_string(static_cast<int>(std::lround(opacity <= 1 ? opacity * 100 : opacity))));
        } else {
            unsupported.push_back(key);
        }
    }
    return tokens;
}

// --- the model ---------------------------------------------------------------------------------

constexpr int kNone = -1;

enum class Direction { TopDown, BottomUp, LeftRight, RightLeft };

layout::FlowDirection flowDirection(Direction direction)
{
    switch (direction) {
    case Direction::BottomUp: return layout::FlowDirection::Up;
    case Direction::LeftRight: return layout::FlowDirection::Right;
    case Direction::RightLeft: return layout::FlowDirection::Left;
    default: return layout::FlowDirection::Down;
    }
}

std::optional<Direction> parseDirection(std::string_view word)
{
    if (word == "TD" || word == "TB")
        return Direction::TopDown;
    if (word == "BT")
        return Direction::BottomUp;
    if (word == "LR")
        return Direction::LeftRight;
    if (word == "RL")
        return Direction::RightLeft;
    return std::nullopt;
}

struct Element {
    std::string id;
    bool subgraph = false;
    std::string label; // HTML
    Shape shape = Shape::Rectangle;
    int parent = kNone;       // the subgraph it is in
    std::vector<int> members; // subgraphs: the elements inside, in order
    std::vector<std::string> classes;
    std::vector<std::pair<std::string, std::string>> style; // `style` statements
    std::optional<Direction> direction;                     // `direction` inside a subgraph
    double x = 0, y = 0, w = 0, h = 0;                      // inside the parent
};

enum class Stroke { Solid, Dotted, Thick, Invisible };
enum class Head { None, Arrow, Cross, Circle };

struct Link {
    Stroke stroke = Stroke::Solid;
    Head start = Head::None;
    Head end = Head::None;
    std::string label; // HTML
};

struct FlowEdge {
    int from = kNone;
    int to = kNone;
    Link link;
    std::vector<std::pair<std::string, std::string>> style; // `linkStyle`
    int owner = kNone;                                       // the subgraph whose layout routed it, when it did
    std::optional<std::vector<Point>> route;                 // relative to `owner`
};

struct NodeRef {
    std::string id;
    std::optional<Shape> shape;
    std::string text; // as written
    bool hasText = false;
    std::vector<std::string> classes;
};

struct Scanner {
    const Statement& statement;
    std::size_t pos = 0;

    [[nodiscard]] const std::string& text() const { return statement.text; }
    [[nodiscard]] bool end() const { return pos >= statement.text.size(); }
    [[nodiscard]] char peek(std::size_t ahead = 0) const
    {
        return pos + ahead < statement.text.size() ? statement.text[pos + ahead] : '\0';
    }
    [[nodiscard]] bool looking(std::string_view prefix) const { return statement.text.compare(pos, prefix.size(), prefix) == 0; }
    void skipSpaces()
    {
        while (!end() && (peek() == ' ' || peek() == '\t'))
            ++pos;
    }
};

bool isIdChar(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           static_cast<unsigned char>(c) >= 0x80;
}

struct Opener {
    const char* open;
    std::vector<std::pair<const char*, Shape>> closers; // the first that appears ends the text
};

const std::vector<Opener>& openers()
{
    static const std::vector<Opener> table = {
        {"(((", {{")))", Shape::DoubleCircle}}},
        {"((", {{"))", Shape::Circle}}},
        {"([", {{"])", Shape::Stadium}}},
        {"(", {{")", Shape::Rounded}}},
        {"[[", {{"]]", Shape::Subroutine}}},
        {"[(", {{")]", Shape::Cylinder}}},
        {"[/", {{"/]", Shape::Parallelogram}, {"\\]", Shape::Trapezoid}}},
        {"[\\", {{"\\]", Shape::ParallelogramAlt}, {"/]", Shape::TrapezoidAlt}}},
        {"[", {{"]", Shape::Rectangle}}},
        {"{{", {{"}}", Shape::Hexagon}}},
        {"{", {{"}", Shape::Rhombus}}},
        {">", {{"]", Shape::Asymmetric}}},
    };
    return table;
}

class Importer {
public:
    Importer(const std::vector<Statement>& statements, std::size_t first, const Options& options)
        : statements_(statements), first_(first), options_(options)
    {
    }

    Result run(const std::string& directionWord)
    {
        if (!directionWord.empty()) {
            const std::optional<Direction> direction = parseDirection(directionWord);
            if (!direction) {
                error(at(statements_[first_ - 1], 0, "The direction '" + directionWord + "' is not known. Use TD, TB, BT, LR or RL."));
                return std::move(result_);
            }
            direction_ = *direction;
        }
        for (std::size_t i = first_; i < statements_.size(); ++i) {
            if (!statement(statements_[i]))
                return std::move(result_);
        }
        if (!open_.empty()) {
            const Element& unclosed = elements_[static_cast<std::size_t>(open_.back())];
            error({openedAt_.back(), 0, "The subgraph '" + unclosed.id + "' has no matching 'end'."});
            return std::move(result_);
        }
        if (elements_.empty()) {
            error({0, 0, "The flowchart has no nodes."});
            return std::move(result_);
        }
        layoutAll();
        emit();
        return std::move(result_);
    }

private:
    void error(Diagnostic diagnostic) { result_.errors.push_back(std::move(diagnostic)); }
    void warn(const Statement& s, std::string message) { result_.warnings.push_back(at(s, 0, std::move(message))); }

    // --- statements ---

    bool statement(const Statement& s)
    {
        const std::string& text = s.text;
        std::size_t end = 0;
        while (end < text.size() && text[end] != ' ' && text[end] != '\t')
            ++end;
        const std::string word = text.substr(0, end);
        const std::string rest(trim(std::string_view(text).substr(end)));
        if (word == "end" && rest.empty())
            return endSubgraph(s);
        if (word == "subgraph")
            return beginSubgraph(s, rest);
        if (end < text.size() || word == "end") {
            if (word == "direction")
                return setDirection(s, rest);
            if (word == "classDef")
                return classDef(s, rest);
            if (word == "class")
                return classStatement(s, rest);
            if (word == "style")
                return styleStatement(s, rest);
            if (word == "linkStyle")
                return linkStyle(s, rest);
            if (word == "click" || word == "callback" || word == "href" || word == "accTitle" || word == "accDescr" || word == "title") {
                warn(s, "'" + word + "' is ignored.");
                return true;
            }
        }
        return chain(s);
    }

    bool beginSubgraph(const Statement& s, const std::string& rest)
    {
        if (rest.empty()) {
            error(at(s, 0, "A subgraph needs a name: 'subgraph id [Title]'."));
            return false;
        }
        std::string id;
        std::string title;
        const std::size_t bracket = rest.find('[');
        if (rest.front() == '"' && rest.size() >= 2 && rest.back() == '"') {
            id = title = rest.substr(1, rest.size() - 2);
        } else if (bracket != std::string::npos && bracket > 0 && rest.back() == ']') {
            id = std::string(trim(std::string_view(rest).substr(0, bracket)));
            title = rest.substr(bracket + 1, rest.size() - bracket - 2);
        } else {
            id = title = rest;
        }
        const int index = addElement(id, true, s);
        if (index == kNone)
            return false;
        elements_[static_cast<std::size_t>(index)].label = labelHtml(title);
        open_.push_back(index);
        openedAt_.push_back(s.line);
        return true;
    }

    bool endSubgraph(const Statement& s)
    {
        if (open_.empty()) {
            error(at(s, 0, "'end' without a subgraph."));
            return false;
        }
        open_.pop_back();
        openedAt_.pop_back();
        return true;
    }

    bool setDirection(const Statement& s, const std::string& rest)
    {
        const std::optional<Direction> direction = parseDirection(rest);
        if (!direction) {
            error(at(s, 0, "The direction '" + rest + "' is not known. Use TD, TB, BT, LR or RL."));
            return false;
        }
        if (open_.empty())
            direction_ = *direction;
        else
            elements_[static_cast<std::size_t>(open_.back())].direction = direction;
        return true;
    }

    // The styles of a `classDef`, `style` or `linkStyle` line.
    std::vector<std::pair<std::string, std::string>> properties(const Statement& s, const std::string& css)
    {
        std::vector<std::string> unsupported;
        auto tokens = cssToStyle(css, unsupported);
        for (const std::string& name : unsupported) {
            if (std::find(warnedProperties_.begin(), warnedProperties_.end(), name) == warnedProperties_.end()) {
                warnedProperties_.push_back(name);
                warn(s, "The style property '" + name + "' is not supported and is ignored.");
            }
        }
        return tokens;
    }

    bool classDef(const Statement& s, const std::string& rest)
    {
        const std::size_t space = rest.find_first_of(" \t");
        if (space == std::string::npos) {
            error(at(s, 0, "classDef needs a class name and styles: 'classDef name fill:#f9f,stroke:#333'."));
            return false;
        }
        const auto tokens = properties(s, std::string(trim(std::string_view(rest).substr(space))));
        std::string names = rest.substr(0, space);
        std::size_t start = 0;
        while (start <= names.size()) {
            const std::size_t comma = names.find(',', start);
            const std::string name = names.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!name.empty())
                classes_[name] = tokens;
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
        return true;
    }

    bool classStatement(const Statement& s, const std::string& rest)
    {
        const std::size_t space = rest.find_last_of(" \t");
        if (space == std::string::npos) {
            error(at(s, 0, "class needs node ids and a class name: 'class A,B name'."));
            return false;
        }
        const std::string name(trim(std::string_view(rest).substr(space)));
        for (const std::string& id : splitList(rest.substr(0, space))) {
            const auto found = byId_.find(id);
            if (found == byId_.end())
                warn(s, "The class is not applied: there is no node '" + id + "'.");
            else
                elements_[static_cast<std::size_t>(found->second)].classes.push_back(name);
        }
        return true;
    }

    bool styleStatement(const Statement& s, const std::string& rest)
    {
        const std::size_t space = rest.find_first_of(" \t");
        if (space == std::string::npos) {
            error(at(s, 0, "style needs a node id and styles: 'style A fill:#f9f'."));
            return false;
        }
        const std::string id = rest.substr(0, space);
        const auto tokens = properties(s, std::string(trim(std::string_view(rest).substr(space))));
        const auto found = byId_.find(id);
        if (found == byId_.end()) {
            warn(s, "The style is not applied: there is no node '" + id + "'.");
            return true;
        }
        auto& style = elements_[static_cast<std::size_t>(found->second)].style;
        style.insert(style.end(), tokens.begin(), tokens.end());
        return true;
    }

    bool linkStyle(const Statement& s, const std::string& rest)
    {
        const std::size_t space = rest.find_first_of(" \t");
        if (space == std::string::npos) {
            error(at(s, 0, "linkStyle needs link numbers and styles: 'linkStyle 0 stroke:#f00'."));
            return false;
        }
        const std::string which(trim(std::string_view(rest).substr(0, space)));
        const auto tokens = properties(s, std::string(trim(std::string_view(rest).substr(space))));
        const auto apply = [&](FlowEdge& edge) { edge.style.insert(edge.style.end(), tokens.begin(), tokens.end()); };
        if (which == "default") {
            defaultLinkStyle_.insert(defaultLinkStyle_.end(), tokens.begin(), tokens.end());
            return true;
        }
        for (const std::string& number : splitList(which)) {
            char* end = nullptr;
            const long index = std::strtol(number.c_str(), &end, 10);
            if (end == number.c_str() || *end != '\0' || index < 0 || static_cast<std::size_t>(index) >= edges_.size())
                warn(s, "The link style is not applied: there is no link number '" + number + "'.");
            else
                apply(edges_[static_cast<std::size_t>(index)]);
        }
        return true;
    }

    static std::vector<std::string> splitList(const std::string& text)
    {
        std::vector<std::string> items;
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t comma = text.find(',', start);
            const std::string item(trim(std::string_view(text).substr(start, comma == std::string::npos ? std::string::npos : comma - start)));
            if (!item.empty())
                items.push_back(item);
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
        return items;
    }

    // --- nodes and links ---

    int addElement(const std::string& id, bool subgraph, const Statement& s)
    {
        if (elements_.size() >= kMaxVertices) {
            error(at(s, 0, "The flowchart has more than " + std::to_string(kMaxVertices) + " nodes."));
            return kNone;
        }
        Element element;
        element.id = id;
        element.subgraph = subgraph;
        element.label = escapeHtml(id);
        element.parent = open_.empty() ? kNone : open_.back();
        const int index = static_cast<int>(elements_.size());
        if (element.parent != kNone)
            elements_[static_cast<std::size_t>(element.parent)].members.push_back(index);
        elements_.push_back(std::move(element));
        byId_.emplace(id, index); // the first element with an id keeps it
        return index;
    }

    // A node mentioned in a statement: made on first use, and a label or shape written for it replaces what it had.
    int touch(const NodeRef& ref, const Statement& s)
    {
        int index = kNone;
        const auto found = byId_.find(ref.id);
        if (found == byId_.end()) {
            index = addElement(ref.id, false, s);
            if (index == kNone)
                return kNone;
        } else {
            index = found->second;
            Element& element = elements_[static_cast<std::size_t>(index)];
            // Mentioned inside a subgraph while not in one yet: it belongs there.
            if (!open_.empty() && element.parent == kNone && index != open_.back() && !element.subgraph && !isAncestor(index, open_.back())) {
                element.parent = open_.back();
                elements_[static_cast<std::size_t>(open_.back())].members.push_back(index);
            }
        }
        Element& element = elements_[static_cast<std::size_t>(index)];
        if (ref.hasText && !element.subgraph)
            element.label = labelHtml(ref.text);
        if (ref.shape && !element.subgraph)
            element.shape = *ref.shape;
        for (const std::string& name : ref.classes)
            element.classes.push_back(name);
        return index;
    }

    bool isAncestor(int candidate, int of) const
    {
        for (int e = of; e != kNone; e = elements_[static_cast<std::size_t>(e)].parent) {
            if (e == candidate)
                return true;
        }
        return false;
    }

    bool nodeRef(Scanner& in, NodeRef& ref)
    {
        in.skipSpaces();
        const std::size_t start = in.pos;
        while (!in.end() && isIdChar(in.peek()))
            ++in.pos;
        if (in.pos == start) {
            error(at(in.statement, in.pos, in.end() ? "A node id is missing." : std::string("A node id was expected, found '") + in.peek() + "'."));
            return false;
        }
        ref.id = in.text().substr(start, in.pos - start);
        if (in.peek() == '@' && in.peek(1) == '{') {
            error(at(in.statement, in.pos, "Node shapes written as 'id@{ shape: ... }' are not supported."));
            return false;
        }
        for (const Opener& opener : openers()) {
            if (!in.looking(opener.open))
                continue;
            if (!nodeText(in, opener, ref))
                return false;
            break;
        }
        while (in.looking(":::")) {
            in.pos += 3;
            const std::size_t classStart = in.pos;
            while (!in.end() && (isIdChar(in.peek()) || in.peek() == '-'))
                ++in.pos;
            if (in.pos == classStart) {
                error(at(in.statement, in.pos, "A class name is missing after ':::'."));
                return false;
            }
            ref.classes.push_back(in.text().substr(classStart, in.pos - classStart));
        }
        return true;
    }

    bool nodeText(Scanner& in, const Opener& opener, NodeRef& ref)
    {
        const std::size_t openPos = in.pos;
        in.pos += std::char_traits<char>::length(opener.open);
        in.skipSpaces();
        std::size_t textStart = in.pos;
        std::size_t textEnd = std::string::npos;
        if (in.peek() == '"') {
            const std::size_t close = in.text().find('"', in.pos + 1);
            if (close == std::string::npos) {
                error(at(in.statement, in.pos, "The quoted text of the node is not closed."));
                return false;
            }
            textEnd = close + 1;
            in.pos = textEnd;
            in.skipSpaces();
            for (const auto& [closer, shape] : opener.closers) {
                if (in.looking(closer)) {
                    ref.shape = shape;
                    ref.text = in.text().substr(textStart, textEnd - textStart);
                    ref.hasText = true;
                    in.pos += std::char_traits<char>::length(closer);
                    return true;
                }
            }
            error(at(in.statement, in.pos, std::string("Expected '") + opener.closers.front().first + "' after the quoted text."));
            return false;
        }
        std::size_t best = std::string::npos;
        const char* bestCloser = nullptr;
        Shape bestShape = Shape::Rectangle;
        for (const auto& [closer, shape] : opener.closers) {
            const std::size_t found = in.text().find(closer, textStart);
            if (found != std::string::npos && (best == std::string::npos || found < best)) {
                best = found;
                bestCloser = closer;
                bestShape = shape;
            }
        }
        if (best == std::string::npos) {
            error(at(in.statement, openPos, std::string("The text of the node is not closed: expected '") + opener.closers.front().first + "'."));
            return false;
        }
        ref.shape = bestShape;
        ref.text = in.text().substr(textStart, best - textStart);
        ref.hasText = true;
        in.pos = best + std::char_traits<char>::length(bestCloser);
        return true;
    }

    enum class LinkResult { Found, NotALink, Error };

    // The head written at `pos` (> x o) of an arrow's end, consumed when present.
    static Head endHead(Scanner& in)
    {
        if (in.peek() == '>') {
            ++in.pos;
            return Head::Arrow;
        }
        if (in.peek() == 'x') {
            ++in.pos;
            return Head::Cross;
        }
        if (in.peek() == 'o') {
            ++in.pos;
            return Head::Circle;
        }
        return Head::None;
    }

    // The text of a link written between its two halves ("-- text -->"): everything up to the
    // first run of `body` characters that can end a link. Returns npos when there is none.
    static std::size_t findTerminator(const std::string& text, std::size_t from, char body, std::size_t minimum, std::size_t& length)
    {
        for (std::size_t i = from; i + 1 < text.size(); ++i) {
            if (text[i] != body)
                continue;
            std::size_t n = 0;
            while (i + n < text.size() && text[i + n] == body)
                ++n;
            const char next = i + n < text.size() ? text[i + n] : '\0';
            if (n >= minimum + 1 || (n >= minimum && (next == '>' || next == 'x' || next == 'o'))) {
                length = n;
                return i;
            }
            i += n - 1;
        }
        return std::string::npos;
    }

    LinkResult link(Scanner& in, Link& out)
    {
        in.skipSpaces();
        const std::size_t begin = in.pos;
        Head start = Head::None;
        const char first = in.peek();
        if (first == '<') {
            start = Head::Arrow;
            ++in.pos;
        } else if ((first == 'x' || first == 'o') && (in.peek(1) == '-' || in.peek(1) == '=')) {
            start = first == 'x' ? Head::Cross : Head::Circle;
            ++in.pos;
        }
        Link parsed;
        parsed.start = start;
        const char c = in.peek();
        std::string text;
        bool hasText = false;
        if (c == '~') {
            std::size_t n = 0;
            while (in.peek(n) == '~')
                ++n;
            if (n < 3) {
                in.pos = begin;
                return LinkResult::NotALink;
            }
            in.pos += n;
            parsed.stroke = Stroke::Invisible;
        } else if (c == '-' && in.peek(1) == '.') {
            parsed.stroke = Stroke::Dotted;
            in.pos += 2;
            if (in.peek() == ' ') {
                const std::size_t close = in.text().find(".-", in.pos);
                if (close == std::string::npos) {
                    error(at(in.statement, begin, "The text of the link is not closed: expected '.->' or '.-' after it."));
                    return LinkResult::Error;
                }
                text = std::string(trim(std::string_view(in.text()).substr(in.pos, close - in.pos)));
                hasText = true;
                in.pos = close + 2;
            } else {
                while (in.peek() == '.')
                    ++in.pos;
                if (in.peek() != '-') {
                    error(at(in.statement, begin, "A dotted link ends with '-': '-.->' or '-.-'."));
                    return LinkResult::Error;
                }
                ++in.pos;
            }
            parsed.end = endHead(in);
        } else if (c == '-' || c == '=') {
            const bool thick = c == '=';
            std::size_t n = 0;
            while (in.peek(n) == c)
                ++n;
            if (n < 2) {
                in.pos = begin;
                return LinkResult::NotALink;
            }
            parsed.stroke = thick ? Stroke::Thick : Stroke::Solid;
            in.pos += n;
            const char next = in.peek();
            const bool headFollows = next == '>' || next == 'x' || next == 'o';
            if (n == 2 && !headFollows) {
                // "-- text -->": the text comes before the end of the link.
                std::size_t length = 0;
                const std::size_t close = findTerminator(in.text(), in.pos, c, 2, length);
                if (close == std::string::npos) {
                    error(at(in.statement, begin, std::string("The text of the link is not closed: expected '") + (thick ? "==>" : "-->") +
                                                       "' or '" + (thick ? "===" : "---") + "' after it."));
                    return LinkResult::Error;
                }
                text = std::string(trim(std::string_view(in.text()).substr(in.pos, close - in.pos)));
                hasText = true;
                in.pos = close + length;
            }
            parsed.end = endHead(in);
        } else {
            in.pos = begin;
            return LinkResult::NotALink;
        }
        if (parsed.stroke != Stroke::Invisible && !hasText) {
            // "|text|" after the arrow.
            const std::size_t save = in.pos;
            in.skipSpaces();
            if (in.peek() == '|') {
                const std::size_t close = in.text().find('|', in.pos + 1);
                if (close == std::string::npos) {
                    error(at(in.statement, in.pos, "The text of the link is not closed: expected '|'."));
                    return LinkResult::Error;
                }
                text = in.text().substr(in.pos + 1, close - in.pos - 1);
                hasText = true;
                in.pos = close + 1;
            } else {
                in.pos = save;
            }
        }
        if (hasText)
            parsed.label = labelHtml(text);
        out = std::move(parsed);
        return LinkResult::Found;
    }

    bool group(Scanner& in, std::vector<int>& members, const Statement& s)
    {
        for (;;) {
            NodeRef ref;
            if (!nodeRef(in, ref))
                return false;
            const int index = touch(ref, s);
            if (index == kNone)
                return false;
            members.push_back(index);
            in.skipSpaces();
            if (in.peek() != '&')
                return true;
            ++in.pos;
        }
    }

    bool chain(const Statement& s)
    {
        Scanner in{s};
        std::vector<int> left;
        if (!group(in, left, s))
            return false;
        for (;;) {
            in.skipSpaces();
            if (in.end())
                return true;
            Link link;
            const std::size_t at = in.pos;
            const LinkResult found = this->link(in, link);
            if (found == LinkResult::Error)
                return false;
            if (found == LinkResult::NotALink) {
                error(imports::detail::at(s, at, std::string("An arrow (-->, ---, -.->, ==>) or the end of the line was expected, found '") +
                                                     in.text()[at] + "'."));
                return false;
            }
            in.skipSpaces();
            if (in.end()) {
                error(imports::detail::at(s, in.pos, "A node is missing after the arrow."));
                return false;
            }
            std::vector<int> right;
            if (!group(in, right, s))
                return false;
            for (const int from : left) {
                for (const int to : right) {
                    FlowEdge edge;
                    edge.from = from;
                    edge.to = to;
                    edge.link = link;
                    edges_.push_back(std::move(edge));
                }
            }
            left = std::move(right);
        }
    }

    // --- layout ---

    [[nodiscard]] Direction directionOf(int owner) const
    {
        return owner != kNone ? elements_[static_cast<std::size_t>(owner)].direction.value_or(direction_) : direction_;
    }

    // The element of `members`' level that contains `element` (or is it); kNone when it is outside the level.
    [[nodiscard]] int memberOf(int element, int owner) const
    {
        while (element != kNone && elements_[static_cast<std::size_t>(element)].parent != owner)
            element = elements_[static_cast<std::size_t>(element)].parent;
        return element;
    }

    [[nodiscard]] std::string styleOf(const Element& element) const
    {
        std::string base = element.subgraph ? std::string(kSubgraphStyle) : shapeStyle(element.shape);
        core::Style style(base);
        if (!element.subgraph) {
            if (const auto standard = classes_.find("default"); standard != classes_.end()) {
                for (const auto& [key, value] : standard->second)
                    style.set(key, value);
            }
        }
        for (const std::string& name : element.classes) {
            if (const auto found = classes_.find(name); found != classes_.end()) {
                for (const auto& [key, value] : found->second)
                    style.set(key, value);
            }
        }
        for (const auto& [key, value] : element.style)
            style.set(key, value);
        return style.toString();
    }

    void layoutLevel(int owner, const std::vector<int>& members)
    {
        constexpr double kPadding = 20;
        constexpr double kTitle = 30;
        for (const int member : members) {
            Element& element = elements_[static_cast<std::size_t>(member)];
            if (element.subgraph)
                layoutLevel(member, element.members);
            else {
                const Point size = nodeSize(options_, element.shape, styleOf(element), element.label);
                element.w = size.x;
                element.h = size.y;
            }
        }

        layout::Graph graph;
        std::unordered_map<int, int> nodeOf;
        for (const int member : members) {
            const Element& element = elements_[static_cast<std::size_t>(member)];
            nodeOf.emplace(member, static_cast<int>(graph.nodes.size()));
            graph.nodes.push_back({element.id, {0, 0, element.w, element.h}, true});
        }
        std::vector<std::size_t> edgeOf; // layout edge -> edges_
        for (std::size_t e = 0; e < edges_.size(); ++e) {
            const int a = memberOf(edges_[e].from, owner);
            const int b = memberOf(edges_[e].to, owner);
            if (a == kNone || b == kNone || a == b)
                continue;
            graph.edges.push_back({"e" + std::to_string(e), nodeOf[a], nodeOf[b]});
            edgeOf.push_back(e);
        }
        layout::FlowOptions options;
        options.direction = flowDirection(directionOf(owner));
        options.intraCellSpacing = 40;
        options.interRankCellSpacing = 60;
        options.origin = owner == kNone ? Point{0, 0} : Point{kPadding, kPadding + kTitle};
        const layout::Placement placement = layout::flowLayout(graph, options);

        double right = 0;
        double bottom = 0;
        for (std::size_t i = 0; i < members.size(); ++i) {
            Element& element = elements_[static_cast<std::size_t>(members[i])];
            if (i < placement.bounds.size() && placement.bounds[i]) {
                element.x = placement.bounds[i]->x;
                element.y = placement.bounds[i]->y;
            }
            right = std::max(right, element.x + element.w);
            bottom = std::max(bottom, element.y + element.h);
        }
        for (std::size_t k = 0; k < edgeOf.size(); ++k) {
            FlowEdge& edge = edges_[edgeOf[k]];
            // A route computed for the nodes of this level is only right when the edge ends at them.
            if (edge.from == memberOf(edge.from, owner) && edge.to == memberOf(edge.to, owner) && k < placement.edgePoints.size() &&
                placement.edgePoints[k]) {
                edge.owner = owner;
                edge.route = *placement.edgePoints[k];
            }
        }
        if (owner != kNone) {
            Element& container = elements_[static_cast<std::size_t>(owner)];
            const Point title = measureLabel(options_, styleOf(container), container.label);
            container.w = std::max(members.empty() ? 140.0 : 0.0, std::max(title.x + 20, right + kPadding));
            container.h = members.empty() ? kPadding + kTitle + kPadding : bottom + kPadding;
        }
    }

    void layoutAll()
    {
        std::vector<int> top;
        for (std::size_t i = 0; i < elements_.size(); ++i) {
            if (elements_[i].parent == kNone)
                top.push_back(static_cast<int>(i));
        }
        layoutLevel(kNone, top);
        // Absolute positions: the routes are in the coordinates of the page.
        absolute_.assign(elements_.size(), Point{});
        for (const int member : top)
            place(member, Point{0, 0});
    }

    void place(int index, Point origin)
    {
        const Element& element = elements_[static_cast<std::size_t>(index)];
        absolute_[static_cast<std::size_t>(index)] = {origin.x + element.x, origin.y + element.y};
        for (const int member : element.members)
            place(member, absolute_[static_cast<std::size_t>(index)]);
    }

    // --- cells ---

    void emitElement(int index)
    {
        const Element& element = elements_[static_cast<std::size_t>(index)];
        const std::string parent = element.parent == kNone ? std::string() : cellId(element.parent);
        cells_.push_back(Cell::makeVertex(cellId(index), parent, element.label, styleOf(element), {element.x, element.y, element.w, element.h}));
        for (const int member : element.members)
            emitElement(member);
    }

    static std::string cellId(int index) { return "c" + std::to_string(index + 1); }

    std::string edgeStyle(const FlowEdge& edge) const
    {
        const auto arrow = [](Head head) -> const char* {
            switch (head) {
            case Head::Arrow: return "classic";
            case Head::Cross: return "cross";
            case Head::Circle: return "oval";
            default: return "none";
            }
        };
        std::string style = "edgeStyle=orthogonalEdgeStyle;rounded=0;orthogonalLoop=1;jettySize=auto;html=1;";
        const Link& link = edge.link;
        if (link.end != Head::Arrow)
            style += std::string("endArrow=") + arrow(link.end) + ";";
        if (link.start != Head::None)
            style += std::string("startArrow=") + arrow(link.start) + ";";
        if (link.stroke == Stroke::Dotted)
            style += "dashed=1;";
        else if (link.stroke == Stroke::Thick)
            style += "strokeWidth=3;";
        else if (link.stroke == Stroke::Invisible)
            style += "strokeColor=none;endArrow=none;";
        if (!link.label.empty())
            style += "labelBackgroundColor=default;";
        core::Style parsed(style);
        for (const auto& [key, value] : defaultLinkStyle_)
            parsed.set(key, value);
        for (const auto& [key, value] : edge.style)
            parsed.set(key, value);
        return parsed.toString();
    }

    void emit()
    {
        for (std::size_t i = 0; i < elements_.size(); ++i) {
            if (elements_[i].parent == kNone)
                emitElement(static_cast<int>(i));
        }
        for (std::size_t e = 0; e < edges_.size(); ++e) {
            const FlowEdge& edge = edges_[e];
            Cell cell = Cell::makeEdge("e" + std::to_string(e + 1), "", edge.link.label, edgeStyle(edge), cellId(edge.from), cellId(edge.to));
            if (edge.route && !edge.route->empty()) {
                const Point origin = edge.owner == kNone ? Point{0, 0} : absolute_[static_cast<std::size_t>(edge.owner)];
                std::vector<Point> points;
                for (const Point& p : *edge.route)
                    points.push_back({p.x + origin.x, p.y + origin.y});
                cell.geometry()->setPoints(points);
            }
            cells_.push_back(std::move(cell));
        }
        moveToOrigin(cells_);
        result_.cells = std::move(cells_);
    }

    const std::vector<Statement>& statements_;
    std::size_t first_;
    Options options_;
    Direction direction_ = Direction::TopDown;
    std::vector<Element> elements_;
    std::vector<FlowEdge> edges_;
    std::unordered_map<std::string, int> byId_;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> classes_;
    std::vector<std::pair<std::string, std::string>> defaultLinkStyle_;
    std::vector<std::string> warnedProperties_;
    std::vector<int> open_;       // the subgraphs being read
    std::vector<int> openedAt_;   // the lines they were opened at
    std::vector<Point> absolute_; // top-left of each element on the page
    std::vector<Cell> cells_;
    Result result_;
};

} // namespace

Result importMermaidFlowchart(const std::vector<Statement>& statements, std::size_t first, const std::string& direction,
                              const Options& options)
{
    return Importer(statements, first, options).run(direction);
}

} // namespace qraw::commands::imports::detail
