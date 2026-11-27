#pragma once

// Internal to qraw_commands: the pieces of the Mermaid importer
// (import_mermaid.cpp reads the file and picks the diagram type,
// import_mermaid_flow.cpp and import_mermaid_sequence.cpp import one type each).

#include "import_support.h"

#include <string>
#include <vector>

namespace qraw::commands::imports::detail {

// One statement of a Mermaid source: a line without its comment lines and
// leading white space, or the part of a line between `;` separators.
struct Statement {
    int line = 0;   // 1-based
    int column = 0; // 1-based column of text[0] in its line
    std::string text;
};

// Mermaid text as HTML for a draw.io label: the quotes around it are dropped,
// `#quot;` style entity codes are decoded, <br> becomes a line break and
// everything else is escaped.
[[nodiscard]] std::string labelHtml(std::string_view mermaidText);

// A diagnostic at `statement.column + offset` (offset: 0-based characters into the statement).
[[nodiscard]] Diagnostic at(const Statement& statement, std::size_t offset, std::string message);

// `statements[first..]` of a flowchart whose header is statements[first - 1]
// (`graph TD`, `flowchart LR`); `direction` is the word after the keyword, or "".
[[nodiscard]] Result importMermaidFlowchart(const std::vector<Statement>& statements, std::size_t first,
                                            const std::string& direction, const Options& options);

// `statements[first..]` of a sequence diagram.
[[nodiscard]] Result importMermaidSequence(const std::vector<Statement>& statements, std::size_t first,
                                           const Options& options);

} // namespace qraw::commands::imports::detail
