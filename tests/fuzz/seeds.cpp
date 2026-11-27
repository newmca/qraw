#include "seeds.h"

#include "qraw/core/xml_node.h"
#include "qraw/format/compression.h"
#include "qraw/format/xml_io.h"
#include "qraw/render/stencil.h"

#include <QByteArray>
#include <QFile>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace qraw::fuzz {

namespace fs = std::filesystem;

namespace {

// Stencil keys across several sets of the builtin registry. A key that a
// future registry no longer has is skipped.
constexpr const char* kStencilKeys[] = {
    "mxgraph.alibaba_cloud.address_purification", "mxgraph.arrows.two_way_arrow_horizontal",
    "mxgraph.aws3.deployments", "mxgraph.aws3.topic_2", "mxgraph.aws4.clean_rooms", "mxgraph.aws4.eks_cloud",
    "mxgraph.aws4.internet", "mxgraph.aws4.simple_email_service", "mxgraph.azure.powershell_file",
    "mxgraph.basic.4_point_star", "mxgraph.basic.cloud_callout", "mxgraph.basic.pie",
    "mxgraph.cisco.computers_and_peripherals.terminal", "mxgraph.cisco.routers.router", "mxgraph.cisco19.ldap",
    "mxgraph.cisco_safe.design.storage", "mxgraph.citrix2.director",
    "mxgraph.electrical.electro-mechanical.pressure_actuated", "mxgraph.electrical.miscellaneous.generic_component",
    "mxgraph.electrical.signal_sources.vss2", "mxgraph.fluid_power.x10420", "mxgraph.fluid_power.x12290",
    "mxgraph.flowchart.document", "mxgraph.flowchart.process", "mxgraph.gcp2.cloud_vpn", "mxgraph.gcp3.aimachinelearning",
    "mxgraph.ibm_cloud.ibm--db2", "mxgraph.ios7.misc.add", "mxgraph.mockup.controls.on-off_button_3",
    "mxgraph.mscae.deprecated.compute", "mxgraph.networks2.scanner", "mxgraph.office.concepts.search",
    "mxgraph.office.servers.skype_for_business_server", "mxgraph.pid.feeders.proportional_feeder_(metering)",
    "mxgraph.pid.misc.ship", "mxgraph.pid.vessels.tank_(dished_roof)", "mxgraph.rack.general.2u_rack_server",
    "mxgraph.salesforce.partners", "mxgraph.signs.sports.dirt_bike", "mxgraph.signs.travel.escalator_down",
    "mxgraph.veeam.2d.virtual_machine", "mxgraph.veeam.tape_writing_device", "mxgraph.veeam2.transport",
    "mxgraph.webicons.ask", "mxgraph.weblogos.bookmarks.fr", "mxgraph.android.phone2", "mxgraph.bpmn.task",
    "mxgraph.lean_mapping.truck_icon", "mxgraph.sitemap.home", "mxgraph.atlassian2.jira",
};

constexpr const char* kHandWrittenStencils[] = {
    "<shape name=\"hand\" w=\"40\" h=\"40\" aspect=\"fixed\" strokewidth=\"inherit\"><connections><constraint x=\"0.5\" y=\"0\" "
    "perimeter=\"1\" name=\"N\"/></connections><background><path><move x=\"0\" y=\"0\"/><line x=\"40\" y=\"0\"/>"
    "<curve x1=\"40\" y1=\"20\" x2=\"20\" y2=\"40\" x3=\"0\" y3=\"40\"/><close/></path></background><foreground>"
    "<fillcolor color=\"#ff0000\"/><fillstroke/><ellipse x=\"10\" y=\"10\" w=\"20\" h=\"20\"/><stroke/>"
    "<text str=\"Hi\" x=\"20\" y=\"20\" align=\"center\" valign=\"middle\"/></foreground></shape>",
    "<shape h=\"10\" w=\"10\"><background><roundrect x=\"0\" y=\"0\" w=\"10\" h=\"10\" arcsize=\"50\"/></background>"
    "<foreground><dashpattern pattern=\"3 3\"/><strokewidth width=\"2\"/><arc x=\"5\" y=\"5\" rx=\"4\" ry=\"4\" "
    "x-axis-rotation=\"0\" large-arc-flag=\"1\" sweep-flag=\"0\"/><stroke/></foreground></shape>",
};

constexpr const char* kHtmlSnippets[] = {
    "<b>bold <i>both</b> italic</i>",
    "<div><div><div>nested</div></div></div><br><br/>",
    "<font color=\"#ff0000\" size=\"7\" face=\"a&quot;b\">x</font>",
    "<span style=\"color:red;background:url(x);font-size:12px\">s</span>",
    "<a href=\"javascript:alert(1)\">x</a><a href='https://example.com/?a=1&b=2'>y</a>",
    "<script>alert(1)</script><style>p{}</style><iframe src=x></iframe>after",
    "<table><tr><td colspan=\"2\">a<td>b<tr><th>c</table>",
    "<img src=\"data:image/png;base64,AAAA\" width=10><img src=http://x/y.png>",
    "&amp;&lt;&gt;&quot;&#39;&nbsp;&#x41;&#65;&bogus;&#xFFFFFFFF;&#0;",
    "<ul><li>one<li>two<ol><li>three</ul>",
    "<p style=\"margin:0 0 0 -5px;border:1px solid #000\">p</p><hr>",
    "unclosed <b <i x=\">",
    "<!-- comment --><![CDATA[ data ]]><?pi x?>text",
    "<h1>h</h1><pre>  keep\n  space </pre><code>c</code><blockquote>q</blockquote>",
    "line1\nline2\r\nline3<br>line4",
};

constexpr const char* kStyleSnippets[] = {
    "", ";", ";;", "=", "a=", "=b", "a=b;", ";a=b", "a=b;;c=d", "a==b", "a=b=c;d",
    "rounded=1;whiteSpace=wrap;html=1;", "shape=stencil(", "edgeStyle=orthogonalEdgeStyle;rounded=0;",
    "text;html=1;strokeColor=none;fillColor=none;align=center;verticalAlign=middle;",
    " a = b ; c ", "a=b;a=c;a=d", "ellipse;shape=cloud;",
};

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::vector<fs::path> sortedFiles(const fs::path& directory, const std::set<std::string>& extensions)
{
    std::vector<fs::path> files;
    std::error_code error;
    if (!fs::exists(directory, error))
        return files;
    for (fs::recursive_directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
        if (it->is_regular_file() && extensions.contains(it->path().extension().string()))
            files.push_back(it->path());
    std::sort(files.begin(), files.end());
    return files;
}

// Collects the pieces of one parsed corpus file. Sets keep the output
// sorted and free of duplicates.
struct Collector {
    std::set<std::string> compressed, inflated, styles, html, shapes, inlineStencils;

    void visit(const core::XmlNode& node)
    {
        if (!node.isElement()) {
            return;
        }
        if (node.isElement("diagram") && !node.hasElementChildren()) {
            const std::string text = node.textContent();
            if (const auto inflatedXml = format::decompressDiagram(text); inflatedXml && !text.empty()) {
                compressed.insert(text);
                inflated.insert(*inflatedXml);
            }
        }
        if (node.isElement("shape"))
            shapes.insert(format::serializeElement(node, {.pretty = false}));
        const std::string* style = node.attributes.find("style");
        if (style) {
            styles.insert(*style);
            if (const auto at = style->find("stencil("); at != std::string::npos) {
                const auto close = style->find(')', at);
                if (close != std::string::npos)
                    inlineStencils.insert(style->substr(at, close - at + 1));
            }
        }
        if (isHtmlLabel(node, style)) {
            for (const char* name : {"value", "label"})
                if (const std::string* value = node.attributes.find(name))
                    html.insert(*value);
        }
        for (const core::XmlNode& child : node.children)
            visit(child);
    }

private:
    // html=1 on the cell, or on the mxCell inside an object / UserObject.
    static bool isHtmlLabel(const core::XmlNode& node, const std::string* style)
    {
        const auto hasHtml = [](const std::string* s) { return s && s->find("html=1") != std::string::npos; };
        if (hasHtml(style))
            return true;
        if (node.isElement("object") || node.isElement("UserObject"))
            if (const core::XmlNode* cell = node.firstChild("mxCell"))
                return hasHtml(cell->attributes.find("style"));
        return false;
    }
};

void appendSorted(std::vector<std::string>& out, const std::set<std::string>& values, std::size_t maxLength)
{
    for (const std::string& value : values)
        if (value.size() <= maxLength)
            out.push_back(value);
}

// Reads the QDSTPK01 resource directly, so the seeds do not depend on the
// registry's private state. Small sets first: mutating a 1 MB set is useless.
void collectPackedSets(std::vector<std::string>& out, std::size_t maxLength)
{
    QFile file(QStringLiteral(":/qraw/stencils/stencils.bin"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray pack = file.readAll();
    if (pack.size() < 12 || std::string_view(pack.constData(), 8) != "QDSTPK01")
        return;
    const auto u32 = [&](qsizetype p) {
        return (std::uint32_t(std::uint8_t(pack[p])) << 24) | (std::uint32_t(std::uint8_t(pack[p + 1])) << 16) |
               (std::uint32_t(std::uint8_t(pack[p + 2])) << 8) | std::uint32_t(std::uint8_t(pack[p + 3]));
    };
    std::vector<std::string> sets;
    qsizetype pos = 12;
    for (std::uint32_t i = 0, count = u32(8); i < count && pos + 2 <= pack.size(); ++i) {
        const qsizetype pathLength = (std::uint8_t(pack[pos]) << 8) | std::uint8_t(pack[pos + 1]);
        pos += 2 + pathLength;
        if (pos + 4 > pack.size())
            break;
        const qsizetype blobLength = u32(pos);
        pos += 4;
        if (blobLength < 0 || pos + blobLength > pack.size())
            break;
        if (blobLength <= 6000) // compressed size; the set inflates to a few times that
            if (auto inflatedSet = format::inflateBytes(std::string_view(pack.constData() + pos, blobLength), -15))
                if (inflatedSet->size() <= maxLength)
                    sets.push_back(std::move(*inflatedSet));
        pos += blobLength;
    }
    std::sort(sets.begin(), sets.end(), [](const auto& a, const auto& b) { return a.size() != b.size() ? a.size() < b.size() : a < b; });
    if (sets.size() > 12)
        sets.resize(12);
    out.insert(out.end(), sets.begin(), sets.end());
}

} // namespace

SeedSets collectSeeds(const std::string& repositoryRoot, std::size_t maxLength)
{
    SeedSets seeds;
    const fs::path root(repositoryRoot);
    Collector collector;

    std::vector<fs::path> files;
    for (const fs::path& directory : {root / "tests/corpus", root / "tests/compat/fixtures"})
        for (fs::path& file : sortedFiles(directory, {".drawio", ".xml"}))
            files.push_back(std::move(file));
    for (fs::path& file : sortedFiles(root / "tests/render/data/golden", {".drawio"}))
        files.push_back(std::move(file));

    for (const fs::path& file : files) {
        const std::string bytes = readFile(file);
        const auto parsed = format::parseXml(QByteArrayView(bytes.data(), static_cast<qsizetype>(bytes.size())));
        if (parsed.document)
            collector.visit(parsed.document->root);
        if (bytes.size() <= maxLength)
            seeds.documents.push_back(bytes);
    }

    appendSorted(seeds.compressedPayloads, collector.compressed, maxLength);
    appendSorted(seeds.inflatedModels, collector.inflated, maxLength);
    appendSorted(seeds.styles, collector.styles, maxLength);
    appendSorted(seeds.htmlLabels, collector.html, maxLength);
    appendSorted(seeds.inlineStencils, collector.inlineStencils, maxLength);
    for (const char* snippet : kStyleSnippets)
        seeds.styles.emplace_back(snippet);
    for (const char* snippet : kHtmlSnippets)
        seeds.htmlLabels.emplace_back(snippet);

    // Builtin stencils first, then the shapes found in the corpus and the hand-written ones.
    auto& registry = render::StencilRegistry::builtin();
    for (const char* key : kStencilKeys)
        if (const render::Stencil* stencil = registry.find(key))
            seeds.stencilShapes.push_back(format::serializeElement(stencil->shapeElement(), {.pretty = false}));
    appendSorted(seeds.stencilShapes, collector.shapes, maxLength);
    for (const char* snippet : kHandWrittenStencils)
        seeds.stencilShapes.emplace_back(snippet);
    collectPackedSets(seeds.packedStencilSets, maxLength);
    return seeds;
}

} // namespace qraw::fuzz
