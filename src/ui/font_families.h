#pragma once
// The font family list of the Format panel's Text tab: draw.io's default fonts
// (Menus.defaultFonts), the recently used ones, then every installed family.
// Values are draw.io family lists ("Arial,Helvetica"), so the combo box stays a
// plain QComboBox (QFontComboBox cannot show a comma list) and is edited as text.

#include <QStringList>

class QComboBox;

namespace qraw::ui {

// draw.io's default web fonts, in its order (Menus.prototype.defaultFonts).
[[nodiscard]] QStringList defaultFontFamilies();

// The installed families (QFontDatabase::families, private families left out),
// sorted without regard to case. Read once, on first use: the query is slow
// on systems with many fonts.
[[nodiscard]] const QStringList& installedFontFamilies();

// Fills `combo` with the default fonts, a separator, `recent` (most recent
// first), a separator and the installed families, and gives it a completer
// that finds any family containing the typed text. Entries of the first two
// sections are drawn in their own family. The text being edited is kept.
void fillFontFamilyCombo(QComboBox* combo, const QStringList& recent);

} // namespace qraw::ui
