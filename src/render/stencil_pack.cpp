// Port of js/stencils.min.js decode() (draw.io, Apache-2.0), modified: builds
// core::XmlNode trees instead of StencilNode and bounds-checks every read.
// Reference implementation in Python: tools/stencils/stencil_pack.py.

#include "qraw/core/number.h"
#include "qraw/format/xml_io.h"
#include "qraw/render/stencil.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace qraw::render {

namespace {

constexpr std::size_t kHeaderSize = 20;
constexpr std::size_t kStreamCount = 5;
constexpr std::uint8_t kCloseOp = 255;

enum Stream { Ops = 0, DeltaX = 1, DeltaY = 2, DeltaOther = 3, Strings = 4 };

// Elements whose x*/y* attributes are delta-coded against running positions.
bool isDrawElement(std::string_view name)
{
    static const std::unordered_set<std::string_view> names = {
        "move", "line", "quad", "curve", "arc", "rect", "roundrect", "ellipse", "text", "image", "include-shape"};
    return names.contains(name);
}

enum class Axis { None, X, Y };

Axis coordinateAxis(std::string_view attribute)
{
    if (attribute == "x" || attribute == "x1" || attribute == "x2" || attribute == "x3")
        return Axis::X;
    if (attribute == "y" || attribute == "y1" || attribute == "y2" || attribute == "y3")
        return Axis::Y;
    return Axis::None;
}

// String(n / 1000) for a count of thousandths.
std::string formatThousandths(std::int64_t n)
{
    if (n % 1000 == 0) {
        char buffer[32];
        const auto result = std::to_chars(buffer, buffer + sizeof buffer, n / 1000);
        return std::string(buffer, result.ptr);
    }
    return core::formatNumber(static_cast<double>(n) / 1000.0);
}

class PackedSetDecoder {
public:
    explicit PackedSetDecoder(QByteArrayView data) : data_(data) {}

    std::optional<core::XmlNode> decode()
    {
        if (!splitStreams())
            return std::nullopt;
        std::optional<core::XmlNode> root;
        // Open elements, innermost last. Pointers stay valid: a node gets
        // siblings only after it was popped.
        std::vector<core::XmlNode*> stack;
        core::XmlNode top; // synthetic parent of the root element
        stack.push_back(&top);

        while (opPos_ < streams_[Ops].size()) {
            const std::uint8_t op = nextOp();
            if (op == kCloseOp) {
                if (stack.size() <= 1)
                    return std::nullopt;
                stack.pop_back();
                continue;
            }
            if (stack.size() > format::kMaxXmlDepth)
                return std::nullopt;
            core::XmlNode* node = readElement(op, *stack.back());
            if (!node)
                return std::nullopt;
            if ((op & 1) == 0)
                stack.push_back(node);
        }
        if (failed_ || top.children.empty())
            return std::nullopt;
        return std::move(top.children.front());
    }

private:
    // The five streams, validated against the header.
    bool splitStreams()
    {
        if (static_cast<std::size_t>(data_.size()) < kHeaderSize)
            return false;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(data_.data());
        std::size_t offset = kHeaderSize;
        for (std::size_t i = 0; i < kStreamCount; ++i) {
            const std::uint32_t length = (std::uint32_t(bytes[4 * i]) << 24) | (std::uint32_t(bytes[4 * i + 1]) << 16) |
                                         (std::uint32_t(bytes[4 * i + 2]) << 8) | std::uint32_t(bytes[4 * i + 3]);
            if (length > static_cast<std::size_t>(data_.size()) - offset)
                return false;
            streams_[i] = std::string_view(reinterpret_cast<const char*>(bytes) + offset, length);
            offset += length;
        }
        splitStrings();
        return true;
    }

    // S4: UTF-8 strings separated by NUL (like String.split('\0'): a stream
    // without NUL is one string, an empty stream is one empty string).
    void splitStrings()
    {
        std::string_view rest = streams_[Strings];
        for (;;) {
            const std::size_t nul = rest.find('\0');
            if (nul == std::string_view::npos) {
                strings_.push_back(rest);
                return;
            }
            strings_.push_back(rest.substr(0, nul));
            rest.remove_prefix(nul + 1);
        }
    }

    std::uint8_t nextOp() { return static_cast<std::uint8_t>(streams_[Ops][opPos_++]); }

    bool hasOp() const { return opPos_ < streams_[Ops].size(); }

    std::optional<std::string_view> nextString()
    {
        if (stringPos_ >= strings_.size())
            return std::nullopt;
        return strings_[stringPos_++];
    }

    // Zigzag varint, little-endian base 128. nullopt: stream exhausted or
    // longer than 64 bits.
    std::optional<std::int64_t> readDelta(Stream stream)
    {
        const std::string_view bytes = streams_[stream];
        std::size_t& pos = positions_[stream];
        std::uint64_t n = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (pos >= bytes.size())
                return std::nullopt;
            const auto c = static_cast<std::uint8_t>(bytes[pos++]);
            n |= std::uint64_t(c & 127) << shift;
            if (!(c & 128)) {
                const std::uint64_t magnitude = (n & 1) ? (n + 1) >> 1 : n >> 1;
                return (n & 1) ? static_cast<std::int64_t>(0 - magnitude) : static_cast<std::int64_t>(magnitude);
            }
        }
        return std::nullopt;
    }

    // Reads one element (name, attributes) from the op stream and appends it
    // to `parent`. nullptr on malformed input.
    core::XmlNode* readElement(std::uint8_t op, core::XmlNode& parent)
    {
        const std::size_t nameIndex = op >> 1;
        if (nameIndex > names_.size()) // the next new name is names_.size()
            return fail();
        if (nameIndex == names_.size()) {
            const auto name = nextString();
            if (!name)
                return fail();
            names_.emplace_back(*name);
        }
        if (!hasOp())
            return fail();
        const std::size_t count = nextOp();

        parent.children.push_back(core::XmlNode::element(names_[nameIndex]));
        core::XmlNode& node = parent.children.back();
        if (node.name == "shape")
            resetRunningValues();
        const bool coordinates = isDrawElement(node.name);

        for (std::size_t i = 0; i < count; ++i) {
            if (!readAttribute(node, nameIndex, coordinates))
                return fail();
        }
        return &node;
    }

    bool readAttribute(core::XmlNode& node, std::size_t nameIndex, bool coordinates)
    {
        if (opPos_ + 2 > streams_[Ops].size()) // attribute id and type byte
            return false;
        const std::size_t id = nextOp();
        if (id > attributeNames_.size())
            return false;
        if (id == attributeNames_.size()) {
            const auto name = nextString();
            if (!name)
                return false;
            attributeNames_.emplace_back(*name);
        }
        const std::uint8_t type = nextOp();
        const std::string& attribute = attributeNames_[id];

        if (type == 0) {
            const auto value = nextString();
            if (!value)
                return false;
            node.attributes.set(attribute, *value);
            return true;
        }
        const Axis axis = coordinates ? coordinateAxis(attribute) : Axis::None;
        std::uint64_t sum = 0; // wraps instead of overflowing on hostile input
        if (axis == Axis::None) {
            const auto delta = readDelta(DeltaOther);
            if (!delta)
                return false;
            std::uint64_t& running = otherSums_[(std::uint64_t(nameIndex) << 32) | id];
            running += static_cast<std::uint64_t>(*delta);
            sum = running;
        } else {
            const auto delta = readDelta(axis == Axis::X ? DeltaX : DeltaY);
            if (!delta)
                return false;
            std::uint64_t& running = axis == Axis::X ? px_ : py_;
            running += static_cast<std::uint64_t>(*delta);
            sum = running;
        }
        node.attributes.set(attribute, formatThousandths(static_cast<std::int64_t>(sum)));
        return true;
    }

    core::XmlNode* fail()
    {
        failed_ = true;
        return nullptr;
    }

    void resetRunningValues()
    {
        px_ = py_ = 0;
        otherSums_.clear();
    }

    QByteArrayView data_;
    std::array<std::string_view, kStreamCount> streams_{};
    std::array<std::size_t, 4> positions_{};
    std::size_t opPos_ = 0;
    std::vector<std::string_view> strings_;
    std::size_t stringPos_ = 0;
    std::vector<std::string> names_;
    std::vector<std::string> attributeNames_;
    // Running values, reset at every <shape>: x and y positions, and sums
    // per (element name, attribute name).
    std::uint64_t px_ = 0;
    std::uint64_t py_ = 0;
    std::unordered_map<std::uint64_t, std::uint64_t> otherSums_;
    bool failed_ = false;
};

} // namespace

std::optional<core::XmlNode> decodePackedStencilSet(QByteArrayView inflated)
{
    return PackedSetDecoder(inflated).decode();
}

} // namespace qraw::render
