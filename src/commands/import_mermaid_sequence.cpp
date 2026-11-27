// Mermaid sequence diagrams. Our own reader of the subset in COMPATIBILITY.md:
// participants and actors (umlLifeline), messages (edges that leave and enter
// the lifelines at the height of their row), notes, activation bars and
// loop / alt / opt / par / critical / break frames (umlFrame). The layout is a
// column per participant and a row per message, note or block edge; the
// columns are spaced so that every message text and note fits.
#include "import_mermaid.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace qraw::commands::imports::detail {

namespace {

using core::Cell;
using core::Point;

constexpr double kHeadSize = 40;      // the box of a participant
constexpr double kActorSize = 60;     // the figure of an actor
constexpr double kMinColumnGap = 40;  // between the boxes of neighbouring participants
constexpr double kBarWidth = 10;
constexpr double kBarStep = 5;        // nested bars are shifted by this
constexpr double kTextHeight = 18;
constexpr char kMeasureStyle[] = "html=1;whiteSpace=wrap;";

constexpr char kLifelineStyle[] =
    "shape=umlLifeline;perimeter=lifelinePerimeter;whiteSpace=wrap;html=1;container=1;dropTarget=0;collapsible=0;"
    "recursiveResize=0;outlineConnect=0;portConstraint=eastwest;";
constexpr char kBarStyle[] = "html=1;points=[];outlineConnect=0;portConstraint=eastwest;";
constexpr char kNoteStyle[] = "shape=note;whiteSpace=wrap;html=1;size=8;fillColor=#fff2cc;strokeColor=#d6b656;align=left;spacingLeft=4;";
constexpr char kFrameStyle[] = "shape=umlFrame;whiteSpace=wrap;html=1;pointerEvents=0;";
constexpr char kGuardStyle[] = "text;html=1;align=left;verticalAlign=middle;spacingLeft=4;spacingRight=4;";

struct Participant {
    std::string id;
    std::string label; // HTML
    bool actor = false;
    double cell = 0;   // the width of the lifeline's box
    double extent = 0; // the room the head and its label need
    double cx = 0;     // the centre
    std::vector<double> bars; // start heights of the open activation bars
};

struct Arrow {
    bool dashed = false;
    enum class Head { None, Block, Cross, Open } end = Head::Block;
    bool both = false;
};

struct Message {
    int from = 0;
    int to = 0;
    Arrow arrow;
    std::string label;
    double y = 0;      // the height of the arrow
    int fromDepth = 0; // open bars at the sender and at the receiver when the message is drawn
    int toDepth = 0;
    double textWidth = 0;
};

enum class Place { Left, Right, Over };

struct NoteEvent {
    Place place = Place::Over;
    int first = 0;
    int last = 0;
    std::string label;
    double y = 0;
    double width = 0;
    double height = 0;
};

struct Bar {
    int participant = 0;
    int level = 0; // 0 for the outermost
    double top = 0;
    double bottom = 0;
};

struct Divider {
    double y = 0;
    std::string text;
};

struct Frame {
    std::string kind; // loop, alt ... ; empty for blocks that are not drawn (rect, box)
    std::string text; // HTML
    double top = 0;
    double bottom = 0;
    int depth = 0;
    int firstColumn = std::numeric_limits<int>::max();
    int lastColumn = -1;
    int line = 0;
    std::vector<Divider> dividers;
};

struct ColumnNeed {
    int a = 0;
    int b = 0;
    double needed = 0; // between the centres of columns a and b
};

class Importer {
public:
    Importer(const std::vector<Statement>& statements, std::size_t first, const Options& options)
        : statements_(statements), first_(first), options_(options)
    {
    }

    Result run()
    {
        for (std::size_t i = first_; i < statements_.size(); ++i) {
            if (!statement(statements_[i]))
                return std::move(result_);
        }
        if (!open_.empty()) {
            const Frame& unclosed = frames_[open_.back()];
            result_.errors.push_back({unclosed.line, 0, "The '" + (unclosed.kind.empty() ? std::string("block") : unclosed.kind) +
                                                            "' block has no matching 'end'."});
            return std::move(result_);
        }
        if (participants_.empty()) {
            result_.errors.push_back({0, 0, "The sequence diagram has no participants and no messages."});
            return std::move(result_);
        }
        closeBars();
        layoutColumns();
        emit();
        return std::move(result_);
    }

private:
    void error(const Statement& s, std::size_t offset, std::string message) { result_.errors.push_back(at(s, offset, std::move(message))); }
    void warn(const Statement& s, std::string message) { result_.warnings.push_back(at(s, 0, std::move(message))); }

    [[nodiscard]] Point measure(const std::string& html) const { return measureLabel(options_, kMeasureStyle, html); }

    // --- participants ---

    int participant(const std::string& id, bool actor, std::optional<std::string> label, const Statement& s)
    {
        for (std::size_t i = 0; i < participants_.size(); ++i) {
            if (participants_[i].id == id) {
                if (label) {
                    participants_[i].label = *label;
                    participants_[i].actor = actor;
                }
                return static_cast<int>(i);
            }
        }
        if (participants_.size() >= 200) {
            error(s, 0, "The sequence diagram has more than 200 participants.");
            return -1;
        }
        Participant p;
        p.id = id;
        p.label = label ? *label : escapeHtml(id);
        p.actor = actor;
        participants_.push_back(std::move(p));
        return static_cast<int>(participants_.size()) - 1;
    }

    // --- statements ---

    bool statement(const Statement& s)
    {
        const std::string& text = s.text;
        std::size_t end = 0;
        while (end < text.size() && text[end] != ' ' && text[end] != '\t')
            ++end;
        const std::string word = lower(std::string_view(text).substr(0, end));
        const std::string rest(trim(std::string_view(text).substr(end)));

        if (word == "participant" || word == "actor")
            return declare(s, word == "actor", rest);
        if (word == "create" && (startsWithNoCase(rest, "participant ") || startsWithNoCase(rest, "actor "))) {
            const std::size_t space = rest.find(' ');
            return declare(s, startsWithNoCase(rest, "actor"), std::string(trim(std::string_view(rest).substr(space))));
        }
        if (word == "destroy") {
            warn(s, "'destroy' is ignored: the lifeline stays to the end.");
            return true;
        }
        if (word == "note")
            return note(s, rest);
        if (word == "activate" || word == "deactivate")
            return activation(s, word == "activate", rest);
        if (word == "autonumber") {
            autonumber_ = lower(rest) != "off";
            return true;
        }
        if (word == "loop" || word == "alt" || word == "opt" || word == "par" || word == "critical" || word == "break")
            return beginBlock(s, word, rest);
        if (word == "rect" || word == "box") {
            blockWithoutFrame(s, word);
            return true;
        }
        if (word == "else" || word == "and" || word == "option")
            return divider(s, word, rest);
        if (word == "end" && rest.empty())
            return endBlock(s);
        if (word == "title" || word == "link" || word == "links" || word == "properties" || word == "details" ||
            word == "accdescr" || word == "acctitle") {
            warn(s, "'" + word + "' is ignored.");
            return true;
        }
        return message(s);
    }

    bool declare(const Statement& s, bool actor, const std::string& rest)
    {
        if (rest.empty()) {
            error(s, 0, std::string("A ") + (actor ? "actor" : "participant") + " needs a name.");
            return false;
        }
        std::string id = rest;
        std::optional<std::string> label;
        // "A as Alice": the id is the first word, the label the rest.
        const std::size_t space = rest.find_first_of(" \t");
        if (space != std::string::npos) {
            const std::string_view tail = trim(std::string_view(rest).substr(space));
            if (startsWithNoCase(tail, "as ") || startsWithNoCase(tail, "as\t")) {
                id = rest.substr(0, space);
                label = labelHtml(trim(tail.substr(2)));
            } else {
                error(s, 0, "Expected 'as' after the participant id: 'participant A as Alice'.");
                return false;
            }
        } else {
            label = escapeHtml(id);
        }
        return participant(id, actor, label, s) >= 0;
    }

    // The id at `pos`: up to white space, an arrow or a colon.
    static std::string idAt(const std::string& text, std::size_t& pos)
    {
        const std::size_t start = pos;
        while (pos < text.size()) {
            const char c = text[pos];
            if (c == ' ' || c == '\t' || c == ':' || c == ',' || c == '+' || c == '<' || c == '>')
                break;
            if (c == '-' && (pos + 1 >= text.size() || std::string_view("->x)").find(text[pos + 1]) != std::string_view::npos))
                break;
            ++pos;
        }
        return text.substr(start, pos - start);
    }

    static void skipSpaces(const std::string& text, std::size_t& pos)
    {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
            ++pos;
    }

    static bool arrowAt(const std::string& text, std::size_t& pos, Arrow& arrow)
    {
        struct Token {
            const char* text;
            bool dashed;
            Arrow::Head head;
            bool both;
        };
        static const Token tokens[] = {
            {"<<-->>", true, Arrow::Head::Block, true}, {"<<->>", false, Arrow::Head::Block, true},
            {"-->>", true, Arrow::Head::Block, false},  {"->>", false, Arrow::Head::Block, false},
            {"--x", true, Arrow::Head::Cross, false},   {"-x", false, Arrow::Head::Cross, false},
            {"--)", true, Arrow::Head::Open, false},    {"-)", false, Arrow::Head::Open, false},
            {"-->", true, Arrow::Head::None, false},    {"->", false, Arrow::Head::None, false},
        };
        for (const Token& token : tokens) {
            if (text.compare(pos, std::char_traits<char>::length(token.text), token.text) == 0) {
                arrow.dashed = token.dashed;
                arrow.end = token.head;
                arrow.both = token.both;
                pos += std::char_traits<char>::length(token.text);
                return true;
            }
        }
        return false;
    }

    bool message(const Statement& s)
    {
        const std::string& text = s.text;
        std::size_t pos = 0;
        const std::string sender = idAt(text, pos);
        skipSpaces(text, pos);
        Arrow arrow;
        const std::size_t arrowPos = pos;
        if (sender.empty() || !arrowAt(text, pos, arrow)) {
            error(s, sender.empty() || arrowPos >= text.size() ? 0 : arrowPos,
                  "Unrecognised statement. Expected a message like 'Alice->>Bob: Hello', a note, 'participant', 'activate' or a block.");
            return false;
        }
        bool activate = false;
        bool deactivate = false;
        if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
            (text[pos] == '+' ? activate : deactivate) = true;
            ++pos;
        }
        skipSpaces(text, pos);
        const std::string receiver = idAt(text, pos);
        if (receiver.empty()) {
            error(s, pos, "A message needs a receiver after the arrow.");
            return false;
        }
        skipSpaces(text, pos);
        std::string label;
        if (pos < text.size()) {
            if (text[pos] != ':') {
                error(s, pos, "Expected ':' and the text of the message after the receiver.");
                return false;
            }
            label = labelHtml(trim(std::string_view(text).substr(pos + 1)));
        }
        const int from = participant(sender, false, std::nullopt, s);
        const int to = participant(receiver, false, std::nullopt, s);
        if (from < 0 || to < 0)
            return false;
        if (autonumber_)
            label = std::to_string(++number_) + ". " + label;

        Message m;
        m.from = from;
        m.to = to;
        m.arrow = arrow;
        m.label = label;
        const Point size = measure(label);
        m.textWidth = label.empty() ? 0 : size.x;
        const double textHeight = label.empty() ? 0 : std::max(kTextHeight, size.y);
        m.y = y_ + textHeight + 8;
        if (activate)
            participants_[static_cast<std::size_t>(to)].bars.push_back(m.y);
        m.fromDepth = static_cast<int>(participants_[static_cast<std::size_t>(from)].bars.size());
        m.toDepth = static_cast<int>(participants_[static_cast<std::size_t>(to)].bars.size());
        if (deactivate)
            endBar(from, m.y);
        y_ = m.y + (from == to ? 20 + 16 : 20);
        lastY_ = m.y;
        messages_.push_back(m);
        touchFrames(std::min(from, to), std::max(from, to));
        if (from == to) {
            spacings_.push_back({from, from + 1, m.textWidth + 60});
        } else {
            spacings_.push_back({std::min(from, to), std::max(from, to), m.textWidth + 30});
        }
        return true;
    }

    bool note(const Statement& s, const std::string& rest)
    {
        Place place = Place::Over;
        std::string tail = rest;
        if (startsWithNoCase(rest, "left of ")) {
            place = Place::Left;
            tail = rest.substr(8);
        } else if (startsWithNoCase(rest, "right of ")) {
            place = Place::Right;
            tail = rest.substr(9);
        } else if (startsWithNoCase(rest, "over ")) {
            tail = rest.substr(5);
        } else {
            error(s, 0, "A note is 'Note left of A: text', 'Note right of A: text' or 'Note over A,B: text'.");
            return false;
        }
        const std::size_t colon = tail.find(':');
        if (colon == std::string::npos) {
            error(s, 0, "A note needs ':' and its text.");
            return false;
        }
        NoteEvent n;
        n.place = place;
        std::vector<int> columns;
        std::size_t start = 0;
        const std::string names = tail.substr(0, colon);
        while (start <= names.size()) {
            const std::size_t comma = names.find(',', start);
            const std::string id(trim(std::string_view(names).substr(start, comma == std::string::npos ? std::string::npos : comma - start)));
            if (id.empty()) {
                error(s, 0, "A note needs the name of a participant.");
                return false;
            }
            const int index = participant(id, false, std::nullopt, s);
            if (index < 0)
                return false;
            columns.push_back(index);
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
        if (columns.size() > 2 || (columns.size() == 2 && place != Place::Over)) {
            error(s, 0, "A note is next to one participant, or over one or two.");
            return false;
        }
        n.first = *std::min_element(columns.begin(), columns.end());
        n.last = *std::max_element(columns.begin(), columns.end());
        n.label = labelHtml(trim(std::string_view(tail).substr(colon + 1)));
        const Point size = measure(n.label);
        n.width = std::clamp(size.x + 24, 100.0, 260.0);
        const double lines = std::max(std::ceil(size.y / kTextHeight), std::ceil(size.x / (n.width - 24)));
        n.height = std::max(34.0, lines * kTextHeight + 16);
        n.y = y_;
        y_ += n.height + 14;
        notes_.push_back(n);
        touchFrames(n.first, n.last);
        if (place == Place::Right)
            spacings_.push_back({n.first, n.first + 1, n.width + 30});
        else if (place == Place::Left)
            spacings_.push_back({n.first - 1, n.first, n.width + 30});
        return true;
    }

    // The bar that ends at height `y` at the participant, when one is open.
    void endBar(int column, double y)
    {
        Participant& p = participants_[static_cast<std::size_t>(column)];
        if (p.bars.empty())
            return;
        bars_.push_back({column, static_cast<int>(p.bars.size()) - 1, p.bars.back() - 5, y + 5});
        p.bars.pop_back();
    }

    bool activation(const Statement& s, bool start, const std::string& rest)
    {
        if (rest.empty()) {
            error(s, 0, start ? "'activate' needs a participant." : "'deactivate' needs a participant.");
            return false;
        }
        const int column = participant(rest, false, std::nullopt, s);
        if (column < 0)
            return false;
        // The bar starts or ends at the arrow of the message before the statement.
        const double y = messages_.empty() ? y_ : lastY_;
        if (start) {
            participants_[static_cast<std::size_t>(column)].bars.push_back(y);
        } else if (participants_[static_cast<std::size_t>(column)].bars.empty()) {
            error(s, 0, "'deactivate " + rest + "' without an 'activate'.");
            return false;
        } else {
            endBar(column, y);
        }
        return true;
    }

    void closeBars()
    {
        for (std::size_t i = 0; i < participants_.size(); ++i) {
            while (!participants_[i].bars.empty())
                endBar(static_cast<int>(i), std::max(lastY_, participants_[i].bars.back()));
        }
    }

    // --- blocks ---

    // Spans the columns in the frames that are open.
    void touchFrames(int first, int last)
    {
        for (const std::size_t index : open_) {
            frames_[index].firstColumn = std::min(frames_[index].firstColumn, first);
            frames_[index].lastColumn = std::max(frames_[index].lastColumn, last);
        }
    }

    bool beginBlock(const Statement& s, const std::string& kind, const std::string& rest)
    {
        Frame f;
        f.kind = kind;
        f.text = rest.empty() ? std::string() : labelHtml(rest);
        f.top = y_;
        f.depth = static_cast<int>(open_.size());
        f.line = s.line;
        frames_.push_back(std::move(f));
        open_.push_back(frames_.size() - 1);
        y_ += 28;
        return true;
    }

    // rect (a coloured background) and box (a group of participants) have no counterpart; the lines
    // up to their `end` are read as usual.
    void blockWithoutFrame(const Statement& s, const std::string& word)
    {
        warn(s, "'" + word + "' is ignored: its contents are drawn without it.");
        Frame f;
        f.line = s.line;
        frames_.push_back(std::move(f));
        open_.push_back(frames_.size() - 1);
    }

    bool divider(const Statement& s, const std::string& word, const std::string& rest)
    {
        const char* expected = word == "else" ? "alt" : word == "and" ? "par" : "critical";
        if (open_.empty() || frames_[open_.back()].kind != expected) {
            error(s, 0, "'" + word + "' is only allowed inside '" + expected + "'.");
            return false;
        }
        frames_[open_.back()].dividers.push_back({y_, rest.empty() ? std::string() : labelHtml(rest)});
        y_ += 24;
        return true;
    }

    bool endBlock(const Statement& s)
    {
        if (open_.empty()) {
            error(s, 0, "'end' without a block.");
            return false;
        }
        Frame& f = frames_[open_.back()];
        open_.pop_back();
        if (!f.kind.empty()) {
            f.bottom = y_ + 6;
            y_ = f.bottom + 14;
        }
        return true;
    }

    // --- layout ---

    void layoutColumns()
    {
        double maxHead = kHeadSize;
        for (Participant& p : participants_) {
            const Point label = measureLabel(options_, kMeasureStyle, p.label);
            p.cell = p.actor ? 30 : std::max(100.0, label.x + 20);
            p.extent = p.actor ? std::max(60.0, label.x + 10) : p.cell;
            if (p.actor)
                maxHead = std::max(maxHead, kActorSize);
        }
        top_ = maxHead + 30;
        double x = 0;
        for (std::size_t i = 0; i < participants_.size(); ++i) {
            if (i > 0)
                x += participants_[i - 1].extent / 2 + participants_[i].extent / 2 + kMinColumnGap;
            else
                x = participants_[i].extent / 2;
            participants_[i].cx = x;
        }
        // The columns a message or note spans must hold its text: nearest columns first.
        const int count = static_cast<int>(participants_.size());
        std::vector<ColumnNeed> constraints;
        for (const ColumnNeed& c : spacings_) {
            if (c.a >= 0 && c.b < count && c.a < c.b)
                constraints.push_back(c);
        }
        std::stable_sort(constraints.begin(), constraints.end(), [](const ColumnNeed& l, const ColumnNeed& r) { return l.b - l.a < r.b - r.a; });
        for (const ColumnNeed& c : constraints) {
            const double have = participants_[static_cast<std::size_t>(c.b)].cx - participants_[static_cast<std::size_t>(c.a)].cx;
            if (have >= c.needed)
                continue;
            const double delta = c.needed - have;
            for (int j = c.a + 1; j < count; ++j) {
                const double share = j <= c.b ? static_cast<double>(j - c.a) / (c.b - c.a) : 1.0;
                participants_[static_cast<std::size_t>(j)].cx += delta * share;
            }
        }
        bottom_ = top_ + y_ + 30;
    }

    // --- cells ---

    std::string participantId(int index) const { return "p" + std::to_string(index + 1); }

    // The x offset from the centre of a lifeline where a message meets its bars: `depth` bars are open
    // and the message arrives or leaves towards the right (`rightwards`).
    static double barEdge(int depth, bool rightwards)
    {
        if (depth <= 0)
            return 0;
        return (depth - 1) * kBarStep + (rightwards ? kBarWidth / 2 : -kBarWidth / 2);
    }

    void emitFrames()
    {
        for (const Frame& f : frames_) {
            if (f.kind.empty())
                continue;
            const int first = f.lastColumn < 0 ? 0 : f.firstColumn;
            const int last = f.lastColumn < 0 ? static_cast<int>(participants_.size()) - 1 : f.lastColumn;
            const double pad = std::max(20.0, 50.0 - f.depth * 10);
            const double left = participants_[static_cast<std::size_t>(first)].cx - pad;
            const double right = participants_[static_cast<std::size_t>(last)].cx + pad;
            const double top = top_ + f.top;
            const double bottom = top_ + f.bottom;
            const double tab = std::max(50.0, measure(escapeHtml(f.kind)).x + 16);
            cells_.push_back(Cell::makeVertex("f" + std::to_string(++count_), "", escapeHtml(f.kind),
                                              std::string(kFrameStyle) + "width=" + std::to_string(static_cast<int>(tab)) + ";height=20;",
                                              {left, top, right - left, bottom - top}));
            if (!f.text.empty()) {
                cells_.push_back(Cell::makeVertex("f" + std::to_string(++count_), "", "[" + f.text + "]", kGuardStyle,
                                                  {left + tab + 6, top, std::max(60.0, right - left - tab - 12), 20}));
            }
            for (const Divider& d : f.dividers) {
                Cell line = Cell::makeEdge("f" + std::to_string(++count_), "", "", "endArrow=none;html=1;dashed=1;", "", "");
                line.geometry()->setSourcePoint(Point{left, top_ + d.y});
                line.geometry()->setTargetPoint(Point{right, top_ + d.y});
                cells_.push_back(std::move(line));
                if (!d.text.empty()) {
                    cells_.push_back(Cell::makeVertex("f" + std::to_string(++count_), "", "[" + d.text + "]", kGuardStyle,
                                                      {left + 4, top_ + d.y + 2, std::max(60.0, right - left - 8), 20}));
                }
            }
        }
    }

    void emitLifelines()
    {
        for (std::size_t i = 0; i < participants_.size(); ++i) {
            const Participant& p = participants_[i];
            std::string style = kLifelineStyle;
            style += "size=" + std::to_string(static_cast<int>(p.actor ? kActorSize : kHeadSize)) + ";";
            if (p.actor)
                style += "participant=umlActor;verticalLabelPosition=top;verticalAlign=bottom;";
            cells_.push_back(Cell::makeVertex(participantId(static_cast<int>(i)), "", p.label, style, {p.cx - p.cell / 2, 0, p.cell, bottom_}));
        }
    }

    void emitBars()
    {
        for (const Bar& bar : bars_) {
            const Participant& p = participants_[static_cast<std::size_t>(bar.participant)];
            cells_.push_back(Cell::makeVertex("b" + std::to_string(++count_), "", "", kBarStyle,
                                              {p.cx + bar.level * kBarStep - kBarWidth / 2, top_ + bar.top, kBarWidth, bar.bottom - bar.top}));
        }
    }

    void emitMessages()
    {
        for (const Message& m : messages_) {
            const Participant& from = participants_[static_cast<std::size_t>(m.from)];
            const Participant& to = participants_[static_cast<std::size_t>(m.to)];
            const bool self = m.from == m.to;
            const bool rightwards = to.cx >= from.cx;
            const double y = top_ + m.y; // page coordinates
            std::string style = "html=1;verticalAlign=bottom;edgeStyle=none;curved=0;rounded=0;";
            switch (m.arrow.end) {
            case Arrow::Head::Block: style += "endArrow=block;endFill=1;"; break;
            case Arrow::Head::Cross: style += "endArrow=cross;endSize=10;"; break;
            case Arrow::Head::Open: style += "endArrow=open;endSize=8;"; break;
            case Arrow::Head::None: style += "endArrow=none;"; break;
            }
            if (m.arrow.both)
                style += "startArrow=block;startFill=1;";
            if (m.arrow.dashed)
                style += "dashed=1;";
            const double exitDx = self ? barEdge(m.fromDepth, true) : barEdge(m.fromDepth, rightwards);
            const double entryDx = self ? barEdge(m.toDepth, true) : barEdge(m.toDepth, !rightwards);
            const double entryY = self ? m.y + 20 : m.y;
            style += "exitX=0.5;exitY=0;exitDx=" + number(exitDx) + ";exitDy=" + number(m.y + top_) + ";exitPerimeter=0;";
            style += "entryX=0.5;entryY=0;entryDx=" + number(entryDx) + ";entryDy=" + number(entryY + top_) + ";entryPerimeter=0;";
            if (self)
                style += "align=left;spacingLeft=2;";
            Cell edge = Cell::makeEdge("m" + std::to_string(++count_), "", m.label, style, participantId(m.from), participantId(m.to));
            if (self) {
                const double x = from.cx + 30 + std::max(0.0, barEdge(m.fromDepth, true));
                edge.geometry()->setPoints({{x, y}, {x, y + 20}});
            }
            cells_.push_back(std::move(edge));
        }
    }

    void emitNotes()
    {
        for (const NoteEvent& n : notes_) {
            const Participant& first = participants_[static_cast<std::size_t>(n.first)];
            const Participant& last = participants_[static_cast<std::size_t>(n.last)];
            double x = 0;
            double width = n.width;
            switch (n.place) {
            case Place::Left: x = first.cx - width - 15; break;
            case Place::Right: x = first.cx + 15; break;
            case Place::Over:
                width = std::max(width, last.cx - first.cx + 60);
                x = (first.cx + last.cx) / 2 - width / 2;
                break;
            }
            cells_.push_back(Cell::makeVertex("n" + std::to_string(++count_), "", n.label, kNoteStyle, {x, top_ + n.y, width, n.height}));
        }
    }

    static std::string number(double value)
    {
        std::string text = std::to_string(std::round(value * 100) / 100);
        while (text.find('.') != std::string::npos && (text.back() == '0' || text.back() == '.')) {
            const bool dot = text.back() == '.';
            text.pop_back();
            if (dot)
                break;
        }
        return text;
    }

    void emit()
    {
        emitFrames();
        emitLifelines();
        emitBars();
        emitMessages();
        emitNotes();
        moveToOrigin(cells_);
        result_.cells = std::move(cells_);
    }

    const std::vector<Statement>& statements_;
    std::size_t first_;
    Options options_;
    std::vector<Participant> participants_;
    std::vector<Message> messages_;
    std::vector<NoteEvent> notes_;
    std::vector<Bar> bars_;
    std::vector<Frame> frames_;
    std::vector<std::size_t> open_; // frames_ that are open
    std::vector<ColumnNeed> spacings_;
    std::vector<Cell> cells_;
    double y_ = 0;     // the next row, below the heads
    double lastY_ = 0; // the arrow of the last message
    double top_ = 0;   // the page y of row 0
    double bottom_ = 0;
    bool autonumber_ = false;
    int number_ = 0;
    int count_ = 0;
    Result result_;
};

} // namespace

Result importMermaidSequence(const std::vector<Statement>& statements, std::size_t first, const Options& options)
{
    return Importer(statements, first, options).run();
}

} // namespace qraw::commands::imports::detail
