#include "qraw/commands/ops_properties.h"

#include "qraw/commands/primitives.h"
#include "qraw/commands/queries.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/cell.h"
#include "qraw/core/page.h"
#include "qraw/core/style.h"

#include <functional>
#include <string>

namespace qraw::commands::ops {

namespace {

bool isAsciiLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }

// Applies `edit` to every cell in `ids` and emits one ReplaceCell per cell
// whose state changed. The page is re-fetched after each emitted command,
// because structural changes invalidate pointers.
Result editCells(Transaction& tx, std::size_t page, const std::vector<std::string>& ids,
                 const std::function<void(core::Cell&)>& edit)
{
    for (const std::string& id : ids) {
        const core::Page* p = tx.page(page);
        if (!p)
            return Result::failure("unknown page");
        if (!isAddressable(*p, id))
            return Result::failure("unknown cell '" + id + "'");
        core::Cell cell = *p->findCell(id);
        const core::Cell original = cell;
        edit(cell);
        if (cell != original && !tx.emplace<ReplaceCell>(page, cell))
            return Result::failure("cannot replace cell '" + id + "'");
    }
    return Result::success(ids);
}

bool isStyleKeyValid(const std::string& key)
{
    return !key.empty() && key.find_first_of(";=") == std::string::npos;
}

// XML 1.0 (5th edition) NameStartChar / NameChar without the colon, on code
// points. ASCII is spelled out; the rest follows the specification's ranges
// (the XML parsers of draw.io's own readers accept these).
bool isNameStartPoint(char32_t c)
{
    if (c < 0x80)
        return isAsciiLetter(static_cast<char>(c)) || c == '_';
    return (c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) || (c >= 0xF8 && c <= 0x2FF) ||
           (c >= 0x370 && c <= 0x37D) || (c >= 0x37F && c <= 0x1FFF) || (c >= 0x200C && c <= 0x200D) ||
           (c >= 0x2070 && c <= 0x218F) || (c >= 0x2C00 && c <= 0x2FEF) || (c >= 0x3001 && c <= 0xD7FF) ||
           (c >= 0xF900 && c <= 0xFDCF) || (c >= 0xFDF0 && c <= 0xFFFD) || (c >= 0x10000 && c <= 0xEFFFF);
}

bool isNamePoint(char32_t c)
{
    if (c < 0x80)
        return isNameStartPoint(c) || isAsciiDigit(static_cast<char>(c)) || c == '-' || c == '.';
    return isNameStartPoint(c) || c == 0xB7 || (c >= 0x300 && c <= 0x36F) || (c >= 0x203F && c <= 0x2040);
}

// Decodes one UTF-8 code point at `pos`; false on malformed input.
bool nextCodePoint(std::string_view text, std::size_t& pos, char32_t& out)
{
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(pos);
    std::size_t length = 1;
    if (lead < 0x80) {
        out = lead;
    } else if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
        out = lead & 0x1F;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        out = lead & 0x0F;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        out = lead & 0x07;
    } else {
        return false;
    }
    if (pos + length > text.size())
        return false;
    for (std::size_t i = 1; i < length; ++i) {
        if ((byte(pos + i) & 0xC0) != 0x80)
            return false;
        out = (out << 6) | (byte(pos + i) & 0x3F);
    }
    pos += length;
    return true;
}

// An XML name for a custom attribute; `allowColon` admits the namespace
// prefixes of names read from files (find and replace edits those).
bool isXmlName(std::string_view name, bool allowColon)
{
    if (name.empty())
        return false;
    std::size_t pos = 0;
    bool first = true;
    while (pos < name.size()) {
        char32_t point = 0;
        if (!nextCodePoint(name, pos, point))
            return false;
        const bool colon = point == ':';
        const bool ok = (colon && allowColon) || (first ? isNameStartPoint(point) : isNamePoint(point));
        if (!ok)
            return false;
        first = false;
    }
    return true;
}

// An XML name for a custom attribute. `id` and `label` have their own
// operations (setLabel and the cell id), so they are refused here.
bool isCustomAttributeName(std::string_view name)
{
    return name != "id" && name != "label" && isXmlName(name, true);
}

// Sets (value) or removes (nullopt) one attribute on `cell`'s wrapper, wrapping
// a plain cell first. Removing from a plain cell changes nothing.
void applyDataEdit(core::Cell& cell, const DataEdit& edit, std::string_view wrapperTag)
{
    if (!edit.value) {
        if (cell.hasWrapper())
            cell.wrapper()->attributes.remove(edit.name);
        return;
    }
    if (!cell.hasWrapper())
        cell.ensureWrapper(wrapperTag);
    cell.setData(edit.name, *edit.value);
}

} // namespace

Result setStyles(Transaction& tx, std::size_t page, const std::vector<std::string>& ids,
                 const std::vector<StyleEdit>& edits)
{
    for (const StyleEdit& edit : edits) {
        if (!isStyleKeyValid(edit.key))
            return Result::failure("invalid style key");
    }
    return editCells(tx, page, ids, [&edits](core::Cell& cell) {
        core::Style style = cell.style();
        for (const StyleEdit& edit : edits) {
            if (!edit.value || edit.value->empty())
                style.remove(edit.key);
            else
                style.set(edit.key, *edit.value);
        }
        cell.setStyle(style);
    });
}

Result setStyle(Transaction& tx, std::size_t page, const std::vector<std::string>& ids, const std::string& key,
                std::optional<std::string> value)
{
    return setStyles(tx, page, ids, {StyleEdit{key, std::move(value)}});
}

Result setStyleText(Transaction& tx, std::size_t page, const std::vector<std::string>& ids, std::string_view style)
{
    return editCells(tx, page, ids, [style](core::Cell& cell) { cell.setStyleText(style); });
}

Result setStyleName(Transaction& tx, std::size_t page, const std::vector<std::string>& ids, const std::string& name,
                    bool present)
{
    return editCells(tx, page, ids, [&name, present](core::Cell& cell) {
        core::Style style = cell.style();
        if (present)
            style.addName(name); // no-op when the name is already present
        else
            style.removeName(name);
        cell.setStyle(style);
    });
}

Result setLabel(Transaction& tx, std::size_t page, const std::string& id, std::string_view label)
{
    return editCells(tx, page, {id}, [label](core::Cell& cell) { cell.setValue(label); });
}

Result setCellData(Transaction& tx, std::size_t page, const std::string& id, const std::string& name,
                   std::optional<std::string> value, std::string_view wrapperTag)
{
    return editCellData(tx, page, {id}, {DataEdit{name, std::move(value)}}, wrapperTag);
}

Result editCellData(Transaction& tx, std::size_t page, const std::vector<std::string>& ids,
                    const std::vector<DataEdit>& edits, std::string_view wrapperTag)
{
    for (const DataEdit& edit : edits) {
        if (!isCustomAttributeName(edit.name))
            return Result::failure("invalid attribute name");
    }
    return editCells(tx, page, ids, [&edits, wrapperTag](core::Cell& cell) {
        for (const DataEdit& edit : edits)
            applyDataEdit(cell, edit, wrapperTag);
    });
}

bool isValidDataName(std::string_view name)
{
    return !isReservedDataName(name) && isXmlName(name, false);
}

bool isReservedDataName(std::string_view name)
{
    return name == "id" || name == "label" || name == "placeholders" || name.find(':') != std::string_view::npos;
}

} // namespace qraw::commands::ops
