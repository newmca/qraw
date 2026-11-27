#pragma once
// Find / Replace (draw.io's FindWindow, Ctrl+F).
// (src/ui/find_panel.cpp, src/ui/find_engine.cpp).

#include <QString>
#include <QWidget>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace qraw::core {
class Document;
}

namespace qraw::ui {

class DocumentSession;

struct FindOptions {
    bool matchCase = false;
    bool wholeWord = false;
    bool regex = false;
    bool allPages = false;     // search every page (draw.io "All pages")
    bool includeData = true;   // also custom attributes of wrapped cells (not id/label)
    bool shapes = false;       // also match the shape type of each cell ("Shape types")
};

// FindMatch::attribute of a match on the shape type (never replaced: Replace
// does not edit styles).
inline constexpr char kShapeMatchAttribute[] = "shape";

struct FindMatch {
    std::size_t page = 0;
    std::string cellId;
    std::string attribute; // "label" (the label/value), the custom attribute name, or kShapeMatchAttribute
};

// The search itself, without widgets. Labels are matched on their plain
// text (HTML labels with tags stripped and entities decoded, as draw.io's
// FindWindow uses mxUtils.extractTextWithWhitespace); custom attributes on
// their raw value. Order: pages in order (starting with the current page
// when allPages), cells in z-order, label before attributes.
class FindEngine {
public:
    [[nodiscard]] static std::vector<FindMatch> findAll(const core::Document& document, std::size_t currentPage,
                                                        const QString& text, const FindOptions& options);
    // Replaces every occurrence inside one match's value. HTML labels: only
    // text content changes, markup is kept. Returns the new value.
    [[nodiscard]] static std::string replaceIn(const std::string& value, bool html, const QString& text,
                                               const QString& replacement, const FindOptions& options);
};

// The panel: search field, replace field, the options above, Find Next
// (selects the next match's cell, switching page when needed, and scrolls
// to it via the selectionChanged consumers), Replace (current match), Replace
// All (one DocumentSession::edit "Replace All" for the current page, or for
// all pages with allPages: ops::setLabel / ops::setCellData per match), Select
// All (selects every match on the current page, for "find all cylinders"), and
// a status line "n of m".
class FindPanel : public QWidget {
    Q_OBJECT

public:
    explicit FindPanel(DocumentSession& session, QWidget* parent = nullptr);
    ~FindPanel() override;

    void setSearchText(const QString& text);
    void setReplaceText(const QString& text);
    void setOptions(const FindOptions& options);
    bool findNext();       // false when nothing matches
    bool replaceCurrent(); // replaces the current match, then finds the next
    int replaceAll();      // number of replaced values
    int selectAll();       // number of cells selected on the current page
    // Focuses the search field and selects its text (Ctrl+F).
    void activate();

signals:
    // A match was selected; the canvas should scroll it into view.
    void revealCell(const QString& cellId);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::ui
