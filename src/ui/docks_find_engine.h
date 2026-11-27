#pragma once
// Pattern and text helpers behind FindEngine and FindPanel. Not part of the public API; see find_panel.h.
#include "qraw/ui/find_panel.h"

#include <QRegularExpression>
#include <QString>

#include <functional>

namespace qraw::core {
class Cell;
}

namespace qraw::ui::docks {

// The pattern for `text` under `options`: escaped unless regex, wrapped in
// \b..\b for whole words, case-insensitive unless matchCase. Check isValid().
[[nodiscard]] QRegularExpression findPattern(const QString& text, const FindOptions& options);

// True when `pattern` has a non-empty match in `text`.
[[nodiscard]] bool containsMatch(const QString& text, const QRegularExpression& pattern);

// Replaces the matches of `pattern` in `fragment`. With options.regex the
// replacement is a template (\1..\9); otherwise it is inserted literally.
[[nodiscard]] QString replaceMatches(const QString& fragment, const QRegularExpression& pattern,
                                     const QString& replacement, const FindOptions& options);

// Applies `replaceText` to the text nodes of an HTML fragment only: tags are
// copied unchanged. A text node is decoded (basic entities) before it is
// given to `replaceText`, and encoded again only when it changed.
[[nodiscard]] QString replaceInMarkup(const QString& markup,
                                      const std::function<QString(const QString&)>& replaceText);

// The label text Find matches: tags stripped and entities decoded for HTML
// labels (mxUtils.extractTextWithWhitespace), the raw value otherwise.
[[nodiscard]] QString plainLabelText(const QString& label, bool html);

// Whether the cell's style has html=1.
[[nodiscard]] bool isHtmlLabel(const core::Cell& cell);

} // namespace qraw::ui::docks
