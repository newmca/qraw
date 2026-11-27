// The Diagram tab of the Format panel: the page settings shown when nothing is
// selected (draw.io's DiagramFormatPanel, Format.js:9560, addGridOption :9809,
// addPaperSize :9937).
#include "format_controls.h"
#include "format_edit.h"
#include "format_tab.h"

#include "qraw/commands/ops_pages.h"
#include "qraw/ui/document_session.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QSignalBlocker>

#include <algorithm>
#include <iterator>

namespace qraw::ui {
namespace {

// Paper sizes in pixels, portrait (PageSetupDialog.getFormats,
// js/grapheditor/Editor.js:4358).
struct PaperSize {
    const char* name;
    double width;
    double height;
};

constexpr PaperSize kPaperSizes[] = {
    {"A3", 1169, 1654},  {"A4", 827, 1169},   {"A5", 583, 827},
    {"Letter", 850, 1100}, {"Legal", 850, 1400}, {"Tabloid", 1100, 1700},
};

// Index of the "Custom" entry of the paper size combo box.
constexpr int kCustomPaper = static_cast<int>(std::size(kPaperSizes));

// The preset matching the page size in either orientation, else "Custom".
int paperIndexFor(double width, double height)
{
    const double shortSide = std::min(width, height);
    const double longSide = std::max(width, height);
    for (int i = 0; i < kCustomPaper; ++i) {
        if (kPaperSizes[i].width == shortSide && kPaperSizes[i].height == longSide)
            return i;
    }
    return kCustomPaper;
}

class DiagramTab final : public FormatTab {
public:
    DiagramTab(DocumentSession& session, const QList<QAction*>& dataActions);

    void refresh(const StyleSummary& summary) override;

private:
    void buildView(QVBoxLayout* layout);
    void buildPaper(QVBoxLayout* layout);
    // Swaps width and height so that the page is portrait or landscape.
    void writeOrientation(bool portrait);
    void setCurrentUnit();

    QCheckBox* grid_ = nullptr;
    LengthSpinBox* gridSize_ = nullptr;
    QCheckBox* pageView_ = nullptr;
    ColorField* background_ = nullptr;
    QComboBox* adaptive_ = nullptr;
    QCheckBox* shadow_ = nullptr;
    QCheckBox* math_ = nullptr;
    QComboBox* paperSize_ = nullptr;
    QRadioButton* portrait_ = nullptr;
    QRadioButton* landscape_ = nullptr;
    LengthSpinBox* pageWidth_ = nullptr;
    LengthSpinBox* pageHeight_ = nullptr;
    QComboBox* units_ = nullptr;
};

DiagramTab::DiagramTab(DocumentSession& session, const QList<QAction*>& dataActions) : FormatTab(session)
{
    buildView(contentLayout());
    buildPaper(contentLayout());
    // Format.js:9985 DiagramFormatPanel: Edit Data, which edits the diagram's own data here.
    if (!dataActions.isEmpty()) {
        QFormLayout* form = addFormGroup(contentLayout(), formatTr("Data"));
        form->addRow(createActionButtonGrid({dataActions.first()}, QStringLiteral("format.diagramData")));
    }
    contentLayout()->addStretch();
}

void DiagramTab::buildView(QVBoxLayout* layout)
{
    QFormLayout* form = addFormGroup(layout, formatTr("View"));

    // draw.io's Units selector (View > Units, and the page setup): a preference that
    // changes how the length fields read, not the diagram.
    units_ = new QComboBox;
    units_->setObjectName(QStringLiteral("format.units"));
    units_->setToolTip(formatTr("The unit of the length fields. The diagram itself is always stored in pixels."));
    for (const MeasureUnit unit : kAllMeasureUnits)
        units_->addItem(units::label(unit), static_cast<int>(unit));
    form->addRow(formatTr("Units"), units_);
    onActivated(units_, [this](int index) {
        MeasureUnits::instance().setUnit(static_cast<MeasureUnit>(units_->itemData(index).toInt()));
    });
    QObject::connect(&MeasureUnits::instance(), &MeasureUnits::unitChanged, units_, [this] { setCurrentUnit(); });

    grid_ = new QCheckBox(formatTr("Grid"));
    grid_->setObjectName(QStringLiteral("format.grid"));
    form->addRow(grid_);
    onClicked(grid_, [this](bool on) {
        commands::ops::PageProperties props;
        props.grid = on;
        applyPageProperties(session(), formatTr("Grid"), props);
    });

    gridSize_ = new LengthSpinBox(1, 100);
    gridSize_->setExact(true); // a grid in millimeters has to be a whole number of them
    gridSize_->setObjectName(QStringLiteral("format.gridSize"));
    form->addRow(formatTr("Grid size"), gridSize_);
    onLengthEdited(gridSize_, session(), [this](double value) {
        commands::ops::PageProperties props;
        props.gridSize = value;
        applyPageProperties(session(), formatTr("Grid Size"), props, "format:gridSize:");
    });

    pageView_ = new QCheckBox(formatTr("Page view"));
    pageView_->setObjectName(QStringLiteral("format.pageView"));
    form->addRow(pageView_);
    onClicked(pageView_, [this](bool on) {
        commands::ops::PageProperties props;
        props.pageVisible = on;
        applyPageProperties(session(), formatTr("Page View"), props);
    });

    background_ = new ColorField;
    background_->setObjectName(QStringLiteral("format.background"));
    background_->setPurpose(formatTr("Background Color"));
    form->addRow(formatTr("Background"), background_);
    QObject::connect(background_, &ColorField::edited, background_, [this](std::string value) {
        commands::ops::PageProperties props;
        props.background = value == "none" ? std::string() : value;
        applyPageProperties(session(), formatTr("Background Color"), props);
    });

    // How the diagram looks in the dark scheme (View > Dark Diagram), Editor.js adaptiveColors.
    adaptive_ = new QComboBox;
    adaptive_->setObjectName(QStringLiteral("format.adaptiveColors"));
    adaptive_->setToolTip(formatTr("How colors change in the dark scheme: Automatic inverts every color, Simple "
                                   "changes only the default colors, None keeps the diagram light."));
    adaptive_->addItem(formatTr("Default (Automatic)"), QStringLiteral("default"));
    adaptive_->addItem(formatTr("Automatic"), QStringLiteral("auto"));
    adaptive_->addItem(formatTr("Simple"), QStringLiteral("simple"));
    adaptive_->addItem(formatTr("None"), QStringLiteral("none"));
    form->addRow(formatTr("Adaptive colors"), adaptive_);
    onActivated(adaptive_, [this](int index) {
        commands::ops::PageProperties props;
        props.adaptiveColors = adaptive_->itemData(index).toString().toStdString();
        applyPageProperties(session(), formatTr("Adaptive Colors"), props);
    });

    shadow_ = new QCheckBox(formatTr("Page shadow"));
    shadow_->setObjectName(QStringLiteral("format.pageShadow"));
    form->addRow(shadow_);
    onClicked(shadow_, [this](bool on) {
        commands::ops::PageProperties props;
        props.shadow = on;
        applyPageProperties(session(), formatTr("Shadow"), props);
    });

    math_ = new QCheckBox(formatTr("Math typesetting"));
    math_->setObjectName(QStringLiteral("format.math"));
    form->addRow(math_);
    onClicked(math_, [this](bool on) {
        commands::ops::PageProperties props;
        props.math = on;
        applyPageProperties(session(), formatTr("Math"), props);
    });
}

void DiagramTab::buildPaper(QVBoxLayout* layout)
{
    QFormLayout* form = addFormGroup(layout, formatTr("Paper"));

    paperSize_ = new QComboBox;
    paperSize_->setObjectName(QStringLiteral("format.paperSize"));
    for (const PaperSize& paper : kPaperSizes)
        paperSize_->addItem(QString::fromLatin1(paper.name));
    paperSize_->addItem(formatTr("Custom"));
    form->addRow(formatTr("Size"), paperSize_);
    onActivated(paperSize_, [this](int index) {
        if (index >= kCustomPaper)
            return;
        const PaperSize& paper = kPaperSizes[index];
        const bool portrait = portrait_->isChecked();
        commands::ops::PageProperties props;
        props.pageWidth = portrait ? paper.width : paper.height;
        props.pageHeight = portrait ? paper.height : paper.width;
        applyPageProperties(session(), formatTr("Paper Size"), props);
    });

    portrait_ = new QRadioButton(formatTr("Portrait"));
    portrait_->setObjectName(QStringLiteral("format.portrait"));
    landscape_ = new QRadioButton(formatTr("Landscape"));
    landscape_->setObjectName(QStringLiteral("format.landscape"));
    auto* orientation = new QWidget;
    auto* row = new QHBoxLayout(orientation);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(portrait_);
    row->addWidget(landscape_);
    form->addRow(formatTr("Orientation"), orientation);
    onClicked(portrait_, [this](bool) { writeOrientation(true); });
    onClicked(landscape_, [this](bool) { writeOrientation(false); });

    pageWidth_ = new LengthSpinBox(1, 20000);
    pageWidth_->setObjectName(QStringLiteral("format.pageWidth"));
    pageHeight_ = new LengthSpinBox(1, 20000);
    pageHeight_->setObjectName(QStringLiteral("format.pageHeight"));
    form->addRow(formatTr("Width"), pageWidth_);
    form->addRow(formatTr("Height"), pageHeight_);
    for (LengthSpinBox* spin : {pageWidth_, pageHeight_}) {
        onLengthEdited(spin, session(), [this](double) {
            commands::ops::PageProperties props;
            props.pageWidth = pageWidth_->pixels();
            props.pageHeight = pageHeight_->pixels();
            applyPageProperties(session(), formatTr("Page Size"), props, "format:pageSize:");
        });
    }
}

// Shows the unit in use.
void DiagramTab::setCurrentUnit()
{
    QSignalBlocker blocker(units_);
    units_->setCurrentIndex(units_->findData(static_cast<int>(MeasureUnits::instance().unit())));
}

void DiagramTab::writeOrientation(bool portrait)
{
    const core::Page* page = session().currentPage();
    if (page == nullptr)
        return;
    const double shortSide = std::min(page->pageWidth(), page->pageHeight());
    const double longSide = std::max(page->pageWidth(), page->pageHeight());
    commands::ops::PageProperties props;
    props.pageWidth = portrait ? shortSide : longSide;
    props.pageHeight = portrait ? longSide : shortSide;
    applyPageProperties(session(), formatTr("Page Orientation"), props);
}

void DiagramTab::refresh(const StyleSummary& summary)
{
    Q_UNUSED(summary);
    const core::Page* page = session().currentPage();
    if (page == nullptr)
        return;

    setCheckState(grid_, page->gridEnabled());
    gridSize_->setPixels(page->gridSize());
    setCurrentUnit();
    setCheckState(pageView_, page->pageVisible());
    setCheckState(shadow_, page->shadow());
    setCheckState(math_, page->math());
    background_->setState(page->background(), false);
    {
        QSignalBlocker adaptiveBlocker(adaptive_);
        const int index = adaptive_->findData(QString::fromStdString(page->adaptiveColors()));
        adaptive_->setCurrentIndex(index >= 0 ? index : 0);
    }

    const double width = page->pageWidth();
    const double height = page->pageHeight();
    const int paper = paperIndexFor(width, height);
    {
        QSignalBlocker paperBlocker(paperSize_);
        QSignalBlocker portraitBlocker(portrait_);
        QSignalBlocker landscapeBlocker(landscape_);
        paperSize_->setCurrentIndex(paper);
        const bool portrait = height >= width;
        portrait_->setChecked(portrait);
        landscape_->setChecked(!portrait);
    }
    pageWidth_->setPixels(width);
    pageHeight_->setPixels(height);
    pageWidth_->setEnabled(paper == kCustomPaper);
    pageHeight_->setEnabled(paper == kCustomPaper);
}

} // namespace

std::unique_ptr<FormatTab> makeDiagramTab(DocumentSession& session, const QList<QAction*>& dataActions)
{
    return std::make_unique<DiagramTab>(session, dataActions);
}

} // namespace qraw::ui
