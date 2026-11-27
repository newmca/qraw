#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace qraw::commands {

// Outcome of an operation (see ops_*.h) or an Editor edit.
//
// On failure the operation has changed nothing: the Editor rolls the
// transaction back to the savepoint taken before the operation ran.
struct Result {
    bool ok = true;
    std::string error; // human-readable reason when !ok (English, not translated)

    // Cells the UI should select afterwards, following draw.io (e.g. the new
    // group after groupCells, the former children after ungroupCells, the
    // remaining parents after removeCells). Ids on the operation's page.
    std::vector<std::string> cells;
    // The page the UI should show afterwards (page operations only), as an
    // index into Document::pages().
    std::optional<std::size_t> page;

    static Result success() { return {}; }
    static Result success(std::vector<std::string> cells)
    {
        Result r;
        r.cells = std::move(cells);
        return r;
    }
    static Result failure(std::string error)
    {
        Result r;
        r.ok = false;
        r.error = std::move(error);
        return r;
    }

    explicit operator bool() const { return ok; }
};

} // namespace qraw::commands
