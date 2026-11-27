#pragma once

// Deterministic input mutation for the fuzz harness. Nothing here depends on
// the standard library's random facilities, whose output differs between
// implementations: the same seed must give the same inputs on every platform
// and build (D-028).

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::fuzz {

// xoshiro256** seeded through splitmix64.
class Rng {
public:
    explicit Rng(std::uint64_t seed);

    std::uint64_t next();
    // A value in [0, bound); 0 when bound is 0. Plain modulo reduction: the
    // bias is irrelevant for fuzzing and the result is the same everywhere.
    std::uint64_t below(std::uint64_t bound);
    // A value in [lo, hi] (inclusive).
    std::uint64_t between(std::uint64_t lo, std::uint64_t hi);
    // True with probability 1 / `oneIn`.
    bool chance(std::uint64_t oneIn);

private:
    std::uint64_t state_[4];
};

// The Rng of one iteration: independent of every other iteration, so
// `--dump TARGET:ITERATION` regenerates an input without replaying the run.
Rng iterationRng(std::uint64_t seed, std::string_view targetName, std::uint64_t iteration);

// A dictionary token, inserted once or as a long run (deep nesting).
struct DictionaryEntry {
    std::string text;
    std::size_t maxRepeat = 1; // the run length is drawn from [1, maxRepeat]
};

class Mutator {
public:
    Mutator(std::vector<DictionaryEntry> dictionary, std::size_t maxLength);

    // Applies 0 to 6 random mutations to `data`. `pool` supplies the second
    // input of splice operations (may be empty). The result never exceeds the
    // maximum length.
    void mutate(Rng& rng, std::string& data, const std::vector<std::string>& pool) const;

    [[nodiscard]] std::size_t maxLength() const { return maxLength_; }

private:
    void applyOne(Rng& rng, std::string& data, const std::vector<std::string>& pool) const;

    std::vector<DictionaryEntry> dictionary_;
    std::size_t maxLength_;
};

} // namespace qraw::fuzz
