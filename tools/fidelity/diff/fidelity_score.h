// Scoring of one rendering against a reference rendering (the fidelity diff,
// tools/fidelity). Plain QImage code, no Qraw dependencies, so that the
// scoring can be unit-tested on synthetic images.
//
// Method, per image pair:
//   1. Flatten transparency over white and find each image's content box (the
//      bounding box of everything that is not background white).
//   2. Record the content box sizes and their mismatch separately: it measures
//      geometry/bounds differences, not rendering differences.
//   3. Crop both to their content boxes, pad to a common canvas and align them
//      top-left or centred, whichever gives the smaller difference, then nudge the
//      candidate by up to one pixel in each direction (hairline and bounds rounding).
//   4. Average 2x2 blocks (forgives antialiasing and hinting) and compare each
//      block in CIE L*a*b* (CIE76 deltaE). A block differs when deltaE exceeds
//      the threshold.
//   5. diffShare = differing blocks / blocks that hold content in either image. The same
//      comparison on coarse blocks (8x8 by default) gives coarseShare, which text rendering
//      differences barely move but a missing, moved or differently drawn shape does; the
//      layout score `combined` is built from it and ranks the report.
#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>

namespace qraw::fidelity {

struct Options {
    double deltaEThreshold = 10.0;  // CIE76 deltaE above which a block counts as different
    int backgroundTolerance = 2;    // per-channel distance from white still counted as background
    int coarseBlock = 8;            // block size of the layout comparison
};

enum class Alignment { TopLeft, Centre };

struct Score {
    bool valid = false;           // false when an image was empty (no content at all)
    QSize referenceSize;          // content box size of the reference
    QSize candidateSize;          // content box size of the candidate
    QSize canvasSize;             // padded common size the comparison ran on
    double sizeMismatch = 0.0;    // 1 - overlap of the two content boxes anchored top-left, 0..1
    Alignment alignment = Alignment::TopLeft;
    QPoint offset;                // where the candidate sits relative to the reference on the canvas (includes the 1 px nudge)
    double diffShare = 0.0;       // differing 2x2 blocks / content blocks, 0..1 (pixel level: text antialiasing counts)
    double coarseShare = 0.0;     // the same on coarse blocks: forgives text and antialiasing, sees moved or missing shapes
    double meanDeltaE = 0.0;      // mean deltaE over the content blocks
    double inkRatio = 1.0;        // candidate ink / reference ink (below 1: content missing, above: extra)
    double pixelCombined = 0.0;   // 1 - (1 - diffShare) * (1 - sizeMismatch)
    double combined = 0.0;        // 1 - (1 - coarseShare) * (1 - sizeMismatch), the layout score: the ranking key

    // Kept for the report; not part of the score.
    QImage referenceCanvas;       // aligned, padded reference (RGB32)
    QImage candidateCanvas;       // aligned, padded candidate (RGB32)
    QImage heatMap;               // RGB32 of the same size as the canvases
};

// Over white, as RGB32.
QImage flattenOverWhite(const QImage& image);

// Bounding box of the non-background pixels of an RGB32 image; null rect when there are none.
QRect contentBox(const QImage& flattened, int backgroundTolerance);

Score compare(const QImage& reference, const QImage& candidate, const Options& options = {});

// reference | candidate | heat map with a caption strip, scaled to fit within maxPanel.
QImage makeComposite(const Score& score, const QString& caption, const QSize& maxPanel = {900, 1400});

}  // namespace qraw::fidelity
