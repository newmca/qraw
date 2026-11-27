// Checks the inline parts of the render public headers and that every header
// compiles on its own include set.

#include "qraw/format/embed.h"
#include "qraw/geometry/label_layout.h"
#include "qraw/geometry/path.h"
#include "qraw/geometry/perimeter.h"
#include "qraw/geometry/polyline.h"
#include "qraw/geometry/routing.h"
#include "qraw/render/canvas.h"
#include "qraw/render/export.h"
#include "qraw/render/image_source.h"
#include "qraw/render/markers.h"
#include "qraw/render/page_view.h"
#include "qraw/render/painter_canvas.h"
#include "qraw/render/resolved_style.h"
#include "qraw/render/scene_renderer.h"
#include "qraw/render/shape.h"
#include "qraw/render/stencil.h"
#include "qraw/render/stylesheet.h"
#include "qraw/render/svg_canvas.h"
#include "qraw/render/text.h"

#include "support/recording_canvas.h"

#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QTest>

using namespace qraw;

class DesignContractTest : public QObject {
    Q_OBJECT

private slots:
    void pathBuilderInvariants()
    {
        geometry::Path p;
        p.lineTo({1, 2}); // no current point: implicit moveTo
        QCOMPARE(p.elements().size(), std::size_t(2));
        QCOMPARE(p.elements()[0].kind, geometry::PathElement::Kind::MoveTo);
        p.lineTo({5, 2});
        p.close();
        QVERIFY(p.currentPoint().has_value());
        QCOMPARE(p.currentPoint()->x, 1.0); // close returns to the subpath start
        geometry::Path q;
        q.append(p);
        QVERIFY(q == p);
    }

    void constexprParsers()
    {
        QCOMPARE(geometry::parseDirection("north"), geometry::Direction::North);
        QCOMPARE(geometry::parseDirection("bogus"), geometry::Direction::East);
        QCOMPARE(geometry::parseHAlign("right", geometry::HAlign::Center), geometry::HAlign::Right);
        QCOMPARE(geometry::parseVAlign("", geometry::VAlign::Top), geometry::VAlign::Top);
        QCOMPARE(geometry::parseOverflow("fill"), geometry::Overflow::Fill);
        QCOMPARE(render::parseLineCap("round"), render::LineCap::Round);
        QCOMPARE(render::parseGradientDirection("radial"), render::GradientDirection::Radial);
    }

    void resolvedStyleAccessors()
    {
        render::StyleMap m;
        m.set("strokeWidth", "2.5");
        m.set("dashed", "1");
        m.set("fillColor", "#ff0000");
        m.set("strokeColor", "none");
        m.set("exitX", "1");
        m.set("exitY", "0.5");
        m.set("jettySize", "auto");
        const render::ResolvedStyle vertex(m, false);
        QCOMPARE(vertex.strokeWidth(), 2.5);
        QVERIFY(vertex.dashed());
        QCOMPARE(*vertex.fillColor(), QColor(255, 0, 0));
        QVERIFY(!vertex.strokeColor());
        QVERIFY(!vertex.gradientColor());
        QCOMPARE(vertex.arcSize(), 15.0);
        QCOMPARE(vertex.fontSize(), render::kDefaultFontSize);
        QVERIFY(!vertex.jettySize());
        const auto c = vertex.constraint(true);
        QVERIFY(c && c->point.x == 1 && c->point.y == 0.5 && c->perimeter);
        QVERIFY(!vertex.constraint(false));
        const render::ResolvedStyle edge({}, true);
        QCOMPARE(edge.arcSize(), geometry::kLineArcSize);
        QCOMPARE(edge.endMarkerSize(), render::kDefaultMarkerSize);
        QVERIFY(edge.endFill());
    }

    void vendoredResourcesAreCompiledIn()
    {
        QFile stylesheet(QStringLiteral(":/qraw/styles/default.xml"));
        QVERIFY(stylesheet.open(QIODevice::ReadOnly));
        QVERIFY(stylesheet.readAll().startsWith("<mxStylesheet>"));
    }

    // Tile rasterisation runs on worker threads (ARCHITECTURE.md, "Threading rules"):
    // QPixmap, QIcon and widgets are GUI-thread only, so code that can be reached
    // from a tile job must use QImage. src/render uses none of them.
    void renderModuleUsesNoGuiThreadOnlyClasses()
    {
        const QRegularExpression forbidden(QStringLiteral(R"(\bQ(Pixmap|PixmapCache|Icon|Bitmap|Widget|GraphicsItem|GraphicsScene)\b)"));
        int scanned = 0;
        QDirIterator it(QStringLiteral(QRAW_SOURCE_DIR "/src/render"), {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile file(it.next());
            QVERIFY(file.open(QIODevice::ReadOnly));
            ++scanned;
            int lineNo = 0;
            while (!file.atEnd()) {
                const QString line = QString::fromUtf8(file.readLine());
                ++lineNo;
                if (line.trimmed().startsWith(QLatin1String("//")))
                    continue; // the contract is quoted in comments
                QVERIFY2(!forbidden.match(line).hasMatch(),
                         qPrintable(QStringLiteral("%1:%2 uses a GUI-thread-only class").arg(file.fileName()).arg(lineNo)));
            }
        }
        QVERIFY(scanned > 20);
    }

    void recordingCanvasFormat()
    {
        test::RecordingCanvas c;
        c.begin();
        c.moveTo(1.5, 2);
        c.setFillColor(QColor(0, 0, 255));
        c.fillAndStroke();
        QCOMPARE(QString::fromStdString(c.text()), QStringLiteral("begin\nmoveTo 1.5 2\nsetFillColor #0000ff\nfillAndStroke\n"));
        QCOMPARE(c.paints, 1);
    }
};

QTEST_MAIN(DesignContractTest)
#include "tst_design_contract.moc"
