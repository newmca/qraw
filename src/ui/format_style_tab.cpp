// The Style tab of the Format panel: fill, line, effects and edge styles
// (draw.io's StyleFormatPanel, Format.js:6836; addFill :7378, addStroke
// :7710, addEffects :8540; the edge style menu and addLineEnd).

#include "format_controls.h"
#include "format_edit.h"
#include "format_tab.h"

#include "qraw/render/resolved_style.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <initializer_list>
#include <iterator>

namespace qraw::ui {
namespace {

using commands::ops::StyleEdit;

StyleEdit styleEdit(std::string key, std::optional<std::string> value)
{
    return StyleEdit{std::move(key), std::move(value)};
}

// Writes style keys on the selected cells of `filter` in one undo step. A
// non-empty `mergeKey` names a spin box whose changes coalesce.
void writeStyles(DocumentSession& session, const QString& name, CellFilter filter,
                 std::vector<StyleEdit> edits, std::string_view mergeKey = {})
{
    const std::vector<std::string> ids = selectedCells(session, filter);
    if (ids.empty())
        return;
    const std::string merge = mergeKey.empty() ? std::string() : formatMergeKey(mergeKey, ids);
    applyStyles(session, name, ids, std::move(edits), merge);
}

// --- dash patterns (Format.js addStroke) ------------------------------------

enum DashIndex { DashSolid, DashDashed, DashDotted };

std::vector<StyleEdit> dashEdits(int index)
{
    switch (index) {
    case DashDashed:
        return {styleEdit("dashed", "1"), styleEdit("dashPattern", std::nullopt)};
    case DashDotted:
        return {styleEdit("dashed", "1"), styleEdit("dashPattern", "1 2")};
    default:
        return {styleEdit("dashed", std::nullopt), styleEdit("dashPattern", std::nullopt)};
    }
}

int dashIndexOf(const StyleSummary& summary)
{
    const std::optional<bool> dashed = summary.flag("dashed", false);
    if (!dashed)
        return -1;
    if (!*dashed)
        return DashSolid;
    const StyleValue pattern = summary.value("dashPattern");
    const bool dotted = !pattern.mixed && pattern.value && *pattern.value == "1 2";
    return dotted ? DashDotted : DashDashed;
}

// --- edge styles (the Format.js edge style menu) -------------------------------

enum EdgeStyleIndex { EdgeStraight, EdgeOrthogonal, EdgeElbow, EdgeIsometric, EdgeCurved, EdgeEntityRelation };

std::vector<StyleEdit> edgeStyleEdits(int index)
{
    switch (index) {
    case EdgeOrthogonal:
        return {styleEdit("edgeStyle", "orthogonalEdgeStyle"), styleEdit("curved", std::nullopt)};
    case EdgeElbow:
        return {styleEdit("edgeStyle", "elbowEdgeStyle"), styleEdit("curved", std::nullopt)};
    case EdgeIsometric:
        return {styleEdit("edgeStyle", "isometricEdgeStyle"), styleEdit("curved", std::nullopt)};
    case EdgeCurved:
        return {styleEdit("edgeStyle", "orthogonalEdgeStyle"), styleEdit("curved", "1")};
    case EdgeEntityRelation:
        return {styleEdit("edgeStyle", "entityRelationEdgeStyle"), styleEdit("curved", std::nullopt)};
    default:
        return {styleEdit("edgeStyle", std::nullopt), styleEdit("curved", std::nullopt)};
    }
}

int edgeStyleIndexOf(const StyleSummary& summary)
{
    const StyleValue edgeStyle = summary.value("edgeStyle");
    if (edgeStyle.mixed)
        return -1;
    if (!edgeStyle.value)
        return EdgeStraight;
    const std::string& value = *edgeStyle.value;
    if (value == "orthogonalEdgeStyle")
        return summary.flag("curved", false).value_or(false) ? EdgeCurved : EdgeOrthogonal;
    if (value == "elbowEdgeStyle")
        return EdgeElbow;
    if (value == "isometricEdgeStyle")
        return EdgeIsometric;
    if (value == "entityRelationEdgeStyle")
        return EdgeEntityRelation;
    return EdgeStraight;
}

// --- line ends (draw.io's addLineEnd menus) -------------------------------------

struct MarkerOption {
    const char* value;
    const char* label;
};

constexpr MarkerOption kMarkers[] = {
    {"none", "None"},
    {"classic", "Classic"},
    {"classicThin", "Classic Thin"},
    {"block", "Block"},
    {"blockThin", "Block Thin"},
    {"open", "Open"},
    {"openThin", "Open Thin"},
    {"oval", "Oval"},
    {"diamond", "Diamond"},
    {"diamondThin", "Diamond Thin"},
    {"dash", "Dash"},
    {"ERone", "ERone"},
    {"ERmany", "ERmany"},
    {"ERmandOne", "ERmandOne"},
    {"ERzeroToOne", "ERzeroToOne"},
    {"ERzeroToMany", "ERzeroToMany"},
    {"ERoneToMany", "ERoneToMany"},
};

constexpr int kMarkerCount = static_cast<int>(std::size(kMarkers));

int markerIndexOf(const StyleValue& value)
{
    if (value.mixed)
        return -1;
    if (!value.value)
        return 0; // draw.io's default: no line end
    for (int i = 0; i < kMarkerCount; ++i) {
        if (*value.value == kMarkers[i].value)
            return i;
    }
    return 0;
}

// Marker combo box for start or end: writes `<key>` on the selected edges.
QComboBox* createMarkerCombo(const QString& objectName)
{
    auto* combo = new QComboBox;
    combo->setObjectName(objectName);
    for (const MarkerOption& marker : kMarkers)
        combo->addItem(formatTr(marker.label), QString::fromLatin1(marker.value));
    return combo;
}

// Gradient directions (draw.io mxConstants.DIRECTION_*).
constexpr const char* kGradientDirections[] = {"north", "east", "south", "west", "radial"};
constexpr const char* kGradientDefault = "south";

class StyleTab final : public FormatTab {
public:
    StyleTab(DocumentSession& session, const QList<QAction*>& styleActions);

    void refresh(const StyleSummary& summary) override;

private:
    void buildFill(QVBoxLayout* layout);
    void buildLine(QVBoxLayout* layout);
    void buildEffects(QVBoxLayout* layout);
    void buildEdge(QVBoxLayout* layout);
    void buildStyleButtons(QVBoxLayout* layout, const QList<QAction*>& styleActions);
    void buildFlag(QFormLayout* form, const char* objectName, const QString& label, const char* key,
                   CellFilter filter, QCheckBox*& box);

    QGroupBox* fillGroup_ = nullptr;
    QGroupBox* lineGroup_ = nullptr;
    QGroupBox* effectsGroup_ = nullptr;
    QGroupBox* edgeGroup_ = nullptr;

    ColorField* fill_ = nullptr;
    ColorField* gradient_ = nullptr;
    QComboBox* gradientDirection_ = nullptr;
    ColorField* stroke_ = nullptr;
    QSpinBox* strokeWidth_ = nullptr;
    QComboBox* dash_ = nullptr;
    QCheckBox* rounded_ = nullptr;
    QCheckBox* shadow_ = nullptr;
    QCheckBox* glass_ = nullptr;
    QCheckBox* sketch_ = nullptr;
    QSpinBox* opacity_ = nullptr;
    QComboBox* edgeStyle_ = nullptr;
    QComboBox* startArrow_ = nullptr;
    QComboBox* endArrow_ = nullptr;
    QCheckBox* startFill_ = nullptr;
    QCheckBox* endFill_ = nullptr;
    QSpinBox* startSize_ = nullptr;
    QSpinBox* endSize_ = nullptr;
};

StyleTab::StyleTab(DocumentSession& session, const QList<QAction*>& styleActions) : FormatTab(session)
{
    buildFill(contentLayout());
    buildLine(contentLayout());
    buildEffects(contentLayout());
    buildEdge(contentLayout());
    buildStyleButtons(contentLayout(), styleActions);
    contentLayout()->addStretch();
}

void StyleTab::buildFill(QVBoxLayout* layout)
{
    QFormLayout* form = addFormGroup(layout, formatTr("Fill"), &fillGroup_);

    fill_ = new ColorField;
    fill_->setObjectName(QStringLiteral("format.fillColor"));
    fill_->setPurpose(formatTr("Fill Color"));
    form->addRow(formatTr("Color"), fill_);
    QObject::connect(fill_, &ColorField::edited, fill_, [this](std::string value) {
        writeStyles(session(), formatTr("Fill Color"), CellFilter::Vertices, {styleEdit("fillColor", value)});
    });

    gradient_ = new ColorField;
    gradient_->setObjectName(QStringLiteral("format.gradientColor"));
    gradient_->setPurpose(formatTr("Gradient Color"));
    form->addRow(formatTr("Gradient"), gradient_);
    QObject::connect(gradient_, &ColorField::edited, gradient_, [this](std::string value) {
        writeStyles(session(), formatTr("Gradient Color"), CellFilter::Vertices,
                    {styleEdit("gradientColor", value == "none" ? std::nullopt : std::optional<std::string>(value))});
    });

    gradientDirection_ = new QComboBox;
    gradientDirection_->setObjectName(QStringLiteral("format.gradientDirection"));
    gradientDirection_->addItem(formatTr("North"), QStringLiteral("north"));
    gradientDirection_->addItem(formatTr("East"), QStringLiteral("east"));
    gradientDirection_->addItem(formatTr("South"), QStringLiteral("south"));
    gradientDirection_->addItem(formatTr("West"), QStringLiteral("west"));
    gradientDirection_->addItem(formatTr("Radial"), QStringLiteral("radial"));
    form->addRow(formatTr("Direction"), gradientDirection_);
    onActivated(gradientDirection_, [this](int index) {
        writeStyles(session(), formatTr("Gradient Direction"), CellFilter::Vertices,
                    {styleEdit("gradientDirection", kGradientDirections[index])});
    });
}

void StyleTab::buildLine(QVBoxLayout* layout)
{
    QFormLayout* form = addFormGroup(layout, formatTr("Line"), &lineGroup_);

    stroke_ = new ColorField;
    stroke_->setObjectName(QStringLiteral("format.strokeColor"));
    stroke_->setPurpose(formatTr("Line Color"));
    form->addRow(formatTr("Color"), stroke_);
    QObject::connect(stroke_, &ColorField::edited, stroke_, [this](std::string value) {
        writeStyles(session(), formatTr("Line Color"), CellFilter::All, {styleEdit("strokeColor", value)});
    });

    strokeWidth_ = createIntSpin(nullptr, 0, 999);
    strokeWidth_->setObjectName(QStringLiteral("format.strokeWidth"));
    form->addRow(formatTr("Width"), strokeWidth_);
    onSpinEdited(strokeWidth_, session(), [this](double value) {
        writeStyles(session(), formatTr("Line Width"), CellFilter::All,
                    {styleEdit("strokeWidth", QString::number(value).toStdString())}, "strokeWidth");
    });

    dash_ = new QComboBox;
    dash_->setObjectName(QStringLiteral("format.dash"));
    dash_->addItem(formatTr("Solid"));
    dash_->addItem(formatTr("Dashed"));
    dash_->addItem(formatTr("Dotted"));
    form->addRow(formatTr("Pattern"), dash_);
    onActivated(dash_, [this](int index) {
        writeStyles(session(), formatTr("Line Pattern"), CellFilter::All, dashEdits(index));
    });

    buildFlag(form, "format.rounded", formatTr("Rounded"), "rounded", CellFilter::All, rounded_);
}

void StyleTab::buildEffects(QVBoxLayout* layout)
{
    QFormLayout* form = addFormGroup(layout, formatTr("Effects"), &effectsGroup_);

    buildFlag(form, "format.shadow", formatTr("Shadow"), "shadow", CellFilter::Vertices, shadow_);
    buildFlag(form, "format.glass", formatTr("Glass"), "glass", CellFilter::Vertices, glass_);
    buildFlag(form, "format.sketch", formatTr("Sketch"), "sketch", CellFilter::Vertices, sketch_);

    opacity_ = createIntSpin(nullptr, 0, 100);
    opacity_->setObjectName(QStringLiteral("format.opacity"));
    form->addRow(formatTr("Opacity"), opacity_);
    onSpinEdited(opacity_, session(), [this](double value) {
        writeStyles(session(), formatTr("Opacity"), CellFilter::Vertices,
                    {styleEdit("opacity", QString::number(value).toStdString())}, "opacity");
    });
}

void StyleTab::buildEdge(QVBoxLayout* layout)
{
    QFormLayout* form = addFormGroup(layout, formatTr("Edge"), &edgeGroup_);

    edgeStyle_ = new QComboBox;
    edgeStyle_->setObjectName(QStringLiteral("format.edgeStyle"));
    edgeStyle_->addItem(formatTr("Straight"));
    edgeStyle_->addItem(formatTr("Orthogonal"));
    edgeStyle_->addItem(formatTr("Simple elbow"));
    edgeStyle_->addItem(formatTr("Isometric"));
    edgeStyle_->addItem(formatTr("Curved"));
    edgeStyle_->addItem(formatTr("Entity relation"));
    form->addRow(formatTr("Routing"), edgeStyle_);
    onActivated(edgeStyle_, [this](int index) {
        writeStyles(session(), formatTr("Edge Style"), CellFilter::Edges, edgeStyleEdits(index));
    });

    startArrow_ = createMarkerCombo(QStringLiteral("format.startArrow"));
    form->addRow(formatTr("Start arrow"), startArrow_);
    onActivated(startArrow_, [this](int index) {
        writeStyles(session(), formatTr("Start Arrow"), CellFilter::Edges,
                    {styleEdit("startArrow", kMarkers[index].value)});
    });
    buildFlag(form, "format.startFill", formatTr("Start fill"), "startFill", CellFilter::Edges, startFill_);

    startSize_ = createIntSpin(nullptr, 1, 999);
    startSize_->setObjectName(QStringLiteral("format.startSize"));
    form->addRow(formatTr("Start size"), startSize_);
    onSpinEdited(startSize_, session(), [this](double value) {
        writeStyles(session(), formatTr("Start Size"), CellFilter::Edges,
                    {styleEdit("startSize", QString::number(value).toStdString())}, "startSize");
    });

    endArrow_ = createMarkerCombo(QStringLiteral("format.endArrow"));
    form->addRow(formatTr("End arrow"), endArrow_);
    onActivated(endArrow_, [this](int index) {
        writeStyles(session(), formatTr("End Arrow"), CellFilter::Edges, {styleEdit("endArrow", kMarkers[index].value)});
    });
    buildFlag(form, "format.endFill", formatTr("End fill"), "endFill", CellFilter::Edges, endFill_);

    endSize_ = createIntSpin(nullptr, 1, 999);
    endSize_->setObjectName(QStringLiteral("format.endSize"));
    form->addRow(formatTr("End size"), endSize_);
    onSpinEdited(endSize_, session(), [this](double value) {
        writeStyles(session(), formatTr("End Size"), CellFilter::Edges,
                    {styleEdit("endSize", QString::number(value).toStdString())}, "endSize");
    });
}

// Copy Style, Paste Style and Set as Default Style below the controls
// (Format.js addStyleOps). The actions are the main window's: same enable rules
// as the menu items. Without actions (a panel on its own) there are no buttons.
void StyleTab::buildStyleButtons(QVBoxLayout* layout, const QList<QAction*>& styleActions)
{
    if (styleActions.isEmpty())
        return;
    QWidget* row = createActionButtonGrid(styleActions, QStringLiteral("format.styleActions"));
    layout->addWidget(row);
}

// A check box that writes `key` as "1" / "0" on the selected cells of `filter`.
void StyleTab::buildFlag(QFormLayout* form, const char* objectName, const QString& label, const char* key,
                         CellFilter filter, QCheckBox*& box)
{
    box = new QCheckBox(label);
    box->setObjectName(QString::fromLatin1(objectName));
    form->addRow(box);
    onClicked(box, [this, key, filter, label](bool on) {
        writeStyles(session(), label, filter, {styleEdit(key, flagText(on))});
    });
}

void StyleTab::refresh(const StyleSummary& summary)
{
    fillGroup_->setVisible(summary.hasVertices());
    effectsGroup_->setVisible(summary.hasVertices());
    edgeGroup_->setVisible(summary.hasEdges());

    const StyleValue fill = summary.value("fillColor");
    fill_->setState(fill.value, fill.mixed);
    const StyleValue gradient = summary.value("gradientColor");
    gradient_->setState(gradient.value, gradient.mixed);
    const StyleValue direction = summary.value("gradientDirection");
    {
        QSignalBlocker blocker(gradientDirection_);
        int index = -1;
        if (!direction.mixed) {
            const std::string current = direction.value.value_or(kGradientDefault);
            for (int i = 0; i < 5; ++i) {
                if (current == kGradientDirections[i])
                    index = i;
            }
        }
        gradientDirection_->setCurrentIndex(index);
    }

    const StyleValue stroke = summary.value("strokeColor");
    stroke_->setState(stroke.value, stroke.mixed);
    setSpinState(strokeWidth_, summary.number("strokeWidth", 1));
    {
        QSignalBlocker blocker(dash_);
        dash_->setCurrentIndex(dashIndexOf(summary));
    }
    setCheckState(rounded_, summary.flag("rounded", false));
    setCheckState(shadow_, summary.flag("shadow", false));
    setCheckState(glass_, summary.flag("glass", false));
    setCheckState(sketch_, summary.flag("sketch", false));
    setSpinState(opacity_, summary.number("opacity", 100));

    {
        QSignalBlocker edgeBlocker(edgeStyle_);
        edgeStyle_->setCurrentIndex(edgeStyleIndexOf(summary));
    }
    {
        QSignalBlocker startBlocker(startArrow_);
        QSignalBlocker endBlocker(endArrow_);
        startArrow_->setCurrentIndex(markerIndexOf(summary.value("startArrow")));
        endArrow_->setCurrentIndex(markerIndexOf(summary.value("endArrow")));
    }
    setCheckState(startFill_, summary.flag("startFill", false));
    setCheckState(endFill_, summary.flag("endFill", false));
    setSpinState(startSize_, summary.number("startSize", render::kDefaultMarkerSize));
    setSpinState(endSize_, summary.number("endSize", render::kDefaultMarkerSize));
}

} // namespace

std::unique_ptr<FormatTab> makeStyleTab(DocumentSession& session, const QList<QAction*>& styleActions)
{
    return std::make_unique<StyleTab>(session, styleActions);
}

} // namespace qraw::ui
