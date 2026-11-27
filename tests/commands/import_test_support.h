#pragma once
// Helpers for the importer tests (tst_import_*.cpp).

#include "qraw/commands/import_diagram.h"

#include <QString>
#include <QtTest>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace import_test {

namespace imports = qraw::commands::imports;
using qraw::core::Cell;
using qraw::core::Rect;

inline QString q(const std::string& text) { return QString::fromStdString(text); }

// The cell with `id` (nullptr when there is none).
inline const Cell* cellWithId(const imports::Result& result, const std::string& id)
{
    for (const Cell& cell : result.cells) {
        if (cell.id() == id)
            return &cell;
    }
    return nullptr;
}

// The first cell whose value is `value`.
inline const Cell* cellWithValue(const imports::Result& result, const std::string& value)
{
    for (const Cell& cell : result.cells) {
        if (cell.value() == value)
            return &cell;
    }
    return nullptr;
}

inline std::vector<const Cell*> vertices(const imports::Result& result)
{
    std::vector<const Cell*> found;
    for (const Cell& cell : result.cells) {
        if (cell.isVertex())
            found.push_back(&cell);
    }
    return found;
}

inline std::vector<const Cell*> edges(const imports::Result& result)
{
    std::vector<const Cell*> found;
    for (const Cell& cell : result.cells) {
        if (cell.isEdge())
            found.push_back(&cell);
    }
    return found;
}

// The edges from the cell with value `from` to the cell with value `to`.
inline std::vector<const Cell*> edgesBetween(const imports::Result& result, const std::string& from, const std::string& to)
{
    const Cell* a = cellWithValue(result, from);
    const Cell* b = cellWithValue(result, to);
    std::vector<const Cell*> found;
    if (a == nullptr || b == nullptr)
        return found;
    for (const Cell* edge : edges(result)) {
        if (edge->source() == a->id() && edge->target() == b->id())
            found.push_back(edge);
    }
    return found;
}

inline Rect boundsOf(const Cell& cell) { return cell.geometry()->bounds(); }

// The text of the errors, one per line.
inline QString errorText(const imports::Result& result)
{
    QStringList lines;
    for (const imports::Diagnostic& d : result.errors)
        lines << q(imports::describe(d));
    return lines.join('\n');
}

// Every id is unique, parents and edge terminals come from the same result and parents come first.
inline bool wellFormed(const imports::Result& result, QString* why = nullptr)
{
    std::vector<std::string> seen;
    for (const Cell& cell : result.cells) {
        for (const std::string& id : seen) {
            if (id == cell.id()) {
                if (why != nullptr)
                    *why = QStringLiteral("duplicate id %1").arg(q(id));
                return false;
            }
        }
        if (!cell.parent().empty() && std::find(seen.begin(), seen.end(), cell.parent()) == seen.end()) {
            if (why != nullptr)
                *why = QStringLiteral("parent %1 of %2 does not come first").arg(q(cell.parent()), q(cell.id()));
            return false;
        }
        seen.push_back(cell.id());
    }
    for (const Cell& cell : result.cells) {
        for (const std::string& terminal : {cell.source(), cell.target()}) {
            if (!terminal.empty() && std::find(seen.begin(), seen.end(), terminal) == seen.end()) {
                if (why != nullptr)
                    *why = QStringLiteral("terminal %1 of %2 is not in the result").arg(q(terminal), q(cell.id()));
                return false;
            }
        }
    }
    return true;
}

// The top-level vertices do not overlap.
inline bool noOverlaps(const imports::Result& result)
{
    std::vector<Rect> boxes;
    for (const Cell* cell : vertices(result)) {
        if (cell->parent().empty() && cell->geometry() != nullptr && !cell->geometry()->relative())
            boxes.push_back(boundsOf(*cell));
    }
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        for (std::size_t j = i + 1; j < boxes.size(); ++j) {
            const Rect& a = boxes[i];
            const Rect& b = boxes[j];
            if (a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height)
                return false;
        }
    }
    return true;
}

// Imports every prefix of `text` that ends at a line break or in the middle of a line (a few hundred
// of them): none may crash, and each answers with cells or with errors, never with neither.
inline void truncationsAreSafe(std::string_view text, const std::function<imports::Result(std::string_view)>& import)
{
    const std::size_t step = std::max<std::size_t>(1, text.size() / 300);
    for (std::size_t length = 0; length <= text.size(); length += step) {
        const imports::Result result = import(text.substr(0, length));
        QVERIFY2(result.ok() || !result.errors.empty(), qPrintable(QStringLiteral("a prefix of %1 characters gave neither cells nor errors").arg(length)));
        QVERIFY(!result.ok() || result.errors.empty());
        QVERIFY2(result.errors.empty() || result.cells.empty(), "errors with cells");
        for (const imports::Diagnostic& d : result.errors)
            QVERIFY2(!d.message.empty(), "an error without a message");
    }
}

} // namespace import_test
