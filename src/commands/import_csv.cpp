// Insert > Advanced > CSV. Port of EditorUi.prototype.doImportCsv
// (js/diagramly/EditorUi.js:37190, draw.io, Apache-2.0) with Editor.csvToArray
// (js/diagramly/Editor.js:5597) and Graph.replacePlaceholders
// (js/grapheditor/Graph.js:16300), modified:
//  * the result is a list of detached cells (import_diagram.h) instead of an
//    edit of the live graph, so the cells keep ids of their own and rows are
//    never matched with existing cells (no update by `identity`, no data
//    sources);
//  * label sizes come from the caller's measurement, layouts are ours
//    (layout_graph.h) instead of ELK, and the grid snap is the insertion's;
//  * not supported: `container`, `list`, JSON layouts, libavoid and parallels
//    (each gives a warning), and stylePlaceholders=1 (styles are resolved once);
//  * a quoted field that is not closed is an error with its line number.
#include "import_support.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <unordered_map>

namespace qraw::commands::imports {

namespace {

using core::Cell;
using detail::Arrangement;
using detail::Json;
using detail::trim;

constexpr char kDefaultStyle[] = "whiteSpace=wrap;html=1;";
// Graph.createCurrentEdgeStyle with the stock current edge style of draw.io.
constexpr char kDefaultEdgeStyle[] = "edgeStyle=orthogonalEdgeStyle;rounded=0;orthogonalLoop=1;jettySize=auto;html=1;";

using Attributes = std::vector<std::pair<std::string, std::string>>; // in column order

const std::string* findAttribute(const Attributes& attributes, std::string_view name)
{
    for (const auto& [key, value] : attributes) {
        if (key == name)
            return &value;
    }
    return nullptr;
}

// parseFloat: the number at the start of the text (after white space).
std::optional<double> parseNumber(std::string_view text)
{
    const std::string copy(trim(text));
    if (copy.empty())
        return std::nullopt;
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (end == copy.c_str() || !std::isfinite(value))
        return std::nullopt;
    return value;
}

// --- placeholders -----------------------------------------------------------------------

// The end (index of the closing %) of the placeholder that starts at text[start] == '%'
// (Graph.prototype.placeholderPattern), or npos.
std::size_t placeholderEnd(std::string_view text, std::size_t start)
{
    // date{...}
    if (text.compare(start + 1, 5, "date{") == 0) {
        std::size_t i = start + 6;
        while (i < text.size() && text[i] != '{' && text[i] != '}' && text[i] != '\n' && text[i] != '\r')
            ++i;
        if (i + 1 < text.size() && text[i] == '}' && text[i + 1] == '%')
            return i + 1;
    }
    std::size_t i = start + 1;
    while (i < text.size() && std::string_view("%{}\"'=;").find(text[i]) == std::string_view::npos)
        ++i;
    return i < text.size() && text[i] == '%' && i > start + 1 ? i : std::string_view::npos;
}

// Graph.replacePlaceholders without the global variables: `lookup` gives the
// value of a name (a property of the cell, then the `vars` of the header).
template <class Lookup>
std::string replacePlaceholders(std::string_view text, const Lookup& lookup)
{
    std::string result;
    std::size_t last = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '%') {
            ++i;
            continue;
        }
        const std::size_t end = placeholderEnd(text, i);
        if (end == std::string_view::npos) {
            ++i;
            continue;
        }
        const std::string_view match = text.substr(i, end - i + 1);
        const std::size_t index = i;
        i = end + 1;
        if (match.size() <= 2 || match == "%label%" || match == "%tooltip%")
            continue;
        std::optional<std::string> value;
        if (index > last && text[index - 1] == '%') {
            value = std::string(match.substr(1)); // "%%name%": the first % is literal
        } else {
            const std::string_view name = match.substr(1, match.size() - 2);
            if (name.find('{') == std::string_view::npos)
                value = lookup(name);
        }
        result.append(text.substr(last, index - last));
        result += value ? *value : std::string(match);
        last = index + match.size();
    }
    result.append(text.substr(last));
    return result;
}

// --- Editor.csvToArray ----------------------------------------------------------------

// RFC 4180 fields of one line. `open` is set when a quoted field is not closed.
std::vector<std::string> csvToArray(std::string_view text, bool& open)
{
    open = false;
    if (text.empty())
        return {};
    std::vector<std::string> row(1);
    bool outside = true;
    char previous = '\0';
    for (char c : text) {
        if (c == '"') {
            if (outside && c == previous)
                row.back() += c;
            outside = !outside;
        } else if (c == ',' && outside) {
            row.emplace_back();
            c = '\0';
        } else {
            row.back() += c;
        }
        previous = c;
    }
    open = !outside;
    return row;
}

// The name of a column as an XML attribute (doImportCsv).
std::string attributeName(std::string_view column)
{
    std::string name;
    for (const char c : trim(column)) {
        const bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (alnum)
            name += c;
        else if (name.empty() || name.back() != '_')
            name += '_';
    }
    std::size_t digits = 0;
    while (digits < name.size() && name[digits] >= '0' && name[digits] <= '9')
        ++digits;
    name.erase(0, digits);
    while (!name.empty() && name.back() == '_')
        name.pop_back();
    return name;
}

// --- the header -------------------------------------------------------------------------

struct EdgeLabel {
    std::string label;
    bool hasLabel = false;
    double x = 0;
    double y = 0;
    std::optional<double> dx;
    std::optional<double> dy;
    std::string placeholders;
};

struct Connection {
    std::string from;
    std::string to;
    bool invert = false;
    std::string label;
    std::string fromlabel;
    std::string tolabel;
    std::string sourcelabel;
    std::string targetlabel;
    std::string style;
    bool hasStyle = false;
    std::string placeholders;
    std::vector<EdgeLabel> labels;
    std::vector<std::pair<std::string, std::string>> data;
    bool hasFromlabel = false, hasTolabel = false, hasSourcelabel = false, hasTargetlabel = false, hasLabel = false;
};

using StringMap = std::vector<std::pair<std::string, std::string>>;

const std::string* findIn(const StringMap& map, const std::string& key)
{
    for (const auto& [name, value] : map) {
        if (name == key)
            return &value;
    }
    return nullptr;
}

struct Config {
    std::optional<std::string> label;
    std::string labelname;
    StringMap labels;
    std::string style;
    std::string parentstyle = kDefaultStyle;
    std::optional<std::string> unknownStyle;
    std::string stylename;
    StringMap styles;
    StringMap vars;
    std::string identity;
    std::string parent;
    std::string namespacePrefix;
    std::string width = "auto";
    std::string height = "auto";
    bool collapsed = false;
    std::string left;
    std::string top;
    std::vector<std::string> ignore;
    std::vector<Connection> connections;
    std::string link;
    double padding = 0;
    double edgespacing = 40;
    double nodespacing = 40;
    double levelspacing = 100;
    std::string layout = "auto";
};

// The members of a JSON object as strings (numbers and booleans as text); other values are skipped.
StringMap stringMap(const Json& json)
{
    StringMap map;
    for (const auto& [key, value] : json.object) {
        if (value.type == Json::Type::String || value.type == Json::Type::Number || value.type == Json::Type::Bool)
            map.emplace_back(key, value.text());
    }
    return map;
}

std::string member(const Json& json, std::string_view key, bool* present = nullptr)
{
    const Json* found = json.find(key);
    if (present != nullptr)
        *present = found != nullptr && found->type != Json::Type::Null;
    return found != nullptr ? found->text() : std::string();
}

Connection connectionFrom(const Json& json)
{
    Connection c;
    c.from = member(json, "from");
    c.to = member(json, "to");
    if (const Json* invert = json.find("invert"))
        c.invert = invert->type == Json::Type::Bool ? invert->boolean : invert->type == Json::Type::String && invert->string == "true";
    c.label = member(json, "label", &c.hasLabel);
    c.fromlabel = member(json, "fromlabel", &c.hasFromlabel);
    c.tolabel = member(json, "tolabel", &c.hasTolabel);
    c.sourcelabel = member(json, "sourcelabel", &c.hasSourcelabel);
    c.targetlabel = member(json, "targetlabel", &c.hasTargetlabel);
    c.style = member(json, "style", &c.hasStyle);
    c.placeholders = member(json, "placeholders");
    if (const Json* labels = json.find("labels"); labels != nullptr && labels->type == Json::Type::Array) {
        for (const Json& entry : labels->array) {
            if (entry.type != Json::Type::Object)
                continue;
            EdgeLabel label;
            label.label = member(entry, "label", &label.hasLabel);
            label.x = parseNumber(member(entry, "x")).value_or(0);
            label.y = parseNumber(member(entry, "y")).value_or(0);
            if (entry.find("dx") != nullptr)
                label.dx = parseNumber(member(entry, "dx")).value_or(0);
            if (entry.find("dy") != nullptr)
                label.dy = parseNumber(member(entry, "dy")).value_or(0);
            label.placeholders = member(entry, "placeholders");
            c.labels.push_back(std::move(label));
        }
    }
    if (const Json* data = json.find("data"); data != nullptr && data->type == Json::Type::Object)
        c.data = stringMap(*data);
    return c;
}

std::vector<std::string> splitCommas(std::string_view text)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t comma = text.find(',', start);
        parts.emplace_back(text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
        if (comma == std::string_view::npos)
            break;
        start = comma + 1;
    }
    return parts;
}

// The force-directed layout compares every pair of shapes on every iteration.
constexpr std::size_t kMaxOrganic = 500;

class Importer {
public:
    Importer(std::string_view text, const Options& options) : options_(options), lines_(detail::splitLines(text)) {}

    Result run()
    {
        if (!readHeader() || !readColumns() || !readRows())
            return fail();
        buildCells();
        buildConnections();
        finish();
        return std::move(result_);
    }

private:
    struct Row {
        std::vector<std::string> values;
        int line = 0;
    };

    Result fail()
    {
        Result result;
        result.errors = std::move(result_.errors);
        result.warnings = std::move(result_.warnings);
        return result;
    }

    void error(int line, std::string message) { result_.errors.push_back({line, 0, std::move(message)}); }
    void warn(int line, std::string message) { result_.warnings.push_back({line, 0, std::move(message)}); }

    // --- header ---

    bool json(const std::string& key, const std::string& value, int line, Json& out)
    {
        std::string problem;
        if (detail::parseJson(value, out, problem))
            return true;
        error(line, "The value of '# " + key + "' is not valid JSON: " + problem + ".");
        return false;
    }

    // Returns false when the directive's value is invalid (an error was added).
    bool directive(const std::string& key, const std::string& value, int line)
    {
        const bool set = !value.empty() && value != "-";
        Json parsed;
        if (key == "label") {
            config_.label = value;
        } else if (key == "labelname") {
            if (set)
                config_.labelname = value;
        } else if (key == "labels") {
            if (set) {
                if (!json(key, value, line, parsed))
                    return false;
                config_.labels = stringMap(parsed);
            }
        } else if (key == "style") {
            if (set)
                config_.style = value;
        } else if (key == "parentstyle") {
            config_.parentstyle = value;
        } else if (key == "unknownStyle") {
            if (value != "-")
                config_.unknownStyle = value;
        } else if (key == "stylename") {
            if (set)
                config_.stylename = value;
        } else if (key == "styles") {
            if (set) {
                if (!json(key, value, line, parsed))
                    return false;
                config_.styles = stringMap(parsed);
            }
        } else if (key == "vars") {
            if (set) {
                if (!json(key, value, line, parsed))
                    return false;
                config_.vars = stringMap(parsed);
            }
        } else if (key == "identity") {
            if (set)
                config_.identity = value;
        } else if (key == "parent") {
            if (set)
                config_.parent = value;
        } else if (key == "namespace") {
            if (set)
                config_.namespacePrefix = value;
        } else if (key == "width") {
            config_.width = value;
        } else if (key == "height") {
            config_.height = value;
        } else if (key == "collapsed") {
            if (value != "-")
                config_.collapsed = value == "true";
        } else if (key == "left") {
            config_.left = value;
        } else if (key == "top") {
            config_.top = value;
        } else if (key == "ignore") {
            config_.ignore = splitCommas(value);
        } else if (key == "connect") {
            if (!json(key, value, line, parsed))
                return false;
            if (parsed.type != Json::Type::Object) {
                error(line, "The value of '# connect' must be a JSON object with \"from\" and \"to\".");
                return false;
            }
            config_.connections.push_back(connectionFrom(parsed));
        } else if (key == "link") {
            config_.link = value;
        } else if (key == "padding" || key == "edgespacing" || key == "nodespacing" || key == "levelspacing") {
            const std::optional<double> number = parseNumber(value);
            if (!number) {
                warn(line, "'# " + key + "' is not a number; the default is used.");
            } else if (key == "padding") {
                config_.padding = *number;
            } else if (key == "edgespacing") {
                config_.edgespacing = *number;
            } else if (key == "nodespacing") {
                config_.nodespacing = *number;
            } else {
                config_.levelspacing = *number;
            }
        } else if (key == "layout") {
            config_.layout = value;
        } else if (key == "container") {
            if (value != "-" && value != "false" && !value.empty())
                warn(line, "'# container' is not supported: the cells are arranged once and not kept in a layout container.");
        } else if (key == "list") {
            if (set)
                warn(line, "'# list' is not supported: the column is kept as a property.");
        } else {
            warn(line, "The directive '# " + key + "' is not known and is ignored.");
        }
        return true;
    }

    bool readHeader()
    {
        // Processing instructions first (lines that start with #), a trailing
        // backslash continues the instruction on the next # line.
        while (index_ < lines_.size() && !lines_[index_].empty() && lines_[index_][0] == '#') {
            const int line = static_cast<int>(index_) + 1;
            std::string text(lines_[index_]);
            ++index_;
            while (index_ < lines_.size() && !text.empty() && text.back() == '\\' && !lines_[index_].empty() &&
                   lines_[index_][0] == '#') {
                text.pop_back();
                text += std::string(trim(lines_[index_].substr(1)));
                ++index_;
            }
            if (text.size() > 1 && text[1] == '#')
                continue; // a comment
            const std::size_t colon = text.find(':');
            if (colon == std::string::npos || colon == 0)
                continue;
            if (!directive(std::string(trim(std::string_view(text).substr(1, colon - 1))),
                           std::string(trim(std::string_view(text).substr(colon + 1))), line))
                return false;
        }
        if (index_ >= lines_.size() || trim(lines_[index_]).empty()) {
            error(static_cast<int>(std::min(index_ + 1, lines_.size())),
                  "The CSV has no row of column names after the lines that start with #.");
            return false;
        }
        return true;
    }

    bool readColumns()
    {
        const int line = static_cast<int>(index_) + 1;
        bool open = false;
        columns_ = csvToArray(lines_[index_], open);
        if (open) {
            error(line, "A quoted column name is not closed.");
            return false;
        }
        for (std::size_t i = 0; i < columns_.size(); ++i) {
            if (columns_[i] == config_.identity && !config_.identity.empty() && !identityIndex_)
                identityIndex_ = i;
            if (columns_[i] == config_.parent && !config_.parent.empty() && !parentIndex_)
                parentIndex_ = i;
            attributes_.push_back(attributeName(columns_[i]));
            if (attributes_.back().empty())
                warn(line, "The column '" + columns_[i] + "' has no name that can be a property and is ignored.");
        }
        if (!config_.identity.empty() && !identityIndex_)
            warn(line, "The identity column '" + config_.identity + "' does not exist.");
        if (!config_.parent.empty() && !parentIndex_)
            warn(line, "The parent column '" + config_.parent + "' does not exist.");
        if (!attributes_.empty() && attributes_.front().empty()) {
            error(line, "The first column has no name that can be used in a label.");
            return false;
        }
        if (!config_.label)
            config_.label = "%" + attributes_.front() + "%";
        ++index_;
        return true;
    }

    bool readRows()
    {
        for (; index_ < lines_.size(); ++index_) {
            const int line = static_cast<int>(index_) + 1;
            bool open = false;
            std::vector<std::string> values = csvToArray(lines_[index_], open);
            if (open) {
                std::string shortened(lines_[index_].substr(0, 40));
                error(line, "A quoted value is not closed in '" + shortened + (lines_[index_].size() > 40 ? "..." : "") + "'.");
                return false;
            }
            if (values.empty())
                continue;
            if (values.size() > columns_.size()) {
                warn(line, "The row has more values than the " + std::to_string(columns_.size()) + " columns; the extra values are ignored.");
                values.resize(columns_.size());
            }
            if (rows_.size() >= detail::kMaxVertices) {
                error(line, "The CSV has more than " + std::to_string(detail::kMaxVertices) + " rows.");
                return false;
            }
            rows_.push_back({std::move(values), line});
        }
        if (rows_.empty()) {
            error(static_cast<int>(lines_.size()), "The CSV has the column names but no rows.");
            return false;
        }
        return true;
    }

    // --- cells ---

    // The properties of a row as the placeholders see them: the columns, with
    // the label that `labelname` and `labels` choose.
    Attributes attributesOf(const Row& row) const
    {
        Attributes attributes;
        for (std::size_t j = 0; j < row.values.size() && j < attributes_.size(); ++j) {
            if (!attributes_[j].empty())
                setAttribute(attributes, attributes_[j], row.values[j]);
        }
        if (!config_.labelname.empty() && !config_.labels.empty()) {
            const std::string* key = findAttribute(attributes, config_.labelname);
            const std::string* named = key != nullptr ? findIn(config_.labels, *key) : nullptr;
            if (named != nullptr)
                setAttribute(attributes, "label", *named);
        }
        return attributes;
    }

    static void setAttribute(Attributes& attributes, const std::string& name, const std::string& value)
    {
        for (auto& [key, current] : attributes) {
            if (key == name) {
                current = value;
                return;
            }
        }
        attributes.emplace_back(name, value);
    }

    std::string resolve(std::string_view text, const Attributes& attributes, const std::string& id) const
    {
        return replacePlaceholders(text, [&](std::string_view name) -> std::optional<std::string> {
            if (name == "id")
                return id.empty() ? std::nullopt : std::optional<std::string>(id);
            if (const std::string* value = findAttribute(attributes, name))
                return *value;
            if (const std::string* value = findIn(config_.vars, std::string(name)))
                return *value;
            return std::nullopt;
        });
    }

    std::string resolveStyle(const std::string& style, const Attributes& attributes, const std::string& id)
    {
        if (!warnedStylePlaceholders_ && style.find("stylePlaceholders=1") != std::string::npos) {
            warnedStylePlaceholders_ = true;
            warn(0, "stylePlaceholders=1 is not supported: the placeholders of a style are replaced once, at the import.");
        }
        return resolve(style, attributes, id);
    }

    // getRowStyle: the style of the row, or the one `styles` names for its `stylename` column.
    std::string rowStyle(const Attributes& attributes, const std::string& id)
    {
        std::string style = config_.style.empty() ? std::string(kDefaultStyle) : config_.style;
        if (!config_.stylename.empty() && !config_.styles.empty()) {
            const std::string* key = findAttribute(attributes, config_.stylename);
            const std::string* named = key != nullptr ? findIn(config_.styles, *key) : nullptr;
            if (named != nullptr)
                style = *named;
        }
        return resolveStyle(style, attributes, id);
    }

    // The width or height of a cell: a number, `auto` (the label's size plus padding), `width`
    // (heights only) or `@column`, the column holding a number or `auto`.
    double dimension(const std::string& directive, const Attributes& attributes, double natural, double width) const
    {
        std::string value = directive;
        if (!value.empty() && value[0] == '@') {
            const std::string* column = findAttribute(attributes, value.substr(1));
            if (column == nullptr)
                return natural + config_.padding;
            if (*column != "auto")
                return std::max(0.0, parseNumber(*column).value_or(0));
            value = "auto";
        }
        if (value == "auto")
            return natural + config_.padding;
        if (value == "width" && width >= 0)
            return width;
        return parseNumber(value).value_or(natural + config_.padding);
    }

    bool isAuto(const std::string& directive, const Attributes& attributes) const
    {
        if (directive == "auto")
            return true;
        if (!directive.empty() && directive[0] == '@') {
            const std::string* column = findAttribute(attributes, directive.substr(1));
            return column != nullptr && *column == "auto";
        }
        return false;
    }

    std::optional<double> columnNumber(const std::string& column, const Attributes& attributes) const
    {
        if (column.empty())
            return std::nullopt;
        const std::string* value = findAttribute(attributes, column);
        return value != nullptr ? parseNumber(*value) : std::nullopt;
    }

    void buildCells()
    {
        double y = 0;
        std::unordered_map<std::string, std::size_t> byIdentity; // namespace + identity value -> node
        for (std::size_t r = 0; r < rows_.size(); ++r) {
            const Row& row = rows_[r];
            std::string identity;
            if (identityIndex_ && *identityIndex_ < row.values.size())
                identity = config_.namespacePrefix + row.values[*identityIndex_];
            Attributes attributes = attributesOf(row);
            if (identityIndex_) {
                const auto existing = byIdentity.find(identity);
                if (existing != byIdentity.end()) {
                    // The same identity again: no new cell, but the row's connections start at the first one.
                    nodes_[existing->second].duplicates.push_back(std::move(attributes));
                    continue;
                }
            }

            const std::string* ownLabel = findAttribute(attributes, "label");
            const std::string label = ownLabel != nullptr ? *ownLabel : *config_.label;
            const std::string style = rowStyle(attributes, identity);
            const core::Point natural = detail::measureLabel(options_, style, resolve(label, attributes, identity));
            const std::size_t node = nodes_.size();

            // The parent: an earlier row, by its identity or (without an identity column) its 1-based number.
            std::optional<std::size_t> parent;
            if (parentIndex_ && *parentIndex_ < row.values.size()) {
                const std::string& reference = row.values[*parentIndex_];
                const std::string key = identityIndex_ ? config_.namespacePrefix + reference : reference;
                const auto& source = identityIndex_ ? byIdentity : byRow_;
                if (const auto found = source.find(key); found != source.end())
                    parent = found->second;
            }

            const double width = dimension(config_.width, attributes, natural.x, -1);
            const double height = dimension(config_.height, attributes, natural.y, width);
            core::Rect bounds{0, y, width, height};
            if (const auto left = columnNumber(config_.left, attributes))
                bounds.x = *left;
            if (const auto top = columnNumber(config_.top, attributes))
                bounds.y = *top;

            Cell cell = Cell::makeVertex("c" + std::to_string(node + 1), "", "", style, bounds);
            cell.setCollapsed(config_.collapsed);
            fillValue(cell, attributes, label);
            if (parent) {
                cell.setParent(vertices_[*parent].id());
                if (std::find(parents_.begin(), parents_.end(), *parent) == parents_.end())
                    parents_.push_back(*parent);
            } else {
                topLevel_.push_back(node);
            }
            y += height + config_.nodespacing;

            nodes_.push_back({std::move(attributes), {}, identity, row.line});
            vertices_.push_back(std::move(cell));
            byRow_.emplace(std::to_string(r + 1), node);
            if (identityIndex_)
                byIdentity.emplace(identity, node);
            for (const Connection& connection : config_.connections) {
                if (const std::string* value = findAttribute(nodes_[node].attributes, connection.to))
                    lookups_[connection.to][*value] = node;
            }
        }
        rowNodes_ = nodes_.size();
        fitParents();
    }

    // The cell's value: a UserObject with the label template, the columns, placeholders=1 and the link.
    void fillValue(Cell& cell, const Attributes& attributes, const std::string& label) const
    {
        cell.ensureWrapper("UserObject");
        cell.setValue(label);
        for (const auto& [name, value] : attributes) {
            if (name == "id")
                continue; // the attribute `id` of an object is the cell's id
            if (name == "label") {
                cell.setValue(value); // a column called label replaces the template
                continue;
            }
            cell.setData(name, value);
        }
        cell.setData("placeholders", "1");
        if (!config_.link.empty() && config_.link != "link") {
            if (const std::string* value = findAttribute(attributes, config_.link)) {
                if (!value->empty())
                    cell.setData("link", *value);
                cell.wrapper()->attributes.remove(config_.link);
            }
        }
    }

    // The style of a parent (`parentstyle`) and, with automatic sizes, a size that holds its children
    // (mxGraph.updateGroupBounds(parent, padding, moveParent)).
    void fitParents()
    {
        for (const std::size_t p : parents_) {
            Cell& parent = vertices_[p];
            const std::string style = resolveStyle(config_.parentstyle, nodes_[p].attributes, nodes_[p].identity);
            parent.setStyleText(style);
            if (!isAuto(config_.width, nodes_[p].attributes) || !isAuto(config_.height, nodes_[p].attributes))
                continue;
            double left = 0, top = 0, right = 0, bottom = 0;
            std::vector<Cell*> children;
            for (Cell& cell : vertices_) {
                if (cell.parent() == parent.id() && cell.geometry() != nullptr && !cell.geometry()->relative())
                    children.push_back(&cell);
            }
            if (children.empty())
                continue;
            for (std::size_t i = 0; i < children.size(); ++i) {
                const core::Rect b = children[i]->geometry()->bounds();
                left = i == 0 ? b.x : std::min(left, b.x);
                top = i == 0 ? b.y : std::min(top, b.y);
                right = i == 0 ? b.x + b.width : std::max(right, b.x + b.width);
                bottom = i == 0 ? b.y + b.height : std::max(bottom, b.y + b.height);
            }
            if (right - left <= 0 || bottom - top <= 0)
                continue;
            // The strip of a swimlane's title.
            double startLeft = 0, startTop = 0;
            const core::Style parentStyle = parent.style();
            if (parentStyle.hasName("swimlane") || parentStyle.value("shape", "") == "swimlane") {
                const double size = parseNumber(parentStyle.value("startSize", "23")).value_or(23);
                (parentStyle.value("horizontal", "1") == "0" ? startLeft : startTop) = size;
            }
            const double border = config_.padding;
            core::Geometry& geometry = *parent.geometry();
            geometry.setX(std::round(geometry.x() + left - border - startLeft));
            geometry.setY(std::round(geometry.y() + top - border - startTop));
            geometry.setWidth(std::round(right - left + 2 * border + startLeft));
            geometry.setHeight(std::round(bottom - top + 2 * border + startTop));
            for (Cell* child : children) {
                child->geometry()->setX(child->geometry()->x() + border + startLeft - left);
                child->geometry()->setY(child->geometry()->y() + border + startTop - top);
            }
        }
    }

    // --- connections ---

    void buildConnections()
    {
        for (const Connection& connection : config_.connections) {
            for (std::size_t i = 0; i < rowNodes_; ++i) {
                connect(connection, i, nodes_[i].attributes);
                for (std::size_t d = 0; d < nodes_[i].duplicates.size(); ++d)
                    connect(connection, i, nodes_[i].duplicates[d]);
            }
        }
    }

    std::string connectionLabel(const Connection& c, const Attributes& data, const Attributes& reference, const std::string& dataId,
                                const std::string& referenceId) const
    {
        std::string label = c.label;
        const auto attribute = [](const Attributes& attributes, const std::string& name) {
            const std::string* value = findAttribute(attributes, name);
            return value != nullptr ? *value : std::string();
        };
        if (c.hasFromlabel)
            label = attribute(data, c.fromlabel) + label;
        if (c.hasSourcelabel)
            label = resolve(c.sourcelabel, data, dataId) + label;
        if (c.hasTolabel)
            label += attribute(reference, c.tolabel);
        if (c.hasTargetlabel)
            label += resolve(c.targetlabel, reference, referenceId);
        return label;
    }

    void connect(const Connection& c, std::size_t real, const Attributes& data)
    {
        const std::string* from = findAttribute(data, c.from);
        if (from == nullptr || from->empty())
            return;
        for (const std::string& name : splitCommas(*from)) {
            auto& lookup = lookups_[c.to];
            auto found = lookup.find(name);
            if (found == lookup.end() && config_.unknownStyle) {
                found = lookup.emplace(name, addUnknown(name, data, nodes_[real].identity)).first;
            }
            if (found == lookup.end())
                continue;
            const std::size_t ref = found->second;
            // Placeholders of the style come from the source end of the connection.
            const bool referenceIsSource = (c.placeholders == "target") == !c.invert;
            const std::size_t styled = referenceIsSource ? ref : real;
            const std::string style = c.hasStyle ? resolveStyle(c.style, nodes_[styled].attributes, nodes_[styled].identity)
                                                 : std::string(kDefaultEdgeStyle);
            const std::string label = connectionLabel(c, data, nodes_[ref].attributes, nodes_[real].identity, nodes_[ref].identity);
            const std::size_t source = c.invert ? ref : real;
            const std::size_t target = c.invert ? real : ref;
            ++edgeCount_;
            Cell edge = Cell::makeEdge("e" + std::to_string(edgeCount_), "", label, style, vertices_[source].id(), vertices_[target].id());
            if (!c.data.empty()) {
                edge.ensureWrapper("UserObject");
                for (const auto& [key, value] : c.data)
                    edge.setData(key, value);
            }
            const std::string edgeId = edge.id();
            edgeCells_.push_back(std::move(edge));
            for (std::size_t k = 0; k < c.labels.size(); ++k) {
                const EdgeLabel& def = c.labels[k];
                std::string text = def.hasLabel && !def.label.empty() ? def.label : std::to_string(k);
                if (!def.placeholders.empty()) {
                    const std::size_t owner = (def.placeholders == "target") == !c.invert ? ref : real;
                    text = resolve(text, nodes_[owner].attributes, nodes_[owner].identity);
                }
                Cell label_cell = Cell::makeVertex(edgeId + "l" + std::to_string(k), edgeId, text, "resizable=0;html=1;", {def.x, def.y, 0, 0});
                label_cell.setConnectable(false);
                label_cell.geometry()->setRelative(true);
                if (def.dx || def.dy)
                    label_cell.geometry()->setOffset(core::Point{def.dx.value_or(0), def.dy.value_or(0)});
                edgeCells_.push_back(std::move(label_cell));
            }
            targets_.push_back(c.invert ? real : ref);
        }
    }

    // A shape for a reference that no row has (`unknownStyle`).
    std::size_t addUnknown(const std::string& name, const Attributes& data, const std::string& dataId)
    {
        const std::string style = resolveStyle(*config_.unknownStyle, data, dataId);
        const std::string label = style.find("html=1") != std::string::npos ? detail::escapeHtml(name) : name;
        const core::Point size = detail::measureLabel(options_, style, label);
        const std::size_t node = nodes_.size();
        vertices_.push_back(Cell::makeVertex("c" + std::to_string(node + 1), "", label, style,
                                             {0, 0, size.x + config_.padding, size.y + config_.padding}));
        nodes_.push_back({{}, {}, std::string(), 0});
        topLevel_.push_back(node);
        return node;
    }

    // --- the end ---

    void finish()
    {
        for (std::size_t i = 0; i < rowNodes_; ++i) {
            for (const std::string& name : config_.ignore) {
                // The attribute `id` of an object is the cell's own id: it stays.
                if (vertices_[i].wrapper() != nullptr && trim(name) != "id")
                    vertices_[i].wrapper()->attributes.remove(trim(name));
            }
        }

        std::size_t roots = 0;
        for (const std::size_t node : topLevel_) {
            if (std::find(targets_.begin(), targets_.end(), node) == targets_.end())
                ++roots;
        }
        // Resolves `auto`: a tree for one root and as many connections as a tree has; otherwise a layered
        // layout for one root, a force-directed one when there are several roots and connections.
        std::string layout = detail::lower(trim(config_.layout));
        if (layout == "auto") {
            if (roots == 1)
                layout = edgeCount_ + 1 == topLevel_.size() ? "verticaltree" : "verticalflow";
            else
                layout = edgeCount_ > 0 && topLevel_.size() <= kMaxOrganic ? "organic" : edgeCount_ > 0 ? "verticalflow" : "none";
        }
        Arrangement arrangement = Arrangement::None;
        if ((layout == "organic" || layout == "elkorganic" || layout == "elkstress") && topLevel_.size() > kMaxOrganic) {
            warn(0, "The organic layout is limited to " + std::to_string(kMaxOrganic) + " shapes (it takes too long above that): a layered layout was used.");
            layout = "verticalflow";
        }
        if (layout == "verticaltree") {
            arrangement = Arrangement::VerticalTree;
        } else if (layout == "horizontaltree") {
            arrangement = Arrangement::HorizontalTree;
        } else if (layout == "verticalflow") {
            arrangement = Arrangement::VerticalFlow;
        } else if (layout == "horizontalflow") {
            arrangement = Arrangement::HorizontalFlow;
        } else if (layout == "organic" || layout == "elkorganic" || layout == "elkstress") {
            arrangement = Arrangement::Organic;
        } else if (layout == "circle") {
            arrangement = Arrangement::Circle;
        } else if (layout == "orgchart") {
            arrangement = Arrangement::OrgChart;
        } else if (layout == "elkradial") {
            arrangement = Arrangement::RadialTree;
        } else if (layout != "none") {
            const bool known = layout == "libavoid" || layout == "parallels";
            warn(0, (known ? "The layout '" + layout + "' is not supported" : "The layout '" + std::string(trim(config_.layout)) + "' is not known") +
                        ": the cells stay in a column.");
        }
        std::vector<Cell> cells = std::move(vertices_);
        for (Cell& cell : edgeCells_)
            cells.push_back(std::move(cell));
        detail::Spacing spacing;
        spacing.node = config_.nodespacing;
        spacing.level = config_.levelspacing;
        spacing.edge = config_.edgespacing;
        detail::arrange(cells, arrangement, spacing);
        result_.cells = std::move(cells);
    }

    struct Node {
        Attributes attributes;
        std::vector<Attributes> duplicates; // rows with the same identity
        std::string identity;               // namespace + the identity column's value
        int line = 0;
    };

    Options options_;
    std::vector<std::string_view> lines_;
    std::size_t index_ = 0;
    Config config_;
    std::vector<std::string> columns_;
    std::vector<std::string> attributes_;
    std::optional<std::size_t> identityIndex_;
    std::optional<std::size_t> parentIndex_;
    std::vector<Row> rows_;
    std::vector<Node> nodes_;               // parallel to vertices_: one per cell
    std::size_t rowNodes_ = 0;              // the nodes that rows made (the rest are unknown references)
    std::unordered_map<std::string, std::size_t> byRow_; // 1-based row number -> node
    std::vector<std::size_t> parents_;      // nodes with children, in order of their first child
    std::vector<std::size_t> topLevel_;     // nodes without a parent
    std::vector<std::size_t> targets_;      // nodes that a connection ends at
    std::size_t edgeCount_ = 0;
    std::map<std::string, std::unordered_map<std::string, std::size_t>> lookups_; // `to` column -> value -> node
    std::vector<Cell> vertices_;
    std::vector<Cell> edgeCells_;
    bool warnedStylePlaceholders_ = false;
    Result result_;
};

} // namespace

Result importCsv(std::string_view text, const Options& options)
{
    return Importer(text, options).run();
}

} // namespace qraw::commands::imports
