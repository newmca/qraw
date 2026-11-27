#pragma once
// Helpers shared by the benchmark document generators: a deterministic PRNG,
// identical on every platform and compiler (no std distributions, whose output
// is unspecified), and the XML text helpers.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace qraw::bench {

// splitmix64: small, fast and identical everywhere.
class Prng {
public:
    explicit Prng(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next()
    {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // Uniform enough for a benchmark: modulo bias is irrelevant here.
    std::size_t below(std::size_t bound) { return static_cast<std::size_t>(next() % bound); }

private:
    std::uint64_t state_;
};

// A coordinate as draw.io writes it: whole numbers.
inline std::string num(double value)
{
    return std::to_string(static_cast<long long>(std::llround(value)));
}

inline std::string escapeAttribute(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

} // namespace qraw::bench
