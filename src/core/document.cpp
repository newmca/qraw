#include "qraw/core/document.h"

namespace qraw::core {

std::size_t Document::pageCount() const
{
    std::size_t n = 0;
    for (const auto& e : entries_)
        n += std::holds_alternative<Page>(e) ? 1 : 0;
    return n;
}

std::vector<Document::Entry>::iterator Document::pageEntry(std::size_t index)
{
    std::size_t n = 0;
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (std::holds_alternative<Page>(*it) && n++ == index)
            return it;
    }
    return entries_.end();
}

Page* Document::page(std::size_t index)
{
    const auto it = pageEntry(index);
    return it == entries_.end() ? nullptr : &std::get<Page>(*it);
}

const Page* Document::page(std::size_t index) const { return const_cast<Document*>(this)->page(index); }

std::vector<Page*> Document::pages()
{
    std::vector<Page*> out;
    for (auto& e : entries_) {
        if (Page* p = std::get_if<Page>(&e))
            out.push_back(p);
    }
    return out;
}

std::vector<const Page*> Document::pages() const
{
    std::vector<const Page*> out;
    for (const auto& e : entries_) {
        if (const Page* p = std::get_if<Page>(&e))
            out.push_back(p);
    }
    return out;
}

Page* Document::findPage(std::string_view id)
{
    for (Page* p : pages()) {
        if (p->id() == id)
            return p;
    }
    return nullptr;
}

Page& Document::addPage(Page page, std::optional<std::size_t> index)
{
    const auto pos = index ? pageEntry(*index) : entries_.end();
    return std::get<Page>(*entries_.insert(pos, std::move(page)));
}

std::optional<Page> Document::takePage(std::size_t index)
{
    const auto it = pageEntry(index);
    if (it == entries_.end())
        return std::nullopt;
    Page taken = std::get<Page>(std::move(*it));
    entries_.erase(it);
    return taken;
}

std::optional<std::size_t> Document::pageEntryIndex(std::size_t pageIndex) const
{
    std::size_t n = 0;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (std::holds_alternative<Page>(entries_[i]) && n++ == pageIndex)
            return i;
    }
    return std::nullopt;
}

Page* Document::insertPageAt(std::size_t entryIndex, Page page)
{
    if (entryIndex > entries_.size())
        return nullptr;
    const auto it = entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(entryIndex), std::move(page));
    return &std::get<Page>(*it);
}

} // namespace qraw::core
