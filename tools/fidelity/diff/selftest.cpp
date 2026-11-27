// Self-test of the fidelity scoring on synthetic images: the scoring must be
// trustworthy before its numbers are quoted in docs/FIDELITY.md.

#include "fidelity_score.h"

#include <QPainter>
#include <QtTest>

using namespace qraw::fidelity;

namespace {

QImage canvas(int w, int h)
{
    QImage image(w, h, QImage::Format_ARGB32);
    image.fill(Qt::white);
    return image;
}

// A small diagram: boxes, an ellipse and a line.
QImage diagram(QPoint origin = {0, 0}, bool withEllipse = true, QSize size = {400, 300})
{
    QImage image = canvas(size.width(), size.height());
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(origin);
    p.setPen(QPen(Qt::black, 2));
    p.setBrush(QColor(0xdae8fc));
    p.drawRect(30, 30, 120, 60);
    p.drawRect(230, 30, 120, 60);
    p.drawLine(150, 60, 230, 60);
    if (withEllipse) {
        p.setBrush(QColor(0xf8cecc));
        p.drawEllipse(QRect(120, 160, 160, 90));
    }
    return image;
}

}  // namespace

class TstFidelityScore : public QObject {
    Q_OBJECT
private slots:
    void identicalImagesScoreZero()
    {
        const Score s = compare(diagram(), diagram());
        QVERIFY(s.valid);
        QCOMPARE(s.diffShare, 0.0);
        QCOMPARE(s.sizeMismatch, 0.0);
        QCOMPARE(s.combined, 0.0);
        QCOMPARE(s.inkRatio, 1.0);
    }

    void contentBoxIgnoresMargins()
    {
        // The same drawing placed differently on differently sized pages is identical.
        const Score s = compare(diagram({0, 0}, true, {400, 300}), diagram({60, 40}, true, {600, 500}));
        QVERIFY(s.valid);
        QCOMPARE(s.combined, 0.0);
        QCOMPARE(s.referenceSize, s.candidateSize);
    }

    void transparentBackgroundIsFlattenedOverWhite()
    {
        QImage transparent(diagram().size(), QImage::Format_ARGB32);
        transparent.fill(Qt::transparent);
        QPainter p(&transparent);
        p.drawImage(0, 0, diagram());  // opaque white background drawn over transparency
        const Score s = compare(diagram(), transparent);
        QCOMPARE(s.combined, 0.0);
    }

    void oneAntialiasedPixelShiftIsSmall()
    {
        QImage reference = diagram();
        QImage shifted = canvas(reference.width(), reference.height());
        QPainter p(&shifted);
        p.drawImage(1, 0, reference);
        p.end();
        // Anchoring on the content boxes removes a pure shift; add a different 1 px jitter inside.
        QImage jittered = diagram();
        {
            QPainter q(&jittered);
            q.setPen(QPen(Qt::black, 2));
            q.drawLine(150, 61, 230, 61);  // the connector one pixel lower
        }
        const Score s = compare(reference, jittered);
        QVERIFY(s.diffShare < 0.05);
        QVERIFY(s.combined < 0.05);
        QCOMPARE(compare(reference, shifted).combined, 0.0);
    }

    void aMissingShapeIsLarge()
    {
        const Score s = compare(diagram({0, 0}, true), diagram({0, 0}, false));
        QVERIFY(s.valid);
        QVERIFY2(s.combined > 0.15, qPrintable(QString::number(s.combined)));
        QVERIFY(s.inkRatio < 0.9);
    }

    void aSizeMismatchIsReportedSeparately()
    {
        QImage small = canvas(200, 100), large = canvas(400, 200);
        for (QImage* image : {&small, &large}) {
            QPainter p(image);
            p.fillRect(QRect(0, 0, image->width(), image->height()), Qt::black);
        }
        const Score s = compare(large, small);
        QCOMPARE(s.referenceSize, QSize(400, 200));
        QCOMPARE(s.candidateSize, QSize(200, 100));
        QVERIFY(qAbs(s.sizeMismatch - 0.75) < 1e-9);
        QVERIFY(s.diffShare > 0.0);
        QVERIFY(s.combined >= s.sizeMismatch);
    }

    void centreAlignmentIsChosenWhenBetter()
    {
        // The same ellipse inside a taller candidate box: centred, they match, top-left they do not.
        QImage a = canvas(200, 100), b = canvas(200, 160);
        {
            QPainter p(&a);
            p.setBrush(Qt::red);
            p.drawEllipse(QRect(20, 20, 160, 60));
            QPainter q(&b);
            q.setBrush(Qt::red);
            q.drawEllipse(QRect(20, 50, 160, 60));
            q.setPen(Qt::NoPen);
            q.setBrush(Qt::blue);
            q.drawRect(20, 5, 160, 2);   // thin stripe only the candidate has, 5 px above the ellipse
            q.drawRect(20, 150, 160, 2); // and one below, so its box is symmetric around the ellipse
        }
        const Score s = compare(a, b);
        QVERIFY(s.valid);
        QCOMPARE(s.alignment, Alignment::Centre);
    }

    void layoutScoreForgivesTextLikeJitter()
    {
        // Dense one-pixel strokes (text-like) drawn in opposite phase, which no one-pixel nudge repairs: every pixel differs, the layout does not.
        QImage a = canvas(240, 120), b = canvas(240, 120);
        for (QImage* image : {&a, &b}) {  // a frame pins both content boxes
            QPainter frame(image);
            frame.setPen(Qt::black);
            frame.drawRect(5, 5, 230, 110);
        }
        // Two-pixel bars every four pixels; b's bars sit two pixels lower (opposite phase).
        QPainter pa(&a), pb(&b);
        for (int y = 12; y < 106; y += 4) {
            pa.fillRect(10, y, 220, 2, Qt::black);
            pb.fillRect(10, y + 2, 220, 2, Qt::black);
        }
        pa.end();
        pb.end();
        const Score s = compare(a, b);
        QVERIFY2(s.pixelCombined > 0.3, qPrintable(QString::number(s.pixelCombined)));
        // Block edges still cut some strokes, so the layout score is not zero, but far below the pixel score.
        QVERIFY2(s.combined < s.pixelCombined * 0.5, qPrintable(QString::number(s.combined)));
    }

    void aOnePixelNudgeIsForgiven()
    {
        // A drawing whose second half sits one pixel lower: the nudge aligns the whole image, but
        // here the first half stays put, so only the unshifted version scores zero.
        QImage a = diagram();
        QImage b = canvas(a.width(), a.height() + 1);
        QPainter p(&b);
        p.drawImage(0, 0, a.copy(0, 0, a.width(), 120));
        p.drawImage(0, 121, a.copy(0, 120, a.width(), a.height() - 120));
        p.end();
        const Score s = compare(a, b);
        QVERIFY(s.diffShare < 0.1);
        QCOMPARE(compare(a, a).offset, QPoint(0, 0));
    }

    void blankImages()
    {
        QCOMPARE(compare(canvas(50, 50), canvas(60, 40)).combined, 0.0);
        const Score oneBlank = compare(diagram(), canvas(50, 50));
        QVERIFY(!oneBlank.valid);
        QCOMPARE(oneBlank.combined, 1.0);
    }

    void compositeHasThreePanels()
    {
        const Score s = compare(diagram(), diagram({0, 0}, false));
        const QImage composite = makeComposite(s, QStringLiteral("test"));
        // Three panels (the canvas is small enough not to be scaled) plus gaps and a caption strip.
        QVERIFY(composite.width() >= 3 * s.canvasSize.width());
        QVERIFY(composite.height() > s.canvasSize.height());
        QCOMPARE(s.heatMap.size(), s.referenceCanvas.size());
    }
};

QTEST_MAIN(TstFidelityScore)
#include "selftest.moc"
