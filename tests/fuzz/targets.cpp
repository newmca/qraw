#include "targets.h"

#include "qraw/core/style.h"
#include "qraw/format/codec.h"
#include "qraw/format/compression.h"
#include "qraw/format/library.h"
#include "qraw/format/semantic.h"
#include "qraw/format/xml_io.h"
#include "qraw/render/stencil.h"
#include "qraw/render/text.h"

#include "support/recording_canvas.h"

#include <algorithm>

namespace qraw::fuzz {

namespace {

using Pool = std::vector<std::string>;

QByteArrayView view(std::string_view text)
{
    return QByteArrayView(text.data(), static_cast<qsizetype>(text.size()));
}

std::string clip(std::string text, std::size_t length = 240)
{
    if (text.size() > length)
        text = text.substr(0, length) + "...";
    return text;
}

std::uint64_t hashInput(std::string_view input)
{
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (const char c : input) {
        h ^= static_cast<unsigned char>(c);
        h *= 0x100000001b3ULL;
    }
    return h;
}

// A seed from `pool`, mutated. Splices draw from the same pool.
std::string mutated(Rng& rng, const Mutator& mutator, const Pool& pool)
{
    std::string data = pool.empty() ? std::string() : pool[rng.below(pool.size())];
    mutator.mutate(rng, data, pool);
    return data;
}

// ---------------------------------------------------------------- dictionaries

std::vector<DictionaryEntry> xmlDictionary()
{
    std::vector<DictionaryEntry> d;
    for (const char* text : {
             "<mxCell ", "</", "<![CDATA[", "]]>", "&amp;", "&#x0;", "&#xD800;", "&#38;", "&undefined;", "<!DOCTYPE",
             "<!DOCTYPE x [<!ENTITY a \"b\">]>", "<?xml version=\"1.0\" encoding=\"UTF-16\"?>", "<?pi ?>", "<!--", "-->",
             "<mxGraphModel>", "<mxfile>", "<diagram>", "<root>", "<mxGeometry ", "<Array as=\"points\">", "<mxPoint ",
             "<object ", "<UserObject ", "<mxlibrary>", "parent=\"", "source=\"", "target=\"", "style=\"", "as=\"geometry\"",
             "id=\"", "vertex=\"1\"", "edge=\"1\"", "xmlns:", "value=\"", "\"", "'", "/>", ">", "<"})
        d.push_back({text, 1});
    d.push_back({"<a>", 700});    // deep nesting, past kMaxXmlDepth
    d.push_back({"</a>", 700});
    d.push_back({"<mxCell>", 700});
    d.push_back({"&amp;", 400});
    return d;
}

std::vector<DictionaryEntry> compressionDictionary()
{
    std::vector<DictionaryEntry> d;
    for (const char* text : {"%", "%2", "%E0%", "%C0%80", "%F0%9F%98%80", "%zz", "=", "==", "AAAA", "/", "+", "-_", " ", "\n"})
        d.push_back({text, 1});
    d.push_back({"%41", 500});
    d.push_back({"A", 4000});
    return d;
}

std::vector<DictionaryEntry> styleDictionary()
{
    std::vector<DictionaryEntry> d;
    for (const char* text : {";", "=", "shape=", "stencil(", ")", ";;;", "html=1", "edgeStyle=", "=;", " ", "\n"})
        d.push_back({text, 1});
    d.push_back({";", 300});
    d.push_back({"a=b;", 300});
    return d;
}

std::vector<DictionaryEntry> stencilDictionary()
{
    std::vector<DictionaryEntry> d = xmlDictionary();
    for (const char* text : {
             "<shape ", "<path>", "<move x=\"", "<line x=\"", "<curve x1=\"", "<quad x1=\"", "<arc ", "<close/>", "<ellipse ",
             "<rect ", "<roundrect ", "<fillstroke/>", "<fill/>", "<stroke/>", "<text ", "<connections>", "<constraint ",
             "<foreground>", "<background>", "<include-shape ", "<strokewidth width=\"", "<save/>", "<restore/>", "<rotate ",
             "<image ", "<dashpattern pattern=\"", "<fontsize size=\"", "<fontstyle style=\"", "<alpha alpha=\"", "<fillcolor ",
             "w=\"", "h=\"", "x=\"", "y=\"", "arcsize=\"", "aspect=\"fixed\"", "strokewidth=\"inherit\"", "direction=\"", "stencil("})
        d.push_back({text, 1});
    d.push_back({std::string("\0\0\0\0", 4), 8});
    d.push_back({std::string("\xff\xff\xff\xff", 4), 4});
    d.push_back({std::string("\0\0\0\x14", 4), 2});
    d.push_back({"<move x=\"0\" y=\"0\"/>", 600});
    return d;
}

std::vector<DictionaryEntry> htmlDictionary()
{
    std::vector<DictionaryEntry> d;
    for (const char* text : {
             "<div", "<div>", "</div>", "<font ", "<table", "<tr>", "<td>", "<script", "<script>", "</script>", "<style>",
             "style=\"", "style='", "color:", "url(", "javascript:", "<b>", "</b>", "<i>", "<br>", "<br/>", "<a href=\"", "<img src=\"",
             "<!--", "-->", "<![CDATA[", "&amp;", "&lt;", "&nbsp;", "&#x41;", "&#0;", "&bogus;", "&", "<", ">", "\"", "'", "=", "<ul><li>"})
        d.push_back({text, 1});
    d.push_back({"<div>", 600});
    d.push_back({"<b>", 600});
    d.push_back({"<table><tr><td>", 300});
    d.push_back({"<font size=\"7\">", 300});
    return d;
}

// ---------------------------------------------------------------------- codec

std::string runCodec(std::string_view input)
{
    // The library parser sees the same bytes; it must not crash.
    (void)format::parseLibrary(input);

    const format::ReadResult first = format::readDocument(view(input));
    if (!first.ok())
        return {};
    const QByteArray written = format::writeDocument(*first.document);
    const format::ReadResult second = format::readDocument(written);
    if (!second.ok())
        return "written document does not read back";
    std::string difference;
    if (!format::semanticEqual(*first.document, *second.document, &difference))
        return "write/read is not semantically equal: " + clip(difference);
    const QByteArray writtenAgain = format::writeDocument(*second.document);
    if (written != writtenAgain)
        return "second write is not byte-identical to the first";
    return {};
}

Target codecTarget(const std::shared_ptr<const SeedSets>& seeds, std::size_t maxLength)
{
    auto mutator = std::make_shared<Mutator>(xmlDictionary(), maxLength);
    return {"codec", [=](Rng& rng) { return mutated(rng, *mutator, seeds->documents); }, runCodec};
}

// ---------------------------------------------------------------- compression

std::string runCompression(std::string_view input)
{
    // Decoding is total: whatever the input, it must return, not crash.
    if (const auto decoded = format::decompressDiagram(input)) {
        // Whatever the page decoded to survives a compress / decompress trip.
        const auto again = format::decompressDiagram(format::compressDiagram(*decoded));
        if (!again)
            return "compressDiagram(x) does not decompress, x = " + clip(*decoded, 80);
        if (*again != *decoded)
            return "decompressDiagram(compressDiagram(x)) != x, x = " + clip(*decoded, 80);
    }
    (void)format::decodeUriComponent(input);
    if (const std::string back = format::decodeUriComponent(format::encodeUriComponent(input)); back != input)
        return "decodeUriComponent(encodeUriComponent(x)) != x";
    for (const int windowBits : {-15, 15, 31})
        (void)format::inflateBytes(input, windowBits);
    if (const auto roundTrip = format::inflateBytes(format::deflateBytes(input, -15), -15); !roundTrip || *roundTrip != input)
        return "inflateBytes(deflateBytes(x)) != x";
    return {};
}

Target compressionTarget(const std::shared_ptr<const SeedSets>& seeds, std::size_t maxLength)
{
    auto mutator = std::make_shared<Mutator>(compressionDictionary(), maxLength);
    auto xmlMutator = std::make_shared<Mutator>(xmlDictionary(), maxLength);
    auto generate = [=](Rng& rng) {
        switch (rng.below(3)) {
        case 0: // the base64 text of a stored page, mutated
            return mutated(rng, *mutator, seeds->compressedPayloads);
        case 1: // the inflated XML mutated, then compressed: the mutation reaches the inner decoder
            return format::compressDiagram(mutated(rng, *xmlMutator, seeds->inflatedModels));
        default: // both layers
            return mutated(rng, *mutator, {format::compressDiagram(mutated(rng, *xmlMutator, seeds->inflatedModels))});
        }
    };
    return {"compression", generate, runCompression};
}

// ---------------------------------------------------------------------- style

std::string runStyle(std::string_view input)
{
    core::Style style{input};
    if (style.toString() != input)
        return "Style(s).toString() != s";

    // A few edits derived from the input itself, so a replay is exact.
    Rng rng(hashInput(input));
    const std::string keys[] = {"k", "shape", "fillColor", "a b", "x"};
    for (int step = 0; step < 8; ++step) {
        std::string key = !style.tokens().empty() && rng.chance(2) ? style.tokens()[rng.below(style.tokens().size())].key
                                                                   : keys[rng.below(5)];
        const bool validKey = !key.empty() && key.find_first_of(";=") == std::string::npos;
        switch (rng.below(5)) {
        case 0:
            if (validKey) {
                const std::string value = "v" + std::to_string(rng.below(100));
                style.set(key, value);
                if (style.value(key) != value)
                    return "value(k) != v after set(k, v), k = " + clip(key, 40);
                if (core::Style(style.toString()).value(key) != value)
                    return "set(k, v) is lost by toString/parse, k = " + clip(key, 40);
            }
            break;
        case 1:
            style.remove(key);
            if (style.has(key))
                return "has(k) after remove(k), k = " + clip(key, 40);
            break;
        case 2:
            style.addName(key);
            break;
        case 3:
            style.removeName(key);
            if (!key.empty() && style.hasName(key))
                return "hasName(n) after removeName(n), n = " + clip(key, 40);
            break;
        default:
            (void)style.names();
            (void)style.value(key, "fallback");
            break;
        }
        if (core::Style(style.toString()).toString() != style.toString())
            return "edited style does not re-serialize identically";
    }
    return {};
}

Target styleTarget(const std::shared_ptr<const SeedSets>& seeds, std::size_t maxLength)
{
    auto mutator = std::make_shared<Mutator>(styleDictionary(), std::min<std::size_t>(maxLength, 16384));
    return {"style", [=](Rng& rng) { return mutated(rng, *mutator, seeds->styles); }, runStyle};
}

// -------------------------------------------------------------------- stencil
//
// The input is "<kind>\n<body>": kind is xml (a <shape> element), packed (an
// inflated packed stencil set) or inline (a shape=stencil(...) style value).

void paintStencil(const render::Stencil& stencil)
{
    struct Config {
        double x, y, w, h;
        const char* direction;
    };
    for (const Config& config : {Config{0, 0, 100, 60, "east"}, Config{10, 20, 333, 17, "east"}, Config{0, 0, 50, 80, "south"}}) {
        render::StyleMap map;
        map.set("direction", config.direction);
        const render::ResolvedStyle style(map, false);
        test::RecordingCanvas canvas;
        const render::ShapePaintContext context{style, nullptr, {}, false};
        stencil.paint(canvas, context, config.x, config.y, config.w, config.h);
        (void)stencil.labelBounds(style, {config.x, config.y, config.w, config.h});
        (void)stencil.connectionConstraints(style, config.w, config.h);
    }
}

void paintShapeElement(const core::XmlNode& root)
{
    const core::XmlNode* shape = root.isElement("shape") ? &root : root.isElement("shapes") ? root.firstChild("shape") : nullptr;
    if (!shape)
        return;
    if (const auto stencil = render::Stencil::fromXml(*shape))
        paintStencil(*stencil);
}

std::string runStencil(std::string_view input)
{
    const std::size_t newline = input.find('\n');
    const std::string_view kind = input.substr(0, newline);
    const std::string_view body = newline == std::string_view::npos ? std::string_view() : input.substr(newline + 1);
    if (kind == "xml") {
        if (const auto parsed = format::parseXml(view(body)); parsed.document)
            paintShapeElement(parsed.document->root);
    } else if (kind == "packed") {
        if (const auto set = render::decodePackedStencilSet(view(body)))
            for (std::size_t i = 0; i < set->children.size() && i < 24; ++i)
                paintShapeElement(set->children[i]);
    } else if (kind == "inline") {
        render::StencilRegistry registry; // fresh, so nothing is cached across iterations
        if (const render::Stencil* stencil = registry.findInline(body))
            paintStencil(*stencil);
    }
    return {};
}

Target stencilTarget(const std::shared_ptr<const SeedSets>& seeds, std::size_t maxLength)
{
    auto mutator = std::make_shared<Mutator>(stencilDictionary(), maxLength);
    auto inlinePool = std::make_shared<Pool>(seeds->inlineStencils);
    for (std::size_t i = 0; i < seeds->stencilShapes.size() && i < 24; ++i)
        inlinePool->push_back("stencil(" + format::compressDiagram(seeds->stencilShapes[i]) + ")");
    auto generate = [=](Rng& rng) {
        const std::uint64_t kind = rng.below(10);
        if (kind < 6)
            return "xml\n" + mutated(rng, *mutator, seeds->stencilShapes);
        if (kind < 8)
            return "packed\n" + mutated(rng, *mutator, seeds->packedStencilSets);
        if (rng.chance(2))
            return "inline\n" + mutated(rng, *mutator, *inlinePool);
        // Mutate the shape XML, then wrap it as an inline value: the mutation reaches fromXml through findInline.
        return "inline\nstencil(" + format::compressDiagram(mutated(rng, *mutator, seeds->stencilShapes)) + ")";
    };
    return {"stencil", generate, runStencil};
}

// ----------------------------------------------------------------------- html

std::string runHtml(std::string_view input)
{
    const std::string once = render::sanitizeHtml(input);
    if (render::sanitizeHtml(once) != once)
        return "sanitizeHtml is not idempotent";
    // The header promises a well-formed XHTML fragment.
    const std::string wrapped = "<div>" + once + "</div>";
    if (!format::parseXml(view(wrapped)).document)
        return "sanitized output is not well-formed XML";
    (void)render::makeRichText(input, true);
    (void)render::makeRichText(input, false);
    return {};
}

Target htmlTarget(const std::shared_ptr<const SeedSets>& seeds, std::size_t maxLength)
{
    auto mutator = std::make_shared<Mutator>(htmlDictionary(), std::min<std::size_t>(maxLength, 16384));
    return {"html", [=](Rng& rng) { return mutated(rng, *mutator, seeds->htmlLabels); }, runHtml};
}

} // namespace

std::vector<Target> makeTargets(std::shared_ptr<const SeedSets> seeds, std::size_t maxLength)
{
    std::vector<Target> targets;
    targets.push_back(codecTarget(seeds, maxLength));
    targets.push_back(compressionTarget(seeds, maxLength));
    targets.push_back(styleTarget(seeds, maxLength));
    targets.push_back(stencilTarget(seeds, maxLength));
    targets.push_back(htmlTarget(seeds, maxLength));
    return targets;
}

} // namespace qraw::fuzz
