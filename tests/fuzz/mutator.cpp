#include "mutator.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace qraw::fuzz {

namespace {

std::uint64_t splitmix64(std::uint64_t& state)
{
    state += 0x9e3779b97f4a7c15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

std::uint64_t rotl(std::uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

// FNV-1a, so the target name takes part in the iteration seed.
std::uint64_t hashName(std::string_view name)
{
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (const char c : name) {
        h ^= static_cast<unsigned char>(c);
        h *= 0x100000001b3ULL;
    }
    return h;
}

constexpr std::array<std::string_view, 15> kInterestingNumbers = {
    "0", "-1", "1", "1e309", "nan", "inf", "-inf", "-0", "2147483648", "4294967296",
    "-2147483649", "9223372036854775807", "0.0000001", "1e-320",
    "99999999999999999999999999999999999999999999999999",
};

constexpr std::array<unsigned char, 12> kInterestingBytes = {0x00, 0xff, '<', '>', '"', '&', ';', '=', '\'', '\n', '%', 0x80};

bool isNumberChar(char c)
{
    return std::isdigit(static_cast<unsigned char>(c)) || c == '.';
}

} // namespace

Rng::Rng(std::uint64_t seed)
{
    for (auto& word : state_)
        word = splitmix64(seed);
}

std::uint64_t Rng::next()
{
    const std::uint64_t result = rotl(state_[1] * 5, 7) * 9;
    const std::uint64_t t = state_[1] << 17;
    state_[2] ^= state_[0];
    state_[3] ^= state_[1];
    state_[1] ^= state_[2];
    state_[0] ^= state_[3];
    state_[2] ^= t;
    state_[3] = rotl(state_[3], 45);
    return result;
}

std::uint64_t Rng::below(std::uint64_t bound)
{
    return bound == 0 ? 0 : next() % bound;
}

std::uint64_t Rng::between(std::uint64_t lo, std::uint64_t hi)
{
    return hi <= lo ? lo : lo + below(hi - lo + 1);
}

bool Rng::chance(std::uint64_t oneIn)
{
    return below(oneIn) == 0;
}

Rng iterationRng(std::uint64_t seed, std::string_view targetName, std::uint64_t iteration)
{
    std::uint64_t state = seed ^ (hashName(targetName) * 0x2545f4914f6cdd1dULL);
    state += iteration * 0x9e3779b97f4a7c15ULL;
    return Rng(splitmix64(state));
}

Mutator::Mutator(std::vector<DictionaryEntry> dictionary, std::size_t maxLength)
    : dictionary_(std::move(dictionary)), maxLength_(maxLength)
{
}

void Mutator::mutate(Rng& rng, std::string& data, const std::vector<std::string>& pool) const
{
    // One input in twenty stays a pristine seed: the harness then also checks
    // that the seeds themselves satisfy the invariants.
    if (!rng.chance(20)) {
        const std::uint64_t count = 1 + rng.below(rng.chance(4) ? 6 : 2);
        for (std::uint64_t i = 0; i < count; ++i)
            applyOne(rng, data, pool);
    }
    if (data.size() > maxLength_)
        data.resize(maxLength_);
}

void Mutator::applyOne(Rng& rng, std::string& data, const std::vector<std::string>& pool) const
{
    const auto position = [&](std::size_t extra = 0) { return static_cast<std::size_t>(rng.below(data.size() + extra)); };
    const auto chunkLength = [&](std::size_t at) {
        const std::size_t room = data.size() - at;
        const std::size_t cap = rng.chance(8) ? 4096 : 32;
        return static_cast<std::size_t>(rng.between(1, std::min(room, cap)));
    };
    const auto insertAt = [&](std::size_t at, std::string_view text) {
        if (data.size() + text.size() <= maxLength_)
            data.insert(at, text);
    };

    enum Op { FlipBit, SetByte, XorByte, InsertBytes, DeleteChunk, DuplicateChunk, SwapChunks, Truncate, Splice,
              ReplaceNumber, DictionaryInsert, DictionaryOverwrite, OpCount };
    Op op = static_cast<Op>(rng.below(OpCount));
    if (!dictionary_.empty() && rng.chance(3))
        op = rng.chance(4) ? DictionaryOverwrite : DictionaryInsert; // the dictionary pays off most
    if (rng.chance(8))
        op = ReplaceNumber; // value-level damage keeps the XML well-formed, so it reaches the semantic layer
    if (data.empty() && op != InsertBytes && op != DictionaryInsert && op != Splice)
        op = InsertBytes;

    switch (op) {
    case FlipBit:
        data[position()] ^= static_cast<char>(1u << rng.below(8));
        break;
    case SetByte:
        data[position()] = static_cast<char>(kInterestingBytes[rng.below(kInterestingBytes.size())]);
        break;
    case XorByte:
        data[position()] ^= static_cast<char>(1 + rng.below(255));
        break;
    case InsertBytes: {
        std::string bytes(static_cast<std::size_t>(rng.between(1, 8)), '\0');
        for (char& c : bytes)
            c = static_cast<char>(rng.below(256));
        insertAt(position(1), bytes);
        break;
    }
    case DeleteChunk: {
        const std::size_t at = position();
        data.erase(at, chunkLength(at));
        break;
    }
    case DuplicateChunk: {
        const std::size_t at = position();
        const std::string chunk = data.substr(at, chunkLength(at));
        insertAt(position(1), chunk);
        break;
    }
    case SwapChunks: {
        const std::size_t a = position();
        const std::size_t b = position();
        const std::size_t length = std::min({chunkLength(a), chunkLength(b), a > b ? a - b : b - a});
        if (length > 0)
            std::swap_ranges(data.begin() + static_cast<std::ptrdiff_t>(a), data.begin() + static_cast<std::ptrdiff_t>(a + length),
                             data.begin() + static_cast<std::ptrdiff_t>(b));
        break;
    }
    case Truncate:
        data.resize(position());
        break;
    case Splice: {
        if (pool.empty())
            break;
        const std::string& other = pool[rng.below(pool.size())];
        if (other.empty())
            break;
        const std::size_t from = static_cast<std::size_t>(rng.below(other.size()));
        const std::size_t length = static_cast<std::size_t>(rng.between(1, std::min<std::size_t>(other.size() - from, 4096)));
        insertAt(position(1), std::string_view(other).substr(from, length));
        break;
    }
    case ReplaceNumber: {
        // The first number at or after a random position (wrapping once).
        const std::size_t start = position();
        for (std::size_t step = 0; step < data.size(); ++step) {
            const std::size_t i = (start + step) % data.size();
            if (!isNumberChar(data[i]))
                continue;
            std::size_t begin = i;
            while (begin > 0 && isNumberChar(data[begin - 1]))
                --begin;
            std::size_t end = i;
            while (end < data.size() && isNumberChar(data[end]))
                ++end;
            data.replace(begin, end - begin, kInterestingNumbers[rng.below(kInterestingNumbers.size())]);
            break;
        }
        break;
    }
    case DictionaryInsert:
    case DictionaryOverwrite: {
        if (dictionary_.empty())
            break;
        const DictionaryEntry& entry = dictionary_[rng.below(dictionary_.size())];
        std::string text;
        const std::size_t repeat = static_cast<std::size_t>(rng.between(1, entry.maxRepeat));
        for (std::size_t i = 0; i < repeat && text.size() <= maxLength_; ++i)
            text += entry.text;
        if (op == DictionaryOverwrite && !data.empty()) {
            const std::size_t at = position();
            data.replace(at, std::min(text.size(), data.size() - at), text);
        } else {
            insertAt(position(1), text);
        }
        break;
    }
    case OpCount:
        break;
    }
}

} // namespace qraw::fuzz
