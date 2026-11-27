// Registry of the vendored draw.io stencil sets (docs/design/render.md
// section 7) plus inline `stencil(...)` shapes and caller-added sets.
// Loosely follows mxStencilRegistry and Graph.js parseStencilSet (draw.io, Apache-2.0).

#include "qraw/format/compression.h"
#include "qraw/format/xml_io.h"
#include "qraw/render/stencil.h"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace qraw::render {

namespace {

constexpr std::string_view kIndexResource = ":/qraw/stencils/index.tsv";
constexpr std::string_view kPackResource = ":/qraw/stencils/stencils.bin";
constexpr std::string_view kPackMagic = "QDSTPK01";
constexpr int kAddedSet = -1; // index value of keys added with addSet()
constexpr std::string_view kInlinePrefix = "stencil(";

// Registry keys are lower case (JavaScript toLowerCase).
std::string lowerKey(std::string_view name)
{
    const bool ascii = std::all_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
    if (ascii) {
        std::string out(name);
        for (char& c : out)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())).toLower().toStdString();
}

QByteArray readResource(std::string_view path)
{
    QFile file(QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size())));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

std::uint32_t readU32(const char* p)
{
    return (std::uint32_t(std::uint8_t(p[0])) << 24) | (std::uint32_t(std::uint8_t(p[1])) << 16) |
           (std::uint32_t(std::uint8_t(p[2])) << 8) | std::uint32_t(std::uint8_t(p[3]));
}

// The shape element of a parsed stencil document: <shape> itself, or the
// first <shape> of a <shapes> wrapper.
const core::XmlNode* shapeElementOf(const core::XmlNode& root)
{
    if (root.isElement("shape"))
        return &root;
    if (root.isElement("shapes"))
        return root.firstChild("shape");
    return nullptr;
}

std::unique_ptr<Stencil> stencilFromXmlText(std::string_view xml)
{
    const auto parsed = format::parseXml(QByteArrayView(xml.data(), static_cast<qsizetype>(xml.size())));
    if (!parsed.document)
        return nullptr;
    const core::XmlNode* shape = shapeElementOf(parsed.document->root);
    if (!shape)
        return nullptr;
    auto stencil = Stencil::fromXml(*shape);
    return stencil ? std::make_unique<Stencil>(std::move(*stencil)) : nullptr;
}

} // namespace

struct StencilRegistry::Impl {
    mutable std::mutex mutex;
    // key -> index into `sets`, or kAddedSet.
    std::unordered_map<std::string, int> index;
    std::vector<std::string> sets; // set paths ("flowchart.xml"), in file order
    // set path -> raw-deflated blob, a view into `pack`.
    std::unordered_map<std::string, std::string_view> blobs;
    QByteArray pack;
    std::vector<bool> loaded; // per set: decoding was attempted
    std::unordered_map<std::string, std::unique_ptr<Stencil>> stencils;
    // Stencils replaced by addSet() stay alive: callers may still hold pointers.
    std::vector<std::unique_ptr<Stencil>> retired;
    // Inline shapes by their full style value; nullptr caches a failure.
    std::unordered_map<std::string, std::unique_ptr<Stencil>> inlineCache;

    void loadBuiltin()
    {
        loadPack(readResource(kPackResource));
        loadIndex(readResource(kIndexResource));
    }

    // stencils.bin: magic, u32be count, then per set u16be pathLen, path,
    // u32be blobLen, blob. A damaged file leaves the sets read so far.
    void loadPack(QByteArray data)
    {
        pack = std::move(data);
        if (pack.size() < 12 || std::string_view(pack.constData(), 8) != kPackMagic)
            return;
        const std::size_t size = static_cast<std::size_t>(pack.size());
        const std::uint32_t count = readU32(pack.constData() + 8);
        std::size_t pos = 12;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (pos + 2 > size)
                return;
            const std::size_t pathLength = (std::size_t(std::uint8_t(pack[pos])) << 8) | std::uint8_t(pack[pos + 1]);
            pos += 2;
            if (pos + pathLength + 4 > size)
                return;
            std::string path(pack.constData() + pos, pathLength);
            pos += pathLength;
            const std::size_t blobLength = readU32(pack.constData() + pos);
            pos += 4;
            if (blobLength > size - pos)
                return;
            blobs.emplace(std::move(path), std::string_view(pack.constData() + pos, blobLength));
            pos += blobLength;
        }
    }

    // index.tsv: `key<TAB>set path` lines; lines starting with '#' are comments.
    void loadIndex(const QByteArray& data)
    {
        std::unordered_map<std::string, int> setIds;
        std::string_view rest(data.constData(), static_cast<std::size_t>(data.size()));
        while (!rest.empty()) {
            const std::size_t eol = rest.find('\n');
            std::string_view line = rest.substr(0, eol);
            rest = eol == std::string_view::npos ? std::string_view() : rest.substr(eol + 1);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            const std::size_t tab = line.find('\t');
            if (line.empty() || line.front() == '#' || tab == std::string_view::npos)
                continue;
            std::string path(line.substr(tab + 1));
            const auto [it, isNew] = setIds.try_emplace(path, static_cast<int>(sets.size()));
            if (isNew)
                sets.push_back(std::move(path));
            index.emplace(std::string(line.substr(0, tab)), it->second);
        }
        loaded.assign(sets.size(), false);
    }

    // Inflates and decodes one set and registers its shapes. A shape whose
    // key belongs to another set (duplicate keys) or was replaced by
    // addSet() is skipped.
    void decodeSet(int setId)
    {
        if (setId < 0 || setId >= static_cast<int>(sets.size()) || loaded[setId])
            return;
        loaded[setId] = true;
        const auto blob = blobs.find(sets[setId]);
        if (blob == blobs.end())
            return;
        const auto inflated = format::inflateBytes(blob->second, -15);
        if (!inflated)
            return;
        const auto root = decodePackedStencilSet(QByteArrayView(inflated->data(), static_cast<qsizetype>(inflated->size())));
        if (!root)
            return;
        const std::string_view setName = root->attributes.value("name");
        for (const core::XmlNode& child : root->children) {
            if (!child.isElement("shape"))
                continue;
            const std::string key = stencilKey(setName, child.attributes.value("name"));
            const auto entry = index.find(key);
            if (entry == index.end() || entry->second != setId)
                continue;
            if (auto stencil = Stencil::fromXml(child))
                stencils.try_emplace(key, std::make_unique<Stencil>(std::move(*stencil)));
        }
    }

    const Stencil* find(std::string_view name)
    {
        const std::string key = lowerKey(name);
        if (const auto it = stencils.find(key); it != stencils.end())
            return it->second.get();
        const auto entry = index.find(key);
        if (entry == index.end())
            return nullptr;
        decodeSet(entry->second);
        const auto it = stencils.find(key);
        return it == stencils.end() ? nullptr : it->second.get();
    }
};

StencilRegistry& StencilRegistry::builtin()
{
    static StencilRegistry registry;
    static std::once_flag loaded;
    std::call_once(loaded, [] { registry.d->loadBuiltin(); }); // missing resources leave it empty
    return registry;
}

StencilRegistry::StencilRegistry() : d(std::make_unique<Impl>()) {}
StencilRegistry::~StencilRegistry() = default;

const Stencil* StencilRegistry::find(std::string_view shapeName)
{
    const std::lock_guard lock(d->mutex);
    return d->find(shapeName);
}

const Stencil* StencilRegistry::findInline(std::string_view shapeValue)
{
    const std::lock_guard lock(d->mutex);
    const std::string value(shapeValue);
    if (const auto cached = d->inlineCache.find(value); cached != d->inlineCache.end())
        return cached->second.get();

    // stencil(<data>): the data is Graph.compress of the shape XML. Plain
    // and URI-encoded XML are accepted too.
    std::string_view payload = shapeValue;
    if (payload.starts_with(kInlinePrefix)) {
        payload.remove_prefix(kInlinePrefix.size());
        if (payload.ends_with(')'))
            payload.remove_suffix(1);
    }
    std::unique_ptr<Stencil> stencil;
    if (const auto xml = format::decompressDiagram(payload))
        stencil = stencilFromXmlText(*xml);
    if (!stencil && payload.starts_with('<'))
        stencil = stencilFromXmlText(payload);
    if (!stencil)
        stencil = stencilFromXmlText(format::decodeUriComponent(payload));

    return d->inlineCache.emplace(value, std::move(stencil)).first->second.get();
}

bool StencilRegistry::contains(std::string_view shapeName) const
{
    const std::lock_guard lock(d->mutex);
    const std::string key = lowerKey(shapeName);
    return d->index.contains(key) || d->stencils.contains(key);
}

std::size_t StencilRegistry::indexedCount() const
{
    const std::lock_guard lock(d->mutex);
    return d->index.size();
}

void StencilRegistry::addSet(const core::XmlNode& shapesElement)
{
    const std::lock_guard lock(d->mutex);
    const std::string_view setName = shapesElement.attributes.value("name");
    for (const core::XmlNode& child : shapesElement.children) {
        if (!child.isElement("shape"))
            continue;
        auto stencil = Stencil::fromXml(child);
        if (!stencil)
            continue;
        const std::string key = stencilKey(setName, child.attributes.value("name"));
        d->index.insert_or_assign(key, kAddedSet);
        auto& slot = d->stencils[key];
        if (slot)
            d->retired.push_back(std::move(slot));
        slot = std::make_unique<Stencil>(std::move(*stencil));
    }
}

} // namespace qraw::render
