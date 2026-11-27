#include "fidelity_score.h"

#include <QColor>
#include <QFont>
#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace qraw::fidelity {
namespace {

struct Lab {
    float l, a, b;
};

// sRGB (0..255) to CIE L*a*b*, D65.
Lab toLab(float r8, float g8, float b8)
{
    static const std::array<float, 256> linear = [] {
        std::array<float, 256> table{};
        for (int i = 0; i < 256; ++i) {
            const double c = i / 255.0;
            table[static_cast<size_t>(i)] =
                static_cast<float>(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
        }
        return table;
    }();
    // Block averages are fractional: interpolate the table.
    auto lin = [&](float v) {
        const float clamped = std::clamp(v, 0.0f, 255.0f);
        const int lo = static_cast<int>(clamped);
        const int hi = std::min(lo + 1, 255);
        const float t = clamped - static_cast<float>(lo);
        return linear[static_cast<size_t>(lo)] * (1 - t) + linear[static_cast<size_t>(hi)] * t;
    };
    const float r = lin(r8), g = lin(g8), b = lin(b8);
    const float x = (0.4124564f * r + 0.3575761f * g + 0.1804375f * b) / 0.95047f;
    const float y = 0.2126729f * r + 0.7151522f * g + 0.0721750f * b;
    const float z = (0.0193339f * r + 0.1191920f * g + 0.9503041f * b) / 1.08883f;
    auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.0f / 116.0f; };
    const float fx = f(x), fy = f(y), fz = f(z);
    return {116.0f * fy - 16.0f, 500.0f * (fx - fy), 200.0f * (fy - fz)};
}

float deltaE(const Lab& p, const Lab& q)
{
    const float dl = p.l - q.l, da = p.a - q.a, db = p.b - q.b;
    return std::sqrt(dl * dl + da * da + db * db);
}

// Block x block averages of an RGB32 image (a partial last row or column is repeated), plus a content flag per block.
struct Blocks {
    int width = 0, height = 0;
    std::vector<Lab> lab;
    std::vector<unsigned char> content;  // any pixel of the block was not background
    double ink = 0.0;                    // sum of (255 - luma) / 255 over all pixels
};

Blocks downsample(const QImage& image, int backgroundTolerance, int block)
{
    Blocks out;
    out.width = (image.width() + block - 1) / block;
    out.height = (image.height() + block - 1) / block;
    out.lab.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height));
    out.content.assign(out.lab.size(), 0);
    const int floor = 255 - backgroundTolerance;
    for (int by = 0; by < out.height; ++by) {
        for (int bx = 0; bx < out.width; ++bx) {
            float sr = 0, sg = 0, sb = 0;
            int count = 0;
            bool content = false;
            for (int dy = 0; dy < block; ++dy) {
                const int y = std::min(by * block + dy, image.height() - 1);  // partial edge block: repeat the last row
                const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
                for (int dx = 0; dx < block; ++dx) {
                    const QRgb px = row[std::min(bx * block + dx, image.width() - 1)];
                    const int r = qRed(px), g = qGreen(px), b = qBlue(px);
                    sr += static_cast<float>(r);
                    sg += static_cast<float>(g);
                    sb += static_cast<float>(b);
                    ++count;
                    if (r < floor || g < floor || b < floor)
                        content = true;
                }
            }
            const size_t index = static_cast<size_t>(by) * static_cast<size_t>(out.width) + static_cast<size_t>(bx);
            const float n = static_cast<float>(count);
            out.lab[index] = toLab(sr / n, sg / n, sb / n);
            out.content[index] = content ? 1 : 0;
        }
    }
    for (int y = 0; y < image.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            out.ink += (255.0 - qGray(row[x])) / 255.0;
    }
    return out;
}

QImage cropped(const QImage& image, const QRect& box)
{
    return image.copy(box).convertToFormat(QImage::Format_RGB32);
}

QImage padded(const QImage& image, const QSize& canvas, const QPoint& offset)
{
    QImage out(canvas, QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter painter(&out);
    painter.drawImage(offset, image);
    return out;
}

struct Comparison {
    double diffShare = 1.0;
    double meanDeltaE = 0.0;
    std::vector<float> deltaE;  // per block, for the heat map
    int blockWidth = 0, blockHeight = 0;
};

Comparison compareBlocks(const Blocks& a, const Blocks& b, double threshold)
{
    Comparison result;
    result.blockWidth = a.width;
    result.blockHeight = a.height;
    result.deltaE.resize(a.lab.size());
    size_t contentBlocks = 0, differing = 0;
    double sum = 0.0;
    for (size_t i = 0; i < a.lab.size(); ++i) {
        const float d = deltaE(a.lab[i], b.lab[i]);
        result.deltaE[i] = d;
        if (a.content[i] || b.content[i]) {
            ++contentBlocks;
            sum += d;
            if (d > threshold)
                ++differing;
        }
    }
    if (contentBlocks > 0) {
        result.diffShare = static_cast<double>(differing) / static_cast<double>(contentBlocks);
        result.meanDeltaE = sum / static_cast<double>(contentBlocks);
    } else {
        result.diffShare = 0.0;
    }
    return result;
}

// Red: 2x2 blocks that differ (pixel level). Blue boxes: coarse blocks that differ (layout level).
QImage buildHeatMap(const QImage& reference, const Comparison& cmp, const Comparison& coarse, int coarseBlock, double threshold)
{
    QImage heat(reference.size(), QImage::Format_RGB32);
    // Reference washed out to 25 %, so the differences stand out and are easy to place.
    for (int y = 0; y < heat.height(); ++y) {
        const auto* src = reinterpret_cast<const QRgb*>(reference.constScanLine(y));
        auto* dst = reinterpret_cast<QRgb*>(heat.scanLine(y));
        for (int x = 0; x < heat.width(); ++x) {
            const int gray = 255 - (255 - qGray(src[x])) / 4;
            dst[x] = qRgb(gray, gray, gray);
        }
    }
    for (int by = 0; by < cmp.blockHeight; ++by) {
        for (int bx = 0; bx < cmp.blockWidth; ++bx) {
            const float d = cmp.deltaE[static_cast<size_t>(by) * static_cast<size_t>(cmp.blockWidth) + static_cast<size_t>(bx)];
            if (d <= threshold)
                continue;
            const float strength = std::min(1.0f, 0.4f + d / 100.0f);
            for (int dy = 0; dy < 2 && by * 2 + dy < heat.height(); ++dy) {
                auto* row = reinterpret_cast<QRgb*>(heat.scanLine(by * 2 + dy));
                for (int dx = 0; dx < 2 && bx * 2 + dx < heat.width(); ++dx) {
                    const QRgb base = row[bx * 2 + dx];
                    const int g = qGray(base);
                    row[bx * 2 + dx] = qRgb(static_cast<int>(g + (230 - g) * strength),
                                            static_cast<int>(g * (1 - strength)),
                                            static_cast<int>(g * (1 - strength)));
                }
            }
        }
    }
    QPainter painter(&heat);
    painter.setPen(QPen(QColor(0, 80, 255), 1));
    painter.setBrush(QColor(0, 80, 255, 40));
    for (int by = 0; by < coarse.blockHeight; ++by)
        for (int bx = 0; bx < coarse.blockWidth; ++bx)
            if (coarse.deltaE[static_cast<size_t>(by) * static_cast<size_t>(coarse.blockWidth) + static_cast<size_t>(bx)] > threshold)
                painter.drawRect(bx * coarseBlock, by * coarseBlock, coarseBlock - 1, coarseBlock - 1);
    return heat;
}

}  // namespace

QImage flattenOverWhite(const QImage& image)
{
    QImage out(image.size(), QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter painter(&out);
    painter.drawImage(0, 0, image);
    return out;
}

QRect contentBox(const QImage& flattened, int backgroundTolerance)
{
    const int floor = 255 - backgroundTolerance;
    int minX = flattened.width(), minY = flattened.height(), maxX = -1, maxY = -1;
    for (int y = 0; y < flattened.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(flattened.constScanLine(y));
        for (int x = 0; x < flattened.width(); ++x) {
            const QRgb px = row[x];
            if (qRed(px) < floor || qGreen(px) < floor || qBlue(px) < floor) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }
    return maxX < 0 ? QRect() : QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

Score compare(const QImage& reference, const QImage& candidate, const Options& options)
{
    Score score;
    const QImage flatRef = flattenOverWhite(reference);
    const QImage flatCand = flattenOverWhite(candidate);
    const QRect refBox = contentBox(flatRef, options.backgroundTolerance);
    const QRect candBox = contentBox(flatCand, options.backgroundTolerance);
    score.referenceSize = refBox.size();
    score.candidateSize = candBox.size();
    if (refBox.isNull() || candBox.isNull()) {
        // One side is blank: maximal difference unless both are.
        score.valid = refBox.isNull() && candBox.isNull();
        score.diffShare = score.valid ? 0.0 : 1.0;
        score.sizeMismatch = score.valid ? 0.0 : 1.0;
        score.coarseShare = score.pixelCombined = score.combined = score.diffShare;
        return score;
    }

    const QImage refCrop = cropped(flatRef, refBox);
    const QImage candCrop = cropped(flatCand, candBox);
    const QSize canvas(std::max(refCrop.width(), candCrop.width()), std::max(refCrop.height(), candCrop.height()));
    const double overlap = static_cast<double>(std::min(refCrop.width(), candCrop.width())) *
                           std::min(refCrop.height(), candCrop.height()) /
                           (static_cast<double>(canvas.width()) * canvas.height());
    score.sizeMismatch = 1.0 - overlap;

    // Anchor: top-left or centred. Then the candidate is nudged by up to a pixel in each
    // direction: draw.io places hairlines on half pixels and rounds bounds differently, so a
    // one pixel shift is not a rendering difference. The canvas has a one pixel margin for it.
    constexpr int margin = 1;
    const QSize padded_canvas(canvas.width() + 2 * margin, canvas.height() + 2 * margin);
    auto centred = [&](const QImage& img) {
        return QPoint((canvas.width() - img.width()) / 2, (canvas.height() - img.height()) / 2);
    };
    struct Layout {
        Alignment alignment;
        QPoint refOffset, candOffset;
    };
    const Layout anchors[] = {
        {Alignment::TopLeft, QPoint(0, 0), QPoint(0, 0)},
        {Alignment::Centre, centred(refCrop), centred(candCrop)},
    };

    double bestShare = 2.0;
    Layout best{};
    QPoint bestNudge;
    for (const Layout& anchor : anchors) {
        const QPoint refPos = anchor.refOffset + QPoint(margin, margin);
        const QImage refCanvas = padded(refCrop, padded_canvas, refPos);
        const Blocks refBlocks = downsample(refCanvas, options.backgroundTolerance, 2);
        for (int dy = -margin; dy <= margin; ++dy) {
            for (int dx = -margin; dx <= margin; ++dx) {
                const QImage candCanvas = padded(candCrop, padded_canvas, anchor.candOffset + QPoint(margin + dx, margin + dy));
                const Comparison cmp = compareBlocks(refBlocks, downsample(candCanvas, options.backgroundTolerance, 2), options.deltaEThreshold);
                // Prefer the anchor itself on ties.
                if (cmp.diffShare < bestShare - 1e-12) {
                    bestShare = cmp.diffShare;
                    best = anchor;
                    bestNudge = QPoint(dx, dy);
                }
            }
        }
    }

    const QImage refCanvas = padded(refCrop, padded_canvas, best.refOffset + QPoint(margin, margin));
    const QImage candCanvas = padded(candCrop, padded_canvas, best.candOffset + QPoint(margin, margin) + bestNudge);
    const Blocks refBlocks = downsample(refCanvas, options.backgroundTolerance, 2);
    const Blocks candBlocks = downsample(candCanvas, options.backgroundTolerance, 2);
    const Comparison cmp = compareBlocks(refBlocks, candBlocks, options.deltaEThreshold);
    const Comparison coarse = compareBlocks(downsample(refCanvas, options.backgroundTolerance, options.coarseBlock),
                                            downsample(candCanvas, options.backgroundTolerance, options.coarseBlock),
                                            options.deltaEThreshold);
    score.valid = true;
    score.alignment = best.alignment;
    // Where the candidate sits relative to the reference on the common canvas.
    score.offset = best.candOffset - best.refOffset + bestNudge;
    score.canvasSize = padded_canvas;
    score.diffShare = cmp.diffShare;
    score.coarseShare = coarse.diffShare;
    score.meanDeltaE = cmp.meanDeltaE;
    score.inkRatio = refBlocks.ink > 0 ? candBlocks.ink / refBlocks.ink : 1.0;
    score.referenceCanvas = refCanvas;
    score.candidateCanvas = candCanvas;
    score.heatMap = buildHeatMap(refCanvas, cmp, coarse, options.coarseBlock, options.deltaEThreshold);
    score.pixelCombined = 1.0 - (1.0 - score.diffShare) * (1.0 - score.sizeMismatch);
    score.combined = 1.0 - (1.0 - score.coarseShare) * (1.0 - score.sizeMismatch);
    return score;
}

QImage makeComposite(const Score& score, const QString& caption, const QSize& maxPanel)
{
    if (score.referenceCanvas.isNull())
        return {};
    const QSize size = score.referenceCanvas.size();
    const double scale = std::min({1.0, static_cast<double>(maxPanel.width()) / size.width(),
                                   static_cast<double>(maxPanel.height()) / size.height()});
    const QSize panel(std::max(1, static_cast<int>(size.width() * scale)),
                      std::max(1, static_cast<int>(size.height() * scale)));
    constexpr int gap = 8, header = 26;
    QImage out(panel.width() * 3 + gap * 4, panel.height() + header + gap, QImage::Format_RGB32);
    out.fill(QColor(0x30, 0x30, 0x30));
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QFont font = painter.font();
    font.setPixelSize(14);
    painter.setFont(font);
    painter.setPen(Qt::white);
    const QString labels[3] = {QStringLiteral("draw.io"), QStringLiteral("Qraw"), caption};
    const QImage* panels[3] = {&score.referenceCanvas, &score.candidateCanvas, &score.heatMap};
    for (int i = 0; i < 3; ++i) {
        const int x = gap + i * (panel.width() + gap);
        painter.drawText(QRect(x, 0, panel.width(), header), Qt::AlignLeft | Qt::AlignVCenter, labels[i]);
        painter.drawImage(QRect(QPoint(x, header), panel), *panels[i]);
    }
    return out;
}

}  // namespace qraw::fidelity
