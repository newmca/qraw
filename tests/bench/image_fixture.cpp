// The synthetic image-heavy diagram (generator.h, generateImageHeavyDocument).
//
// An image-heavy diagram: one page with 150 cells (119 vertices, 29 edges),
// 32 of the vertices embedded PNG screenshots of about 1000 x 600 px,
// written the way draw.io writes them:
//   shape=image;...;image=data:image/png,<base64>;shadow=1;rounded=1;...
// (no ";base64" marker, which would end the style value), half of them as
// the child of a group next to a caption. The other cells are a mix of
// shapes, texts, a swimlane, a table, stencil shapes and edges, spread over
// a board of 8 x 5 slots 1100 x 750 px apart: 32 slots hold an image, 8 a
// cluster of the other cells.

#include "generator.h"

#include "generator_support.h"

#include <QBuffer>
#include <QImage>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace qraw::bench {

namespace {

constexpr int kImagePixelWidth = 1000;
constexpr int kImagePixelHeight = 600;
constexpr double kImageWidth = 830; // the shape of an image, in page units
constexpr double kImageHeight = 500;
constexpr std::size_t kImageCount = 32;
constexpr std::size_t kBoardColumns = 8;
constexpr std::size_t kBoardRows = 5;
constexpr double kSlotWidth = 1100;
constexpr double kSlotHeight = 750;
constexpr double kBoardMargin = 10;
constexpr std::size_t kClusterEvery = 5; // every 5th slot is a cluster of small cells
// Photo-like noise: every colour channel of the field is jittered at random. The
// amplitude sets the compressed size (about 0.9 MB of PNG, 1.2 MB of base64).
constexpr std::uint64_t kNoiseLevels = 47; // each channel moves by -23 .. +23
constexpr int kNoiseCentre = 23;

// --- PNG content --------------------------------------------------------------

struct Rgb {
    int r;
    int g;
    int b;
};

int lerp(int a, int b, int t256)
{
    return a + (b - a) * t256 / 256;
}

std::uint32_t pack(int r, int g, int b)
{
    const auto clamp = [](int v) { return static_cast<std::uint32_t>(std::clamp(v, 0, 255)); };
    return 0xFF000000u | (clamp(r) << 16) | (clamp(g) << 8) | clamp(b);
}

// A smooth colour field (bilinear over a coarse random grid) with light panels
// that carry rows of small dark blocks like lines of text, and seeded noise
// in the field: it compresses like a screenshot of an application window on
// a photo background.
QImage makePicture(Prng& rng)
{
    constexpr int kGridColumns = 9;
    constexpr int kGridRows = 6;
    constexpr int kCellWidth = kImagePixelWidth / (kGridColumns - 1);
    constexpr int kCellHeight = kImagePixelHeight / (kGridRows - 1);
    std::array<std::array<Rgb, kGridColumns>, kGridRows> grid{};
    for (auto& row : grid) {
        for (Rgb& c : row)
            c = {40 + int(rng.below(176)), 40 + int(rng.below(176)), 40 + int(rng.below(176))};
    }

    QImage image(kImagePixelWidth, kImagePixelHeight, QImage::Format_ARGB32);
    std::vector<bool> flat(static_cast<std::size_t>(kImagePixelWidth) * kImagePixelHeight, false);
    std::vector<std::uint32_t> pixels(flat.size());
    for (int y = 0; y < kImagePixelHeight; ++y) {
        const int gy = std::min(y / kCellHeight, kGridRows - 2);
        const int fy = (y - gy * kCellHeight) * 256 / kCellHeight;
        for (int x = 0; x < kImagePixelWidth; ++x) {
            const int gx = std::min(x / kCellWidth, kGridColumns - 2);
            const int fx = (x - gx * kCellWidth) * 256 / kCellWidth;
            const auto channel = [&](int Rgb::*member) {
                const int top = lerp(grid[gy][gx].*member, grid[gy][gx + 1].*member, fx);
                const int bottom = lerp(grid[gy + 1][gx].*member, grid[gy + 1][gx + 1].*member, fx);
                return lerp(top, bottom, fy);
            };
            const std::uint64_t noise = rng.next();
            const auto jitter = [&](int shift) { return int(((noise >> shift) & 0xFFFF) % kNoiseLevels) - kNoiseCentre; };
            pixels[static_cast<std::size_t>(y) * kImagePixelWidth + x] =
                pack(channel(&Rgb::r) + jitter(0), channel(&Rgb::g) + jitter(16), channel(&Rgb::b) + jitter(32));
        }
    }

    // Panels of text-like blocks: flat (no noise), like rendered UI.
    const std::size_t panels = 6 + rng.below(5);
    for (std::size_t p = 0; p < panels; ++p) {
        const int pw = 160 + int(rng.below(260));
        const int ph = 90 + int(rng.below(160));
        const int px = int(rng.below(std::size_t(kImagePixelWidth - pw)));
        const int py = int(rng.below(std::size_t(kImagePixelHeight - ph)));
        const std::uint32_t background = pack(225 + int(rng.below(30)), 225 + int(rng.below(30)), 225 + int(rng.below(30)));
        const std::uint32_t ink = pack(20 + int(rng.below(60)), 20 + int(rng.below(60)), 20 + int(rng.below(60)));
        for (int y = py; y < py + ph; ++y) {
            for (int x = px; x < px + pw; ++x)
                pixels[static_cast<std::size_t>(y) * kImagePixelWidth + x] = background;
        }
        for (int line = py + 10; line + 8 < py + ph; line += 16) {
            const int length = pw / 3 + int(rng.below(std::size_t(pw / 2)));
            for (int x = px + 10; x < px + 10 + length && x + 4 < px + pw;) {
                const int glyph = 3 + int(rng.below(8));
                for (int gyp = line; gyp < line + 8; ++gyp) {
                    for (int gxp = x; gxp < x + glyph && gxp < px + pw - 4; ++gxp)
                        pixels[static_cast<std::size_t>(gyp) * kImagePixelWidth + gxp] = ink;
                }
                x += glyph + 3 + int(rng.below(3));
            }
        }
    }

    for (int y = 0; y < kImagePixelHeight; ++y) {
        std::copy_n(pixels.begin() + static_cast<std::ptrdiff_t>(y) * kImagePixelWidth, kImagePixelWidth,
                    reinterpret_cast<std::uint32_t*>(image.scanLine(y)));
    }
    return image;
}

// The base64 text of a PNG file (an alpha channel, 8 bits).
std::string pngBase64(Prng& rng)
{
    const QImage picture = makePicture(rng);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    picture.save(&buffer, "PNG");
    const QByteArray text = png.toBase64();
    return std::string(text.constData(), static_cast<std::size_t>(text.size()));
}

// --- the diagram ----------------------------------------------------------------

class Builder {
public:
    Builder() : rng_(0x1A6E5ull) {}

    std::string build()
    {
        buildImages();
        buildClusters();
        buildEdges();
        return wrap();
    }

private:
    enum class Item { LabelGroup, Swimlane, Table, Stencil, Shape, Text };

    std::string nextId() { return "c" + std::to_string(++ids_); }

    static std::string geometry(double x, double y, double w, double h)
    {
        return "<mxGeometry x=\"" + num(x) + "\" y=\"" + num(y) + "\" width=\"" + num(w) + "\" height=\"" + num(h)
            + "\" as=\"geometry\"/>";
    }

    // Returns the new id.
    std::string vertex(const std::string& value, const std::string& style, const std::string& parent, double x,
                       double y, double w, double h, bool connectable = true)
    {
        const std::string id = nextId();
        xml_ += "<mxCell id=\"" + id + "\" value=\"" + escapeAttribute(value) + "\" style=\"" + escapeAttribute(style)
            + "\" vertex=\"1\"" + (connectable ? "" : " connectable=\"0\"") + " parent=\"" + parent + "\">"
            + geometry(x, y, w, h) + "</mxCell>";
        return id;
    }

    static std::pair<double, double> slotOrigin(std::size_t slot)
    {
        return {kBoardMargin + double(slot % kBoardColumns) * kSlotWidth,
                kBoardMargin + double(slot / kBoardColumns) * kSlotHeight};
    }

    // --- words ----------------------------------------------------------------
    std::string words(std::size_t count)
    {
        static const char* const kVocabulary[] = {"level",   "track",  "rider",  "speed",  "camera", "jump",   "trail",
                                                  "terrain", "menu",   "score",  "button", "screen", "player", "bike",
                                                  "season",  "unlock", "reward", "route",  "stage",  "finish", "start",
                                                  "checkpoint", "timer", "balance"};
        std::string text;
        for (std::size_t i = 0; i < count; ++i)
            text += (i ? " " : "") + std::string(kVocabulary[rng_.below(std::size(kVocabulary))]);
        return text;
    }

    // A rich label: bold lead, a line break, and a plain remainder.
    std::string htmlLabel(std::size_t wordCount)
    {
        return "<b>" + words(2) + "</b><br>" + words(wordCount);
    }

    // --- images -----------------------------------------------------------------
    static std::string imageStyle(const std::string& payload)
    {
        return "shape=image;verticalLabelPosition=bottom;labelBackgroundColor=default;verticalAlign=top;imageAspect=0;"
               "image=data:image/png,"
            + payload + ";shadow=1;rounded=1;roundedPerimeter=1;arcSize=2;fontColor=default;";
    }

    void buildImages()
    {
        std::size_t image = 0;
        for (std::size_t slot = 0; slot < kBoardColumns * kBoardRows && image < kImageCount; ++slot) {
            if (slot % kClusterEvery == 0)
                continue;
            const auto [x, y] = slotOrigin(slot);
            const std::string payload = pngBase64(rng_);
            if (image % 2 == 0) {
                vertex("Screenshot " + std::to_string(image + 1), imageStyle(payload), "1", x, y, kImageWidth,
                       kImageHeight);
            } else {
                const std::string group = vertex("", "group", "1", x, y, kImageWidth, kImageHeight, false);
                vertex("", imageStyle(payload), group, 0, 0, kImageWidth, kImageHeight);
                vertex(htmlLabel(6),
                       "text;whiteSpace=wrap;html=1;fontStyle=1;fontSize=24;align=center;textShadow=1;"
                       "verticalAlign=middle;fontColor=default;",
                       group, 0, 0, kImageWidth, 90);
                groups_.push_back(group);
            }
            ++image;
        }
    }

    // --- clusters of small cells --------------------------------------------------
    void buildClusters()
    {
        std::vector<Item> items;
        const auto add = [&](Item kind, std::size_t count) { items.insert(items.end(), count, kind); };
        add(Item::LabelGroup, 4);
        add(Item::Swimlane, 1);
        add(Item::Table, 1);
        add(Item::Stencil, 3);
        add(Item::Shape, 13);
        add(Item::Text, 16);
        for (std::size_t i = items.size(); i > 1; --i) // Fisher-Yates
            std::swap(items[i - 1], items[rng_.below(i)]);

        for (std::size_t i = 0; i < items.size(); ++i) {
            const std::size_t clusterIndex = i % (kBoardColumns * kBoardRows / kClusterEvery);
            const std::size_t inCluster = i / (kBoardColumns * kBoardRows / kClusterEvery);
            const auto [ox, oy] = slotOrigin(clusterIndex * kClusterEvery);
            const double x = ox + 30 + double(inCluster % 4) * 260;
            const double y = oy + 30 + double(inCluster / 4) * 200;
            placeItem(items[i], x, y);
        }
    }

    void placeItem(Item kind, double x, double y)
    {
        static const char* const kFills[] = {"#dae8fc", "#d5e8d4", "#fff2cc", "#f8cecc", "#e1d5e7"};
        const std::string fill = std::string("fillColor=") + kFills[rng_.below(5)];
        switch (kind) {
        case Item::LabelGroup: {
            const std::string group = vertex("", "group", "1", x, y, 180, 120, false);
            vertex("", "rounded=1;whiteSpace=wrap;html=1;" + fill, group, 0, 0, 180, 120);
            vertex(htmlLabel(3), "text;html=1;align=center;verticalAlign=middle;whiteSpace=wrap;", group, 10, 40, 160,
                   40);
            break;
        }
        case Item::Swimlane: {
            const std::string lane = vertex("Lane", "swimlane;startSize=30;html=1;" + fill, "1", x, y, 220, 150);
            for (int c = 0; c < 3; ++c) {
                vertex("in " + std::to_string(c), "rounded=1;whiteSpace=wrap;html=1;" + fill, lane, 10 + c * 70, 50,
                       60, 40);
            }
            connectable_.push_back(lane);
            break;
        }
        case Item::Table: {
            const std::string table = vertex(
                "Table", "shape=table;startSize=30;container=1;collapsible=0;childLayout=tableLayout;html=1", "1", x, y,
                240, 120);
            for (int r = 0; r < 2; ++r) {
                const std::string row =
                    vertex("",
                           "shape=tableRow;horizontal=0;startSize=0;swimlaneHead=0;swimlaneBody=0;top=0;left=0;"
                           "bottom=0;right=0;collapsible=0;dropTarget=0;fillColor=none;points=[[0,0.5],[1,0.5]];"
                           "portConstraint=eastwest;html=1",
                           table, 0, 30 + r * 45, 240, 45);
                for (int c = 0; c < 2; ++c) {
                    vertex("r" + std::to_string(r) + "c" + std::to_string(c),
                           "shape=partialRectangle;html=1;whiteSpace=wrap;connectable=0;overflow=hidden;fillColor=none;"
                           "top=0;left=0;bottom=0;right=0;pointerEvents=1;",
                           row, c * 120, 0, 120, 45);
                }
            }
            break;
        }
        case Item::Stencil: {
            static const char* const kStencils[] = {"document", "database", "delay"};
            connectable_.push_back(vertex(words(2),
                                          std::string("shape=mxgraph.flowchart.") + kStencils[stencils_++ % 3]
                                              + ";whiteSpace=wrap;html=1;" + fill,
                                          "1", x, y, 140, 80));
            break;
        }
        case Item::Shape: {
            static const char* const kShapes[] = {"rounded=1", "rounded=0", "ellipse", "rhombus"};
            const bool html = shapes_++ % 3 == 0;
            connectable_.push_back(vertex(html ? htmlLabel(3) : words(2),
                                          std::string(kShapes[shapes_ % 4]) + ";whiteSpace=wrap;" + (html ? "html=1;" : "")
                                              + fill,
                                          "1", x, y, 140, 70));
            break;
        }
        case Item::Text: {
            const bool html = texts_++ % 4 != 3;
            // About 280 characters for a rich text, of the length of a typical annotation.
            vertex(html ? htmlLabel(40) : words(8),
                   std::string("text;") + (html ? "html=1;" : "") + "whiteSpace=wrap;align=left;verticalAlign=top;",
                   "1", x, y, 230, 120);
            break;
        }
        }
    }

    // --- edges -------------------------------------------------------------------
    void buildEdges()
    {
        std::vector<std::string> ends = connectable_;
        ends.insert(ends.end(), groups_.begin(), groups_.end());
        for (std::size_t e = 0; e < kEdges; ++e) {
            const std::string& source = ends[(e * 7) % ends.size()];
            const std::string& target = ends[(e * 7 + 3 + e / ends.size()) % ends.size()];
            std::string style;
            if (e < 20) {
                style = "edgeStyle=orthogonalEdgeStyle;rounded=0;orthogonalLoop=1;jettySize=auto;html=1;"
                        "strokeColor=#6c8ebf;strokeWidth=2;endArrow=classic;";
            } else {
                style = "html=1;strokeWidth=2;endArrow=classic;";
            }
            const std::string label = e % 5 == 0 ? (e % 2 == 0 ? "yes" : "no") : "";
            const std::string id = nextId();
            xml_ += "<mxCell id=\"" + id + "\" value=\"" + label + "\" style=\"" + style
                + "\" edge=\"1\" parent=\"1\" source=\"" + source + "\" target=\"" + target
                + "\"><mxGeometry relative=\"1\" as=\"geometry\"/></mxCell>";
        }
    }

    [[nodiscard]] std::string wrap() const
    {
        return "<mxfile host=\"qraw-bench\"><diagram name=\"Board\" id=\"image-heavy\"><mxGraphModel dx=\"1600\" "
               "dy=\"1000\" grid=\"1\" gridSize=\"10\" guides=\"1\" tooltips=\"1\" connect=\"1\" arrows=\"1\" fold=\"1\" "
               "page=\"1\" pageScale=\"1\" pageWidth=\"850\" pageHeight=\"1100\" math=\"0\" shadow=\"0\"><root>"
               "<mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
            + xml_ + "</root></mxGraphModel></diagram></mxfile>";
    }

    static constexpr std::size_t kEdges = 29;

    Prng rng_;
    std::string xml_;
    std::size_t ids_ = 0;
    std::size_t stencils_ = 0;
    std::size_t shapes_ = 0;
    std::size_t texts_ = 0;
    std::vector<std::string> connectable_; // shapes, stencils and the swimlane
    std::vector<std::string> groups_;      // image groups
};

} // namespace

QByteArray generateImageHeavyDocument()
{
    const std::string xml = Builder().build();
    return QByteArray(xml.data(), static_cast<qsizetype>(xml.size()));
}

} // namespace qraw::bench
