// The Text tab of the Format panel: font, alignment, label position and label
// boxes (draw.io's TextFormatPanel, Format.js:4683; addFont :4955; the label
// position presets are the lset table of Format.js:5201).
#include "format_controls.h"
#include "format_edit.h"
#include "font_families.h"
#include "format_tab.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_properties.h"
#include "qraw/commands/transaction.h"
#include "qraw/render/resolved_style.h"
#include "qraw/ui/app_settings.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/document_session.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextDocumentFragment>
#include <QToolButton>
#include <QVBoxLayout>

#include <array>
#include <iterator>
#include <utility>

namespace qraw::ui {
namespace {

using commands::ops::StyleEdit;

StyleEdit styleEdit(std::string key, std::optional<std::string> value)
{
    return StyleEdit{std::move(key), std::move(value)};
}

// draw.io's default font family (mxConstants.DEFAULT_FONTFAMILY).
constexpr const char* kDefaultFontFamily = "Arial,Helvetica";

// Label position presets: labelPosition, verticalLabelPosition, align and
// verticalAlign, in that order (Format.js lset, :5201).
struct LabelPosition {
    const char* label;
    const char* horizontal;
    const char* vertical;
    const char* align;
    const char* verticalAlign;
};

constexpr LabelPosition kLabelPositions[] = {
    {"Top left", "left", "top", "right", "bottom"},
    {"Top", "center", "top", "center", "bottom"},
    {"Top right", "right", "top", "left", "bottom"},
    {"Left", "left", "middle", "right", "middle"},
    {"Center", "center", "middle", "center", "middle"},
    {"Right", "right", "middle", "left", "middle"},
    {"Bottom left", "left", "bottom", "right", "top"},
    {"Bottom", "center", "bottom", "center", "top"},
    {"Bottom right", "right", "bottom", "left", "top"},
};

int labelPositionIndexOf(const StyleSummary& summary)
{
    const std::array<StyleValue, 4> values = {
        summary.value("labelPosition"),
        summary.value("verticalLabelPosition"),
        summary.value("align"),
        summary.value("verticalAlign"),
    };
    for (const StyleValue& value : values) {
        if (value.mixed)
            return -1;
    }
    const std::array<std::string, 4> current = {
        values[0].value.value_or("center"),
        values[1].value.value_or("middle"),
        values[2].value.value_or("center"),
        values[3].value.value_or("middle"),
    };
    for (int i = 0; i < static_cast<int>(std::size(kLabelPositions)); ++i) {
        const LabelPosition& preset = kLabelPositions[i];
        if (current[0] == preset.horizontal && current[1] == preset.vertical && current[2] == preset.align &&
            current[3] == preset.verticalAlign)
            return i;
    }
    return -1;
}

// nullopt shows the indeterminate state of a toggle button.
void setButtonState(QAbstractButton* button, std::optional<bool> on)
{
    QSignalBlocker blocker(button);
    button->setChecked(on.value_or(false));
}

// Checked when the value (or its absent-value default) is `expected`.
std::optional<bool> valueIs(const StyleValue& value, const char* absentDefault, const char* expected)
{
    if (value.mixed)
        return std::nullopt;
    return value.value.value_or(absentDefault) == expected;
}

// Writes style keys on every selected cell in one undo step.
void writeStyles(DocumentSession& session, const QString& name, std::vector<StyleEdit> edits,
                 std::string_view mergeKey = {})
{
    const std::vector<std::string> ids = selectedCells(session);
    if (ids.empty())
        return;
    const std::string merge = mergeKey.empty() ? std::string() : formatMergeKey(mergeKey, ids);
    applyStyles(session, name, ids, std::move(edits), merge);
}

class TextTab final : public FormatTab {
public:
    explicit TextTab(DocumentSession& session);

    void refresh(const StyleSummary& summary) override;

private:
    void buildFont(QVBoxLayout* layout);
    void buildAlignment(QVBoxLayout* layout);
    void buildLabel(QVBoxLayout* layout);

    void writeFamily(const QString& family);
    void writeFontStyleBit(int bit, bool on);
    void writeFormattedText(bool on);

    QString shownFamily_; // the family the combo box shows, for change detection

    QComboBox* fontFamily_ = nullptr;
    QSpinBox* fontSize_ = nullptr;
    QToolButton* bold_ = nullptr;
    QToolButton* italic_ = nullptr;
    QToolButton* underline_ = nullptr;
    ColorField* fontColor_ = nullptr;
    QToolButton* alignLeft_ = nullptr;
    QToolButton* alignCenter_ = nullptr;
    QToolButton* alignRight_ = nullptr;
    QToolButton* verticalTop_ = nullptr;
    QToolButton* verticalMiddle_ = nullptr;
    QToolButton* verticalBottom_ = nullptr;
    QComboBox* labelPosition_ = nullptr;
    QCheckBox* wordWrap_ = nullptr;
    QCheckBox* formatted_ = nullptr;
    ColorField* labelBackground_ = nullptr;
    ColorField* labelBorder_ = nullptr;
};

TextTab::TextTab(DocumentSession& session) : FormatTab(session)
{
    buildFont(contentLayout());
    buildAlignment(contentLayout());
    buildLabel(contentLayout());
    contentLayout()->addStretch();
}

void TextTab::buildFont(QVBoxLayout* layout)
{
    QFormLayout* form = addFormGroup(layout, formatTr("Font"));

    fontFamily_ = new QComboBox;
    fontFamily_->setObjectName(QStringLiteral("format.fontFamily"));
    fontFamily_->setEditable(true);
    fontFamily_->setInsertPolicy(QComboBox::NoInsert); // typed families are not list entries
    fontFamily_->setToolTip(formatTr("A font family or a comma-separated list, for example Arial,Helvetica"));
    fillFontFamilyCombo(fontFamily_, AppSettings().recentFonts());
    form->addRow(formatTr("Family"), fontFamily_);
    onActivated(fontFamily_, [this](int index) {
        writeFamily(fontFamily_->itemText(index));
    });
    QObject::connect(fontFamily_->lineEdit(), &QLineEdit::editingFinished, fontFamily_, [this] {
        writeFamily(fontFamily_->lineEdit()->text().trimmed());
    });

    fontSize_ = createIntSpin(nullptr, 1, 999);
    fontSize_->setObjectName(QStringLiteral("format.fontSize"));
    form->addRow(formatTr("Size"), fontSize_);
    onSpinEdited(fontSize_, session(), [this](double value) {
        writeStyles(session(), formatTr("Font Size"), {styleEdit("fontSize", QString::number(value).toStdString())},
                    "fontSize");
    });

    auto* styles = new QWidget;
    auto* row = new QHBoxLayout(styles);
    row->setContentsMargins(0, 0, 0, 0);
    bold_ = new QToolButton;
    bold_->setObjectName(QStringLiteral("format.bold"));
    bold_->setText(formatTr("B"));
    bold_->setCheckable(true);
    italic_ = new QToolButton;
    italic_->setObjectName(QStringLiteral("format.italic"));
    italic_->setText(formatTr("I"));
    italic_->setCheckable(true);
    underline_ = new QToolButton;
    underline_->setObjectName(QStringLiteral("format.underline"));
    underline_->setText(formatTr("U"));
    underline_->setCheckable(true);
    row->addWidget(bold_);
    row->addWidget(italic_);
    row->addWidget(underline_);
    row->addStretch();
    form->addRow(formatTr("Style"), styles);
    onClicked(bold_, [this](bool on) { writeFontStyleBit(render::FontBold, on); });
    onClicked(italic_, [this](bool on) { writeFontStyleBit(render::FontItalic, on); });
    onClicked(underline_, [this](bool on) { writeFontStyleBit(render::FontUnderline, on); });

    fontColor_ = new ColorField;
    fontColor_->setObjectName(QStringLiteral("format.fontColor"));
    fontColor_->setPurpose(formatTr("Font Color"));
    form->addRow(formatTr("Color"), fontColor_);
    QObject::connect(fontColor_, &ColorField::edited, fontColor_, [this](std::string value) {
        writeStyles(session(), formatTr("Font Color"), {styleEdit("fontColor", value)});
    });
}

void TextTab::buildAlignment(QVBoxLayout* layout)
{
    QFormLayout* form = addFormGroup(layout, formatTr("Alignment"));

    auto makeButton = [](const char* objectName, const QString& text) {
        auto* button = new QToolButton;
        button->setObjectName(QString::fromLatin1(objectName));
        button->setText(text);
        button->setCheckable(true);
        return button;
    };

    auto* horizontal = new QWidget;
    auto* row = new QHBoxLayout(horizontal);
    row->setContentsMargins(0, 0, 0, 0);
    alignLeft_ = makeButton("format.alignLeft", formatTr("Left"));
    alignCenter_ = makeButton("format.alignCenter", formatTr("Center"));
    alignRight_ = makeButton("format.alignRight", formatTr("Right"));
    for (QToolButton* button : {alignLeft_, alignCenter_, alignRight_})
        row->addWidget(button);
    form->addRow(formatTr("Horizontal"), horizontal);

    auto* vertical = new QWidget;
    auto* column = new QHBoxLayout(vertical);
    column->setContentsMargins(0, 0, 0, 0);
    verticalTop_ = makeButton("format.verticalTop", formatTr("Top"));
    verticalMiddle_ = makeButton("format.verticalMiddle", formatTr("Middle"));
    verticalBottom_ = makeButton("format.verticalBottom", formatTr("Bottom"));
    for (QToolButton* button : {verticalTop_, verticalMiddle_, verticalBottom_})
        column->addWidget(button);
    form->addRow(formatTr("Vertical"), vertical);

    // A checked button stays checked when clicked again: the write happens
    // only for a button that was switched on.
    const std::pair<QToolButton*, const char*> aligns[] = {
        {alignLeft_, "left"}, {alignCenter_, "center"}, {alignRight_, "right"}};
    for (const auto& [button, value] : aligns) {
        onClicked(button, [this, button, value](bool on) {
            if (!on) {
                button->setChecked(true);
                return;
            }
            writeStyles(session(), formatTr("Horizontal Align"), {styleEdit("align", value)});
        });
    }
    const std::pair<QToolButton*, const char*> verticals[] = {
        {verticalTop_, "top"}, {verticalMiddle_, "middle"}, {verticalBottom_, "bottom"}};
    for (const auto& [button, value] : verticals) {
        onClicked(button, [this, button, value](bool on) {
            if (!on) {
                button->setChecked(true);
                return;
            }
            writeStyles(session(), formatTr("Vertical Align"), {styleEdit("verticalAlign", value)});
        });
    }

    labelPosition_ = new QComboBox;
    labelPosition_->setObjectName(QStringLiteral("format.labelPosition"));
    for (const LabelPosition& preset : kLabelPositions)
        labelPosition_->addItem(formatTr(preset.label));
    form->addRow(formatTr("Position"), labelPosition_);
    onActivated(labelPosition_, [this](int index) {
        const LabelPosition& preset = kLabelPositions[index];
        writeStyles(session(), formatTr("Label Position"),
                    {styleEdit("labelPosition", preset.horizontal), styleEdit("verticalLabelPosition", preset.vertical),
                     styleEdit("align", preset.align), styleEdit("verticalAlign", preset.verticalAlign)});
    });
}

void TextTab::buildLabel(QVBoxLayout* layout)
{
    QFormLayout* form = addFormGroup(layout, formatTr("Label"));

    wordWrap_ = new QCheckBox(formatTr("Word wrap"));
    wordWrap_->setObjectName(QStringLiteral("format.wordWrap"));
    form->addRow(wordWrap_);
    onClicked(wordWrap_, [this](bool on) {
        writeStyles(session(), formatTr("Word Wrap"),
                    {styleEdit("whiteSpace", on ? std::optional<std::string>("wrap") : std::nullopt)});
    });

    formatted_ = new QCheckBox(formatTr("Formatted text"));
    formatted_->setObjectName(QStringLiteral("format.html"));
    form->addRow(formatted_);
    onClicked(formatted_, [this](bool on) { writeFormattedText(on); });

    labelBackground_ = new ColorField;
    labelBackground_->setObjectName(QStringLiteral("format.labelBackgroundColor"));
    labelBackground_->setPurpose(formatTr("Label Background"));
    form->addRow(formatTr("Background"), labelBackground_);
    QObject::connect(labelBackground_, &ColorField::edited, labelBackground_, [this](std::string value) {
        writeStyles(session(), formatTr("Label Background"), {styleEdit("labelBackgroundColor", value)});
    });

    labelBorder_ = new ColorField;
    labelBorder_->setObjectName(QStringLiteral("format.labelBorderColor"));
    labelBorder_->setPurpose(formatTr("Label Border"));
    form->addRow(formatTr("Border"), labelBorder_);
    QObject::connect(labelBorder_, &ColorField::edited, labelBorder_, [this](std::string value) {
        writeStyles(session(), formatTr("Label Border"), {styleEdit("labelBorderColor", value)});
    });
}

// fontFamily: the combo box text, written when a choice or an edit is made.
void TextTab::writeFamily(const QString& family)
{
    if (family.isEmpty() || family == shownFamily_)
        return;
    shownFamily_ = family;
    writeStyles(session(), formatTr("Font Family"), {styleEdit("fontFamily", family.toStdString())});
    // The new family goes first in the list.
    AppSettings settings;
    settings.addRecentFont(family);
    fillFontFamilyCombo(fontFamily_, settings.recentFonts());
}

// fontStyle bit on or off. fontStyle may differ per cell, so each cell gets
// its own value (one ops::setStyles per cell inside one undo step).
void TextTab::writeFontStyleBit(int bit, bool on)
{
    const std::vector<std::string> ids = selectedCells(session());
    const core::Page* current = session().currentPage();
    if (ids.empty() || current == nullptr)
        return;

    // Computed before the edit: the new value of each cell's own fontStyle.
    const render::Stylesheet& stylesheet = stylesheetOf(session());
    std::vector<std::pair<std::string, std::string>> values;
    for (const std::string& id : ids) {
        const auto cellStyle = StyleSummary::of(*current, {id}, stylesheet);
        const int fontStyle = static_cast<int>(cellStyle.number("fontStyle", 0).value_or(0));
        const int next = on ? (fontStyle | bit) : (fontStyle & ~bit);
        values.emplace_back(id, std::to_string(next));
    }

    editCurrentPage(session(), formatTr("Font Style"),
                    [&values](commands::Transaction& tx, std::size_t index) {
                        for (const auto& [id, value] : values) {
                            const commands::Result result =
                                commands::ops::setStyles(tx, index, {id}, {styleEdit("fontStyle", value)});
                            if (!result)
                                return result;
                        }
                        return commands::Result::success();
                    });
}

// Formatted text (html=1). Turning it off converts each HTML label to plain
// text in the same undo step (draw.io's Graph.convertValueToString).
void TextTab::writeFormattedText(bool on)
{
    const std::vector<std::string> ids = selectedCells(session());
    const core::Page* current = session().currentPage();
    if (ids.empty() || current == nullptr)
        return;

    std::vector<std::pair<std::string, std::string>> plainLabels;
    if (!on) {
        for (const std::string& id : ids) {
            if (!isHtmlLabel(*current, id))
                continue;
            const QString plain =
                QTextDocumentFragment::fromHtml(QString::fromStdString(labelOf(*current, id))).toPlainText();
            plainLabels.emplace_back(id, plain.toStdString());
        }
    }

    const std::optional<std::string> html = on ? std::optional<std::string>("1") : std::nullopt;
    editCurrentPage(session(), formatTr("Formatted Text"),
                    [&ids, &plainLabels, html](commands::Transaction& tx, std::size_t index) {
                        for (const auto& [id, label] : plainLabels) {
                            const commands::Result result = commands::ops::setLabel(tx, index, id, label);
                            if (!result)
                                return result;
                        }
                        return commands::ops::setStyles(tx, index, ids, {styleEdit("html", html)});
                    });
}

void TextTab::refresh(const StyleSummary& summary)
{
    const StyleValue family = summary.value("fontFamily");
    shownFamily_ = family.mixed ? QString() : QString::fromStdString(family.value.value_or(kDefaultFontFamily));
    {
        QSignalBlocker comboBlocker(fontFamily_);
        QSignalBlocker lineBlocker(fontFamily_->lineEdit());
        if (family.mixed) {
            fontFamily_->setCurrentIndex(-1);
            fontFamily_->setEditText(QString());
        } else {
            fontFamily_->setEditText(shownFamily_);
        }
    }
    setSpinState(fontSize_, summary.number("fontSize", render::kDefaultFontSize));

    setButtonState(bold_, summary.fontStyleBit(render::FontBold));
    setButtonState(italic_, summary.fontStyleBit(render::FontItalic));
    setButtonState(underline_, summary.fontStyleBit(render::FontUnderline));

    const StyleValue color = summary.value("fontColor");
    fontColor_->setState(color.value, color.mixed);

    const StyleValue horizontal = summary.value("align");
    const StyleValue vertical = summary.value("verticalAlign");
    setButtonState(alignLeft_, valueIs(horizontal, "center", "left"));
    setButtonState(alignCenter_, valueIs(horizontal, "center", "center"));
    setButtonState(alignRight_, valueIs(horizontal, "center", "right"));
    setButtonState(verticalTop_, valueIs(vertical, "middle", "top"));
    setButtonState(verticalMiddle_, valueIs(vertical, "middle", "middle"));
    setButtonState(verticalBottom_, valueIs(vertical, "middle", "bottom"));

    {
        QSignalBlocker blocker(labelPosition_);
        labelPosition_->setCurrentIndex(labelPositionIndexOf(summary));
    }

    const StyleValue whiteSpace = summary.value("whiteSpace");
    setCheckState(wordWrap_, whiteSpace.mixed ? std::nullopt : std::optional<bool>(whiteSpace.value == "wrap"));
    setCheckState(formatted_, summary.flag("html", false));

    const StyleValue background = summary.value("labelBackgroundColor");
    labelBackground_->setState(background.value, background.mixed);
    const StyleValue border = summary.value("labelBorderColor");
    labelBorder_->setState(border.value, border.mixed);
}

} // namespace

std::unique_ptr<FormatTab> makeTextTab(DocumentSession& session)
{
    return std::make_unique<TextTab>(session);
}

} // namespace qraw::ui
