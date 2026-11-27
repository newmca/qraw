// Widget themes (app_setup.h). The diagram canvas itself stays light in every
// theme (render.md section 10).
#include "qraw/ui/app_setup.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace qraw::ui {

namespace {

// The look the process started with, captured on the first applyTheme call so
// ThemeKind::System can restore it.
struct StartupLook {
    QString styleName;
    QPalette palette;
};

const StartupLook& startupLook(const QApplication& app)
{
    static const StartupLook look{app.style()->name(), app.palette()};
    return look;
}

void setStyle(QApplication& app, const QString& name)
{
    if (QStyle* style = QStyleFactory::create(name))
        app.setStyle(style);
}

QPalette darkPalette()
{
    const QColor window(0x2a, 0x2a, 0x2a);
    const QColor base(0x1e, 0x1e, 0x1e);
    const QColor text(0xe0, 0xe0, 0xe0);
    const QColor disabled(0x80, 0x80, 0x80);
    const QColor highlight(0x29, 0xb6, 0xf2);

    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, window);
    palette.setColor(QPalette::ToolTipBase, base);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, QColor(0x35, 0x35, 0x35));
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Link, highlight);
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, QColor(0x10, 0x10, 0x10));
    palette.setColor(QPalette::PlaceholderText, disabled);
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, disabled);
    return palette;
}

QPalette highContrastPalette()
{
    const QColor yellow(0xff, 0xff, 0x00);

    QPalette palette;
    palette.setColor(QPalette::Window, Qt::black);
    palette.setColor(QPalette::WindowText, Qt::white);
    palette.setColor(QPalette::Base, Qt::black);
    palette.setColor(QPalette::AlternateBase, QColor(0x10, 0x10, 0x10));
    palette.setColor(QPalette::ToolTipBase, Qt::black);
    palette.setColor(QPalette::ToolTipText, Qt::white);
    palette.setColor(QPalette::Text, Qt::white);
    palette.setColor(QPalette::Button, Qt::black);
    palette.setColor(QPalette::ButtonText, Qt::white);
    palette.setColor(QPalette::BrightText, yellow);
    palette.setColor(QPalette::Link, QColor(0x00, 0xff, 0xff));
    palette.setColor(QPalette::Highlight, yellow);
    palette.setColor(QPalette::HighlightedText, Qt::black);
    palette.setColor(QPalette::PlaceholderText, QColor(0xc0, 0xc0, 0xc0));
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, QColor(0xa0, 0xa0, 0xa0));
    return palette;
}

// Qt style sheets have no `outline`; the frame rule gives text inputs a
// visible 2 px focus frame, the generic rule is kept for styles that honour it.
QString highContrastStyleSheet()
{
    return QStringLiteral("QWidget:focus { outline: 2px solid #ffff00; }\n"
                          "QLineEdit:focus, QAbstractSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus,"
                          " QTextEdit:focus { border: 2px solid #ffff00; }\n"
                          "QToolTip { color: #ffffff; background-color: #000000; border: 1px solid #ffffff; }");
}

} // namespace

void applyTheme(QApplication& app, ThemeKind theme)
{
    const StartupLook& startup = startupLook(app);
    app.setStyleSheet(QString());

    switch (theme) {
    case ThemeKind::System:
        setStyle(app, startup.styleName);
        app.setPalette(startup.palette);
        break;
    case ThemeKind::Light:
        setStyle(app, QStringLiteral("Fusion"));
        app.setPalette(app.style()->standardPalette());
        break;
    case ThemeKind::Dark:
        setStyle(app, QStringLiteral("Fusion"));
        app.setPalette(darkPalette());
        break;
    case ThemeKind::HighContrast:
        setStyle(app, QStringLiteral("Fusion"));
        app.setPalette(highContrastPalette());
        app.setStyleSheet(highContrastStyleSheet());
        break;
    }
}

} // namespace qraw::ui
