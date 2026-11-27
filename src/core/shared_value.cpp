#include "qraw/core/shared_value.h"

#include <cstring>

namespace qraw::core {

namespace {

std::atomic<std::uint64_t> g_largeValueCopies{0};

std::shared_ptr<const SharedValue::Buffer> makeBuffer(std::string&& text)
{
    auto buffer = std::make_shared<SharedValue::Buffer>();
    buffer->text = std::move(text);
    return buffer;
}

} // namespace

std::uint64_t hashBytes(std::string_view bytes) noexcept
{
    constexpr std::uint64_t kMul = 0x9E3779B97F4A7C15ull;
    std::uint64_t h = 0xCBF29CE484222325ull ^ (static_cast<std::uint64_t>(bytes.size()) * kMul);
    const char* p = bytes.data();
    std::size_t n = bytes.size();
    while (n >= 8) {
        std::uint64_t word;
        std::memcpy(&word, p, 8);
        h = (h ^ word) * kMul;
        h ^= h >> 29;
        p += 8;
        n -= 8;
    }
    std::uint64_t tail = 0;
    if (n > 0)
        std::memcpy(&tail, p, n);
    h = (h ^ tail) * kMul;
    h ^= h >> 32;
    h *= 0xD6E8FEB86659FD93ull;
    h ^= h >> 32;
    return h == 0 ? 1 : h;
}

std::uint64_t largeValueCopies() noexcept { return g_largeValueCopies.load(std::memory_order_relaxed); }
void resetLargeValueCopies() noexcept { g_largeValueCopies.store(0, std::memory_order_relaxed); }

std::uint64_t SharedValue::Buffer::hash() const noexcept
{
    std::uint64_t h = hash_.load(std::memory_order_relaxed);
    if (h == 0) {
        h = hashBytes(text);
        hash_.store(h, std::memory_order_relaxed);
    }
    return h;
}

SharedValue::SharedValue(std::string_view text)
{
    if (text.size() >= kSharedValueThreshold) {
        g_largeValueCopies.fetch_add(1, std::memory_order_relaxed);
        big_ = makeBuffer(std::string(text));
    } else {
        small_.assign(text);
    }
}

SharedValue::SharedValue(std::string&& text)
{
    if (text.size() >= kSharedValueThreshold)
        big_ = makeBuffer(std::move(text));
    else
        small_ = std::move(text);
}

// `text` may point into this value: the new value is built before the old one goes.
SharedValue& SharedValue::operator=(std::string_view text) { return *this = SharedValue(text); }
SharedValue& SharedValue::operator=(std::string&& text) { return *this = SharedValue(std::move(text)); }

std::uint64_t SharedValue::contentHash() const noexcept
{
    return big_ ? big_->hash() : hashBytes(small_);
}

} // namespace qraw::core
