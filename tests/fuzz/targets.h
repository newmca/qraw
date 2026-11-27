#pragma once

// The five fuzz targets. A target turns an iteration number into an input
// (`generate`, deterministic) and checks the invariants of one parser on it
// (`run`). Invariants that do not hold are reported as a failure string;
// crashes and hangs are caught by the process-level handlers in main.cpp.

#include "mutator.h"
#include "seeds.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::fuzz {

struct Target {
    std::string name;
    // The input of one iteration, from an iteration-specific Rng.
    std::function<std::string(Rng&)> generate;
    // Runs the parser(s) on `input`. Returns an empty string when every
    // invariant holds, otherwise a one-line description of the first
    // violation.
    std::function<std::string(std::string_view input)> run;
};

// Builds all targets in a fixed order: codec, compression, style, stencil,
// html. `seeds` may be empty when only `run` is needed.
std::vector<Target> makeTargets(std::shared_ptr<const SeedSets> seeds, std::size_t maxLength);

} // namespace qraw::fuzz
