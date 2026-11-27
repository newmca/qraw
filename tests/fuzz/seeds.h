#pragma once

// Seed inputs for the fuzz targets: the regression corpus, the compat
// fixtures and the render goldens, plus the parts of them each target needs
// (style values, HTML labels, compressed pages, stencil shapes). Everything
// is collected in sorted order so a seed number always means the same input.

#include <cstddef>
#include <string>
#include <vector>

namespace qraw::fuzz {

struct SeedSets {
    std::vector<std::string> documents;         // whole .drawio / .xml files (up to the maximum length)
    std::vector<std::string> compressedPayloads; // base64 text of compressed <diagram> pages
    std::vector<std::string> inflatedModels;    // the same pages, inflated (XML text)
    std::vector<std::string> styles;            // every distinct style="..." value, unescaped
    std::vector<std::string> htmlLabels;        // value/label of html=1 cells, plus tricky snippets
    std::vector<std::string> stencilShapes;     // <shape> elements, serialized
    std::vector<std::string> inlineStencils;    // shape=stencil(...) style values
    std::vector<std::string> packedStencilSets; // inflated packed stencil sets
};

// Collects the seeds below `repositoryRoot`. Inputs longer than `maxLength`
// are skipped. Never fails: a missing directory just yields fewer seeds.
SeedSets collectSeeds(const std::string& repositoryRoot, std::size_t maxLength);

} // namespace qraw::fuzz
