#pragma once

#include "qraw/core/attributes.h"
#include "qraw/core/page.h"
#include "qraw/core/xml_node.h"

#include <cstddef>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace qraw::core {

// Which top-level form the file used. Written back the same way.
enum class DocumentKind {
    MxFile,    // <mxfile><diagram>...</diagram>...</mxfile> (.drawio)
    BareModel, // a single <mxGraphModel> (.xml)
};

// A whole file: pages plus everything around them that must survive a save.
class Document {
public:
    using Entry = std::variant<Page, XmlNode>;

    [[nodiscard]] DocumentKind kind() const { return kind_; }
    void setKind(DocumentKind kind) { kind_ = kind; }

    // <mxfile> attributes (host, modified, agent, compressed, unknown ones).
    [[nodiscard]] Attributes& fileAttributes() { return fileAttributes_; }
    [[nodiscard]] const Attributes& fileAttributes() const { return fileAttributes_; }

    // <mxfile> children in order: pages interleaved with foreign nodes.
    [[nodiscard]] const std::vector<Entry>& entries() const { return entries_; }
    void appendEntry(Entry entry) { entries_.push_back(std::move(entry)); }

    [[nodiscard]] std::size_t pageCount() const;
    [[nodiscard]] Page* page(std::size_t index);
    [[nodiscard]] const Page* page(std::size_t index) const;
    [[nodiscard]] std::vector<Page*> pages();
    [[nodiscard]] std::vector<const Page*> pages() const;
    [[nodiscard]] Page* findPage(std::string_view id);
    // Inserts before the `index`-th page (end when absent or out of range).
    Page& addPage(Page page, std::optional<std::size_t> index = std::nullopt);
    std::optional<Page> takePage(std::size_t index);

    // Entry-level access, used by commands to restore exact positions even
    // when foreign nodes are interleaved with the pages.
    // Position in entries() of the `pageIndex`-th page.
    [[nodiscard]] std::optional<std::size_t> pageEntryIndex(std::size_t pageIndex) const;
    // Inserts `page` before entries()[entryIndex] (entryIndex ==
    // entries().size() appends). Returns nullptr and changes nothing when
    // entryIndex is out of range.
    Page* insertPageAt(std::size_t entryIndex, Page page);

    // XML declaration, and comments/PIs/DOCTYPE around the root element.
    [[nodiscard]] XmlDeclaration& declaration() { return declaration_; }
    [[nodiscard]] const XmlDeclaration& declaration() const { return declaration_; }
    [[nodiscard]] std::vector<XmlNode>& prolog() { return prolog_; }
    [[nodiscard]] const std::vector<XmlNode>& prolog() const { return prolog_; }
    [[nodiscard]] std::vector<XmlNode>& epilog() { return epilog_; }
    [[nodiscard]] const std::vector<XmlNode>& epilog() const { return epilog_; }

private:
    [[nodiscard]] std::vector<Entry>::iterator pageEntry(std::size_t index);

    DocumentKind kind_ = DocumentKind::MxFile;
    Attributes fileAttributes_;
    std::vector<Entry> entries_;
    XmlDeclaration declaration_;
    std::vector<XmlNode> prolog_;
    std::vector<XmlNode> epilog_;
};

} // namespace qraw::core
