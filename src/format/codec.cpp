#include "qraw/format/codec.h"

#include "qraw/format/compression.h"
#include "qraw/format/library.h"
#include "qraw/format/logging.h"

#include <QFile>
#include <QSaveFile>

#include <algorithm>
#include <iterator>

namespace qraw::format {

using core::Cell;
using core::CellWrapper;
using core::Diagnostic;
using core::Document;
using core::ForeignChildren;
using core::Page;
using core::PageEncoding;
using core::XmlNode;

namespace {

Diagnostic warning(std::string message, const Page* page = nullptr, int line = 0)
{
    Diagnostic d;
    d.severity = Diagnostic::Severity::Warning;
    d.message = std::move(message);
    d.pageId = page ? page->id() : std::string();
    d.line = line;
    return d;
}

// Moves all children except the first element named `modeledName` into
// `foreign` (recording its slot) and returns that element.
std::optional<XmlNode> splitChildren(std::vector<XmlNode>&& children, std::string_view modeledName,
                                     ForeignChildren& foreign)
{
    std::optional<XmlNode> modeled;
    for (auto& child : children) {
        if (!modeled && child.isElement(modeledName)) {
            foreign.modeledSlot = foreign.nodes.size();
            modeled = std::move(child);
        } else {
            foreign.nodes.push_back(std::move(child));
        }
    }
    if (!modeled)
        foreign.modeledSlot = foreign.nodes.size();
    return modeled;
}

// Interleaves `modeled` (zero or more nodes) into the foreign nodes at the
// recorded slot.
std::vector<XmlNode> mergeChildren(const ForeignChildren& foreign, std::vector<XmlNode> modeled)
{
    std::vector<XmlNode> out;
    out.reserve(foreign.nodes.size() + modeled.size());
    const std::size_t slot = std::min(foreign.modeledSlot, foreign.nodes.size());
    out.insert(out.end(), foreign.nodes.begin(), foreign.nodes.begin() + static_cast<std::ptrdiff_t>(slot));
    std::move(modeled.begin(), modeled.end(), std::back_inserter(out));
    out.insert(out.end(), foreign.nodes.begin() + static_cast<std::ptrdiff_t>(slot), foreign.nodes.end());
    return out;
}

std::vector<XmlNode> mergeChildren(const ForeignChildren& foreign, std::optional<XmlNode> modeled)
{
    std::vector<XmlNode> single;
    if (modeled)
        single.push_back(std::move(*modeled));
    return mergeChildren(foreign, std::move(single));
}

// --- reading -----------------------------------------------------------------

Cell readCell(XmlNode&& node)
{
    Cell cell;
    cell.attributes() = std::move(node.attributes);
    if (auto geometryNode = splitChildren(std::move(node.children), "mxGeometry", cell.foreignChildren())) {
        core::Geometry geometry;
        geometry.attributes() = std::move(geometryNode->attributes);
        geometry.children() = std::move(geometryNode->children);
        cell.setGeometry(std::move(geometry));
    }
    return cell;
}

Cell readWrappedCell(XmlNode&& node)
{
    CellWrapper wrapper;
    wrapper.tag = std::move(node.name);
    wrapper.attributes = std::move(node.attributes);
    auto inner = splitChildren(std::move(node.children), "mxCell", wrapper.foreign);
    Cell cell = readCell(std::move(*inner)); // callers guarantee an <mxCell> child
    cell.setWrapper(std::move(wrapper));
    return cell;
}

void readModel(XmlNode&& model, Page& page)
{
    page.modelAttributes() = std::move(model.attributes);
    auto root = splitChildren(std::move(model.children), "root", page.modelForeign());
    page.setRootElementPresent(root.has_value());
    if (!root)
        return;
    page.rootAttributes() = std::move(root->attributes);
    for (auto& child : root->children) {
        if (child.isElement("mxCell"))
            page.appendEntry(readCell(std::move(child)));
        else if (child.isElement() && child.firstChild("mxCell"))
            page.appendEntry(readWrappedCell(std::move(child))); // <object>, <UserObject>, any tag
        else
            page.appendEntry(std::move(child));
    }
}

bool isTextual(const XmlNode& n) { return n.kind == XmlNode::Kind::Text || n.kind == XmlNode::Kind::CData; }

// The children of a compressed <diagram> other than its text payload
// (comments, PIs), with the slot where the payload sits. draw.io reads only
// the text content of the element, so such nodes are legal next to it.
ForeignChildren foreignAroundPayload(std::vector<XmlNode>&& children)
{
    ForeignChildren foreign;
    bool payloadSeen = false;
    for (auto& child : children) {
        if (isTextual(child)) {
            if (!payloadSeen)
                foreign.modeledSlot = foreign.nodes.size();
            payloadSeen = true;
        } else {
            foreign.nodes.push_back(std::move(child));
        }
    }
    return foreign;
}

// A page can be written compressed only if reading it back would find the
// same model: everything next to the payload must be inert (comments, PIs,
// whitespace). A second <mxGraphModel> or stray text would be mistaken for
// the page content.
bool canWriteCompressed(const Page& page)
{
    return std::all_of(page.diagramForeign().nodes.begin(), page.diagramForeign().nodes.end(), [](const XmlNode& n) {
        return n.kind == XmlNode::Kind::Comment || n.kind == XmlNode::Kind::ProcessingInstruction
            || (n.kind == XmlNode::Kind::Text && core::isXmlWhitespace(n.text));
    });
}

Page readDiagram(XmlNode&& node, std::vector<Diagnostic>& diagnostics)
{
    Page page;
    page.diagramAttributes() = std::move(node.attributes);

    const bool hasInlineModel = node.firstChild("mxGraphModel") != nullptr;
    const std::string payload = node.textContent();
    const bool hasPayload = !core::isXmlWhitespace(payload);

    if (hasInlineModel || !hasPayload) {
        page.setEncoding(PageEncoding::Inline);
        auto model = splitChildren(std::move(node.children), "mxGraphModel", page.diagramForeign());
        page.setHasModel(model.has_value());
        if (model)
            readModel(std::move(*model), page);
        return page;
    }

    if (!node.hasElementChildren()) {
        if (auto xml = decompressDiagram(payload)) {
            auto parsed = parseXml(QByteArrayView(xml->data(), static_cast<qsizetype>(xml->size())));
            for (auto& w : parsed.warnings) {
                w.pageId = page.id();
                diagnostics.push_back(std::move(w));
            }
            if (parsed.document && parsed.document->root.isElement("mxGraphModel")) {
                page.setEncoding(PageEncoding::Compressed);
                page.diagramForeign() = foreignAroundPayload(std::move(node.children));
                page.payloadFrame().declaration = std::move(parsed.document->declaration);
                page.payloadFrame().prolog = std::move(parsed.document->prolog);
                page.payloadFrame().epilog = std::move(parsed.document->epilog);
                readModel(std::move(parsed.document->root), page);
                return page;
            }
            diagnostics.push_back(warning(parsed.document ? "compressed page does not contain <mxGraphModel>"
                                                          : "compressed page is not valid XML: " + parsed.error.message
                                                  + " (inflated line " + std::to_string(parsed.error.line) + ")",
                                          &page, node.sourceLine));
        } else {
            diagnostics.push_back(warning("page payload could not be decompressed", &page, node.sourceLine));
        }
    } else {
        diagnostics.push_back(warning("page mixes a text payload with other content", &page, node.sourceLine));
    }
    // Keep the payload verbatim; the page is preserved but has no cells.
    page.setEncoding(PageEncoding::Opaque);
    page.setHasModel(false);
    page.diagramForeign().nodes = std::move(node.children);
    page.diagramForeign().modeledSlot = page.diagramForeign().nodes.size();
    diagnostics.back().message += "; kept verbatim";
    return page;
}

// --- writing -----------------------------------------------------------------

XmlNode cellToXml(const Cell& cell)
{
    XmlNode mxCell = XmlNode::element("mxCell", cell.attributes());
    std::optional<XmlNode> geometry;
    if (const auto* g = cell.geometry()) {
        geometry = XmlNode::element("mxGeometry", g->attributes());
        geometry->children = g->children();
    }
    mxCell.children = mergeChildren(cell.foreignChildren(), std::move(geometry));
    const CellWrapper* wrapper = cell.wrapper();
    if (!wrapper)
        return mxCell;
    XmlNode w = XmlNode::element(wrapper->tag, wrapper->attributes);
    w.children = mergeChildren(wrapper->foreign, std::move(mxCell));
    return w;
}

XmlNode modelToXml(const Page& page)
{
    XmlNode model = XmlNode::element("mxGraphModel", page.modelAttributes());
    std::optional<XmlNode> root;
    if (page.rootElementPresent() || !page.rootEntries().empty()) {
        root = XmlNode::element("root", page.rootAttributes());
        root->children.reserve(page.rootEntries().size());
        for (const auto& entry : page.rootEntries()) {
            if (const Cell* cell = std::get_if<Cell>(&entry))
                root->children.push_back(cellToXml(*cell));
            else
                root->children.push_back(std::get<XmlNode>(entry));
        }
    }
    model.children = mergeChildren(page.modelForeign(), std::move(root));
    return model;
}

XmlNode diagramToXml(const Page& page, PageEncodingPolicy policy)
{
    XmlNode diagram = XmlNode::element("diagram", page.diagramAttributes());
    if (page.encoding() == PageEncoding::Opaque || !page.hasModel()) {
        diagram.children = page.diagramForeign().nodes;
        return diagram;
    }
    bool compressed = page.encoding() == PageEncoding::Compressed;
    if (policy == PageEncodingPolicy::Compressed)
        compressed = true;
    else if (policy == PageEncodingPolicy::Uncompressed)
        compressed = false;
    if (compressed && page.encoding() != PageEncoding::Compressed && !canWriteCompressed(page))
        compressed = false; // would not read back as the same page; keep it inline

    XmlNode model = modelToXml(page);
    const core::PayloadFrame& frame = page.payloadFrame();
    if (compressed) {
        SerializeOptions compact;
        compact.pretty = false;
        compact.spaceBeforeSelfClose = false;
        XmlDocument payload;
        payload.declaration = frame.declaration;
        payload.prolog = frame.prolog;
        payload.root = std::move(model);
        payload.epilog = frame.epilog;
        diagram.children = mergeChildren(page.diagramForeign(),
                                         XmlNode::textNode(compressDiagram(serializeDocument(payload, compact))));
    } else {
        // Comments and PIs around the compressed model stay around it; the
        // declaration and DOCTYPE only exist at the top of a document.
        std::vector<XmlNode> content;
        auto keepable = [](const XmlNode& n) { return n.kind != XmlNode::Kind::Doctype; };
        std::copy_if(frame.prolog.begin(), frame.prolog.end(), std::back_inserter(content), keepable);
        content.push_back(std::move(model));
        std::copy_if(frame.epilog.begin(), frame.epilog.end(), std::back_inserter(content), keepable);
        diagram.children = mergeChildren(page.diagramForeign(), std::move(content));
    }
    return diagram;
}

} // namespace

bool ReadResult::hasErrors() const
{
    return !document || std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) {
        return d.severity == Diagnostic::Severity::Error;
    });
}

std::optional<Document> fromXml(XmlDocument xml, std::vector<Diagnostic>& diagnostics)
{
    Document doc;
    doc.declaration() = std::move(xml.declaration);
    doc.prolog() = std::move(xml.prolog);
    doc.epilog() = std::move(xml.epilog);

    XmlNode& root = xml.root;
    if (root.isElement("mxfile")) {
        doc.setKind(core::DocumentKind::MxFile);
        doc.fileAttributes() = std::move(root.attributes);
        for (auto& child : root.children) {
            if (child.isElement("diagram"))
                doc.appendEntry(readDiagram(std::move(child), diagnostics));
            else
                doc.appendEntry(std::move(child));
        }
    } else if (root.isElement("mxGraphModel")) {
        doc.setKind(core::DocumentKind::BareModel);
        Page page;
        readModel(std::move(root), page);
        doc.appendEntry(std::move(page));
    } else if (root.isElement("mxlibrary")) {
        // D-008: a library is not a diagram; the message lets callers route it.
        Diagnostic d;
        d.severity = Diagnostic::Severity::Error;
        d.message = kLibraryDiagnostic;
        d.line = root.sourceLine;
        diagnostics.push_back(std::move(d));
        return std::nullopt;
    } else {
        Diagnostic d;
        d.severity = Diagnostic::Severity::Error;
        d.message = "unsupported root element <" + root.name + ">; expected <mxfile> or <mxGraphModel>";
        d.line = root.sourceLine;
        diagnostics.push_back(std::move(d));
        return std::nullopt;
    }

    for (const Page* page : doc.pages()) {
        auto problems = page->validate();
        diagnostics.insert(diagnostics.end(), problems.begin(), problems.end());
    }
    return doc;
}

std::string pageModelText(const Page& page)
{
    SerializeOptions options;
    options.spaceBeforeSelfClose = false; // as mxUtils.getPrettyXml
    return serializeElement(modelToXml(page), options);
}

ModelTextResult parsePageModelText(QByteArrayView text)
{
    ModelTextResult result;
    auto parsed = parseXml(text);
    result.diagnostics = std::move(parsed.warnings);
    const auto fail = [&result](std::string message, int line) {
        result.error.severity = Diagnostic::Severity::Error;
        result.error.message = std::move(message);
        result.error.line = line;
        return result;
    };
    if (!parsed.document) {
        result.error = std::move(parsed.error);
        return result;
    }
    XmlNode& root = parsed.document->root;
    if (root.isElement("mxGraphModel")) {
        Page page;
        readModel(std::move(root), page);
        result.page = std::move(page);
    } else if (root.isElement("mxfile")) {
        std::vector<Diagnostic> found;
        std::optional<Document> document = fromXml(std::move(*parsed.document), found);
        const Page* first = document && document->pageCount() > 0 ? document->page(0) : nullptr;
        if (first == nullptr || first->encoding() == PageEncoding::Opaque || !first->hasModel())
            return fail("the first <diagram> has no readable <mxGraphModel>", 0);
        result.diagnostics.insert(result.diagnostics.end(), found.begin(), found.end());
        Page page;
        page.modelAttributes() = first->modelAttributes();
        page.modelForeign() = first->modelForeign();
        page.rootAttributes() = first->rootAttributes();
        page.setRootElementPresent(first->rootElementPresent());
        for (const auto& entry : first->rootEntries())
            page.appendEntry(entry);
        result.page = std::move(page);
    } else {
        return fail("expected <mxGraphModel> (or an <mxfile>), found <" + root.name + ">", root.sourceLine);
    }
    return result;
}

ReadResult readDocument(QByteArrayView data)
{
    ReadResult result;
    auto parsed = parseXml(data);
    result.diagnostics = std::move(parsed.warnings);
    if (!parsed.document) {
        result.diagnostics.push_back(std::move(parsed.error));
        return result;
    }
    result.document = fromXml(std::move(*parsed.document), result.diagnostics);
    return result;
}

ReadResult readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCDebug(lcFormat) << "cannot open" << path << file.errorString();
        ReadResult result;
        Diagnostic d;
        d.severity = Diagnostic::Severity::Error;
        d.message = "cannot open " + path.toStdString() + ": " + file.errorString().toStdString();
        result.diagnostics.push_back(std::move(d));
        return result;
    }
    return readDocument(file.readAll());
}

XmlDocument toXml(const Document& document, const WriteOptions& options)
{
    XmlDocument xml;
    xml.declaration = document.declaration();
    xml.prolog = document.prolog();
    xml.epilog = document.epilog();

    if (document.kind() == core::DocumentKind::BareModel && document.entries().size() == 1
        && document.pageCount() == 1) {
        xml.root = modelToXml(*document.page(0));
        return xml;
    }

    xml.root = XmlNode::element("mxfile", document.fileAttributes());
    if (options.pageEncoding != PageEncodingPolicy::Preserve && xml.root.attributes.has("compressed"))
        xml.root.attributes.set("compressed", options.pageEncoding == PageEncodingPolicy::Compressed ? "true" : "false");
    for (const auto& entry : document.entries()) {
        if (const Page* page = std::get_if<Page>(&entry))
            xml.root.children.push_back(diagramToXml(*page, options.pageEncoding));
        else
            xml.root.children.push_back(std::get<XmlNode>(entry));
    }
    return xml;
}

QByteArray writeDocument(const Document& document, const WriteOptions& options)
{
    const std::string text = serializeDocument(toXml(document, options));
    return QByteArray(text.data(), static_cast<qsizetype>(text.size()));
}

bool writeFile(const Document& document, const QString& path, const WriteOptions& options, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcFormat) << "cannot write" << path << file.errorString();
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray bytes = writeDocument(document, options);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        qCWarning(lcFormat) << "cannot write" << path << file.errorString();
        if (error)
            *error = file.errorString();
        return false;
    }
    qCDebug(lcFormat) << "wrote" << bytes.size() << "bytes to" << path;
    return true;
}

} // namespace qraw::format
