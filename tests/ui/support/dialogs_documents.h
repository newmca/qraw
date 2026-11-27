#pragma once
// Documents for the dialog tests.

#include "support/test_support.h"

namespace qraw::ui::test {

// Two pages: the simple diagram twice, the second with page id "p2".
inline core::Document twoPageDocument()
{
    const std::string xml = kSimpleDiagram;
    const std::string closing = "</diagram>";
    const auto begin = xml.find("<diagram");
    const auto end = xml.find(closing) + closing.size();
    const std::string first = xml.substr(begin, end - begin);
    std::string second = first;
    second.replace(second.find("id=\"p1\""), std::string("id=\"p1\"").size(), "id=\"p2\"");
    return documentFromXml("<mxfile>" + first + second + "</mxfile>");
}

} // namespace qraw::ui::test
