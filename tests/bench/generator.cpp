#include "generator.h"

#include "generator_support.h"
#include "qraw/format/codec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace qraw::bench {

namespace {

constexpr std::size_t kMinimumCells = 100;
constexpr double kVertexShare = 0.65;
constexpr std::size_t kSwimlaneEvery = 50; // grid slots between swimlanes
constexpr std::size_t kSwimlaneChildren = 3;
constexpr double kSlotWidth = 240;
constexpr double kSlotHeight = 180;
constexpr double kShapeWidth = 120;
constexpr double kShapeHeight = 60;

class Builder {
public:
    explicit Builder(std::size_t cellCount) : total_(cellCount), rng_(0x51D7A5ull + cellCount)
    {
        xml_.reserve(cellCount * 330);
    }

    std::string build()
    {
        vertexTarget_ = static_cast<std::size_t>(static_cast<double>(total_) * kVertexShare);
        const std::size_t tables = 2 + total_ / 10000;
        addTables(tables);
        addGrid();
        addEdges();
        return wrap();
    }

private:
    void cell(const std::string& attributes, const std::string& geometry)
    {
        xml_ += "<mxCell " + attributes + ">" + geometry + "</mxCell>";
        ++count_;
    }

    static std::string geometry(double x, double y, double w, double h)
    {
        return "<mxGeometry x=\"" + num(x) + "\" y=\"" + num(y) + "\" width=\"" + num(w) + "\" height=\"" + num(h)
            + "\" as=\"geometry\"/>";
    }

    void vertex(const std::string& id, const std::string& value, const std::string& style, const std::string& parent,
                double x, double y, double w, double h)
    {
        cell("id=\"" + id + "\" value=\"" + escapeAttribute(value) + "\" style=\"" + style
                 + "\" vertex=\"1\" parent=\"" + parent + "\"",
             geometry(x, y, w, h));
        ++vertices_;
    }

    // Shape, fill and label by a random draw: five shapes, about 20% html.
    void shapedVertex(const std::string& id, const std::string& parent, double x, double y)
    {
        static const char* const kShapes[] = {"rounded=0", "rounded=1", "ellipse", "rhombus",
                                              "shape=mxgraph.flowchart.document"};
        static const char* const kFills[] = {"#dae8fc", "#d5e8d4", "#fff2cc", "#f8cecc", "#e1d5e7"};
        const std::size_t shape = rng_.below(5);
        const bool html = rng_.below(5) == 0;
        std::string style = std::string(kShapes[shape]) + ";whiteSpace=wrap;fillColor=" + kFills[rng_.below(5)];
        std::string label = "Cell " + id;
        if (html) {
            style += ";html=1";
            label = "<b>" + id + "</b><br>line " + std::to_string(rng_.below(100));
        }
        vertex(id, label, style, parent, x, y, kShapeWidth, kShapeHeight);
    }

    void addTables(std::size_t tables)
    {
        for (std::size_t t = 0; t < tables; ++t) {
            const std::string tid = "t" + std::to_string(t);
            const double x = 40 + static_cast<double>(t) * 300;
            vertex(tid, "Table " + std::to_string(t),
                   "shape=table;startSize=30;container=1;collapsible=0;childLayout=tableLayout;html=1", "1", x, 40, 240,
                   120);
            for (int r = 0; r < 2; ++r) {
                const std::string rid = tid + "r" + std::to_string(r);
                vertex(rid, "",
                       "shape=tableRow;horizontal=0;startSize=0;swimlaneHead=0;swimlaneBody=0;top=0;left=0;bottom=0;"
                       "right=0;collapsible=0;dropTarget=0;fillColor=none;points=[[0,0.5],[1,0.5]];"
                       "portConstraint=eastwest;html=1",
                       tid, 0, 30 + r * 45, 240, 45);
                for (int c = 0; c < 2; ++c) {
                    vertex(rid + "c" + std::to_string(c), "r" + std::to_string(r) + "c" + std::to_string(c),
                           "shape=partialRectangle;html=1;whiteSpace=wrap;connectable=0;overflow=hidden;fillColor=none;"
                           "top=0;left=0;bottom=0;right=0;pointerEvents=1;",
                           rid, c * 120, 0, 120, 45);
                }
            }
        }
    }

    void addGrid()
    {
        // Slots needed: every 50th is a swimlane (1 + 3 vertices), the rest one vertex.
        const std::size_t remaining = vertexTarget_ > vertices_ ? vertexTarget_ - vertices_ : 0;
        const std::size_t perBlock = kSwimlaneEvery + kSwimlaneChildren;
        const std::size_t slotEstimate = std::max<std::size_t>(1, remaining * kSwimlaneEvery / perBlock);
        // Roughly square on the page.
        cols_ = std::max<std::size_t>(
            1, static_cast<std::size_t>(std::sqrt(static_cast<double>(slotEstimate) * kSlotHeight / kSlotWidth)));
        const double top = 220;
        for (std::size_t i = 0; vertices_ < vertexTarget_; ++i) {
            const double x = 40 + static_cast<double>(i % cols_) * kSlotWidth;
            const double y = top + static_cast<double>(i / cols_) * kSlotHeight;
            const std::string id = "v" + std::to_string(i);
            const bool swimlane = i % kSwimlaneEvery == kSwimlaneEvery - 1
                && vertices_ + 1 + kSwimlaneChildren <= vertexTarget_;
            if (swimlane) {
                vertex(id, "Lane " + std::to_string(i), "swimlane;startSize=30;html=1", "1", x, y, 200, 140);
                for (std::size_t c = 0; c < kSwimlaneChildren; ++c) {
                    vertex(id + "c" + std::to_string(c), "in " + std::to_string(c), "rounded=1;whiteSpace=wrap;html=1",
                           id, 10 + static_cast<double>(c) * 60, 50, 50, 40);
                }
            } else {
                shapedVertex(id, "1", x, y);
            }
            slots_.push_back(id);
        }
    }

    void addEdges()
    {
        const std::size_t edgeTarget = total_ - count_;
        for (std::size_t e = 0, i = 0; e < edgeTarget; ++e, ++i) {
            // Right neighbour, or the one below every fourth edge and on the last column.
            const std::size_t from = i % slots_.size();
            const bool below = from % cols_ == cols_ - 1 || e % 4 == 3;
            std::size_t to = below ? from + cols_ : from + 1;
            if (to >= slots_.size())
                to = (from + 1) % slots_.size();
            edge("e" + std::to_string(e), slots_[from], slots_[to], e % 3);
        }
    }

    void edge(const std::string& id, const std::string& source, const std::string& target, std::size_t kind)
    {
        std::string style;
        std::string geo = "<mxGeometry relative=\"1\" as=\"geometry\"/>";
        switch (kind) {
        case 0: style = "edgeStyle=orthogonalEdgeStyle;rounded=0;html=1"; break;
        case 1: style = "edgeStyle=elbowEdgeStyle;elbow=horizontal;html=1"; break;
        default:
            style = "html=1;endArrow=classic";
            geo = "<mxGeometry relative=\"1\" as=\"geometry\"><Array as=\"points\"><mxPoint x=\""
                + num(120 + static_cast<double>(rng_.below(2000))) + "\" y=\"" + num(300 + static_cast<double>(rng_.below(2000)))
                + "\"/></Array></mxGeometry>";
        }
        cell("id=\"" + id + "\" style=\"" + style + "\" edge=\"1\" parent=\"1\" source=\"" + source + "\" target=\""
                 + target + "\"",
             geo);
    }

    [[nodiscard]] std::string wrap() const
    {
        return "<mxfile host=\"qraw-bench\"><diagram name=\"Bench " + std::to_string(total_) + "\" id=\"bench\">"
               "<mxGraphModel dx=\"800\" dy=\"600\" grid=\"1\" gridSize=\"10\" guides=\"1\" tooltips=\"1\" connect=\"1\" "
               "arrows=\"1\" fold=\"1\" page=\"1\" pageScale=\"1\" pageWidth=\"827\" pageHeight=\"1169\" math=\"0\" "
               "shadow=\"0\"><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
            + xml_ + "</root></mxGraphModel></diagram></mxfile>";
    }

    std::size_t total_;
    Prng rng_;
    std::string xml_;
    std::size_t count_ = 0;
    std::size_t vertices_ = 0;
    std::size_t vertexTarget_ = 0;
    std::size_t cols_ = 1;
    std::vector<std::string> slots_; // ids of the top-level grid vertices
};

} // namespace

QByteArray generateDocument(std::size_t cellCount)
{
    if (cellCount < kMinimumCells)
        return {};
    const std::string xml = Builder(cellCount).build();
    format::ReadResult read = format::readDocument(QByteArrayView(xml.data(), static_cast<qsizetype>(xml.size())));
    if (!read.ok())
        return {};
    return format::writeDocument(*read.document);
}

} // namespace qraw::bench
