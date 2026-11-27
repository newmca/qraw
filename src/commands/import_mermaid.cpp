// Insert > Advanced > Mermaid: the file level. draw.io renders Mermaid with
// the mermaid JavaScript library; this is our own reader of a documented subset
// (COMPATIBILITY.md): flowcharts in import_mermaid_flow.cpp and sequence
// diagrams in import_mermaid_sequence.cpp.
#include "import_mermaid.h"

#include <algorithm>

namespace qraw::commands::imports {

namespace detail {

Diagnostic at(const Statement& statement, std::size_t offset, std::string message)
{
    return {statement.line, statement.column + static_cast<int>(offset), std::move(message)};
}

namespace {

// Entity codes of Mermaid text: #quot; #35; and the HTML names that matter.
std::string decodeEntities(std::string_view text)
{
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '#') {
            const std::size_t semi = text.find(';', i);
            if (semi != std::string_view::npos && semi > i + 1 && semi - i <= 9) {
                const std::string_view name = text.substr(i + 1, semi - i - 1);
                std::string decoded;
                if (name == "quot")
                    decoded = "\"";
                else if (name == "amp")
                    decoded = "&";
                else if (name == "lt")
                    decoded = "<";
                else if (name == "gt")
                    decoded = ">";
                else if (name == "nbsp")
                    decoded = " ";
                else if (std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; })) {
                    // The numeric code goes through the HTML reader of plainText.
                    decoded = plainText("&#" + std::string(name) + ";");
                }
                if (!decoded.empty()) {
                    out += decoded;
                    i = semi;
                    continue;
                }
            }
        }
        out += text[i];
    }
    return out;
}

} // namespace

std::string labelHtml(std::string_view mermaidText)
{
    std::string_view text = trim(mermaidText);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
        text = trim(text.substr(1, text.size() - 2));
    if (text.size() >= 2 && text.front() == '`' && text.back() == '`')
        text = trim(text.substr(1, text.size() - 2)); // a markdown string: shown as text
    const std::string decoded = decodeEntities(text);

    // <br>, <br/> and <br /> are line breaks; the rest of the text is escaped.
    std::string out;
    std::size_t start = 0;
    const std::string lowered = lower(decoded);
    for (std::size_t i = 0; i < decoded.size();) {
        std::size_t length = 0;
        if (lowered.compare(i, 4, "<br>") == 0)
            length = 4;
        else if (lowered.compare(i, 5, "<br/>") == 0)
            length = 5;
        else if (lowered.compare(i, 6, "<br />") == 0)
            length = 6;
        if (length == 0) {
            ++i;
            continue;
        }
        out += escapeHtml(std::string_view(decoded).substr(start, i - start)) + "<br>";
        i += length;
        start = i;
    }
    out += escapeHtml(std::string_view(decoded).substr(start));
    return out;
}

} // namespace detail

namespace {

using detail::Statement;

// The statements of the source: lines without comments (a line that starts
// with %%) and the parts between `;` outside double quotes. A front matter
// block (--- ... ---) at the start is skipped with a warning.
std::vector<Statement> statementsOf(std::string_view text, std::vector<Diagnostic>& warnings)
{
    std::vector<Statement> statements;
    const std::vector<std::string_view> lines = detail::splitLines(text);
    std::size_t i = 0;
    while (i < lines.size() && detail::trim(lines[i]).empty())
        ++i;
    if (i < lines.size() && detail::trim(lines[i]) == "---") {
        std::size_t close = i + 1;
        while (close < lines.size() && detail::trim(lines[close]) != "---")
            ++close;
        if (close < lines.size()) {
            warnings.push_back({static_cast<int>(i) + 1, 0, "The front matter (--- ... ---) is ignored."});
            i = close + 1;
        }
    }
    for (; i < lines.size(); ++i) {
        const std::string_view line = lines[i];
        std::size_t lead = 0;
        while (lead < line.size() && (line[lead] == ' ' || line[lead] == '\t'))
            ++lead;
        const std::string_view body = line.substr(lead);
        if (body.empty())
            continue;
        if (detail::startsWith(body, "%%")) {
            if (detail::startsWith(body, "%%{"))
                warnings.push_back({static_cast<int>(i) + 1, 0, "The Mermaid directive %%{...}%% is ignored."});
            continue;
        }
        std::size_t start = 0;
        bool quoted = false;
        for (std::size_t k = 0; k <= body.size(); ++k) {
            if (k < body.size() && body[k] == '"')
                quoted = !quoted;
            if (k < body.size() && (body[k] != ';' || quoted))
                continue;
            std::string_view part = body.substr(start, k - start);
            std::size_t partLead = 0;
            while (partLead < part.size() && (part[partLead] == ' ' || part[partLead] == '\t'))
                ++partLead;
            const std::string_view trimmed = detail::trim(part);
            if (!trimmed.empty())
                statements.push_back({static_cast<int>(i) + 1, static_cast<int>(lead + start + partLead) + 1, std::string(trimmed)});
            start = k + 1;
        }
    }
    return statements;
}

} // namespace

Result importMermaid(std::string_view text, const Options& options)
{
    Result result;
    const std::vector<Statement> statements = statementsOf(text, result.warnings);
    if (statements.empty()) {
        result.errors.push_back({0, 0, "There is nothing to import: the text has no Mermaid diagram."});
        return result;
    }
    const Statement& header = statements.front();
    // The first word decides the type: "graph TD", "flowchart LR", "sequenceDiagram".
    std::size_t end = 0;
    while (end < header.text.size() && header.text[end] != ' ' && header.text[end] != '\t')
        ++end;
    const std::string type = header.text.substr(0, end);
    const std::string rest(detail::trim(std::string_view(header.text).substr(end)));

    Result imported;
    if (type == "graph" || type == "flowchart" || type == "flowchart-elk") {
        imported = detail::importMermaidFlowchart(statements, 1, rest, options);
    } else if (type == "sequenceDiagram") {
        if (!rest.empty()) {
            result.errors.push_back(detail::at(header, end, "Unexpected text after sequenceDiagram."));
            return result;
        }
        imported = detail::importMermaidSequence(statements, 1, options);
    } else {
        result.errors.push_back(detail::at(header, 0,
                                           "The Mermaid diagram type '" + type + "' is not supported. Supported types: flowchart (graph) and sequenceDiagram."));
        return result;
    }
    // The front matter and directive warnings come first.
    imported.warnings.insert(imported.warnings.begin(), result.warnings.begin(), result.warnings.end());
    return imported;
}

} // namespace qraw::commands::imports
