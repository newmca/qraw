#pragma once
// Helpers of the selection tests: a session with its canvas and readers for
// the model values the tests assert on.

#include "support/test_support.h"

#include "qraw/commands/ops_pages.h"

namespace qraw::ui::test {

struct SelectFixture {
    DocumentSession session{simpleDocument()};
    std::unique_ptr<CanvasView> view = makeCanvas(session);
    QByteArray original = savedBytes(session);

    CanvasView& canvas() { return *view; }

    [[nodiscard]] const core::Cell& cell(const std::string& id) const { return *session.currentPage()->findCell(id); }
    // The stored geometry (parent coordinates).
    [[nodiscard]] core::Rect geometry(const std::string& id) const { return cell(id).geometry()->bounds(); }
    // Absolute unrotated bounds as the canvas shows them.
    [[nodiscard]] QRectF bounds(const std::string& id) const
    {
        const render::CellState* state = view->diagramScene().state(id);
        return state != nullptr ? QRectF(state->bounds.x, state->bounds.y, state->bounds.width, state->bounds.height)
                                : QRectF();
    }
    [[nodiscard]] std::vector<std::string> selected() const { return session.selection().cells(); }

    void setGrid(bool on)
    {
        commands::ops::PageProperties properties;
        properties.grid = on;
        session.edit(QStringLiteral("Grid"), [&](commands::Transaction& tx) {
            return commands::ops::setPageProperties(tx, 0, properties);
        });
        original = savedBytes(session); // the grid switch is part of the fixture
        session.endMerge();
    }

    // Undoes the last step and checks that the document is byte-identical to `before`.
    [[nodiscard]] bool undoRestores(const QByteArray& before)
    {
        return session.undo() && savedBytes(session) == before;
    }
};

inline bool sameIds(std::vector<std::string> actual, std::vector<std::string> expected)
{
    std::sort(actual.begin(), actual.end());
    std::sort(expected.begin(), expected.end());
    return actual == expected;
}

} // namespace qraw::ui::test
