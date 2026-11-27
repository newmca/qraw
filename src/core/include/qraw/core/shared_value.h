#pragma once

// SharedValue: an attribute or style value that is cheap to copy when it is
// large.
//
// draw.io embeds images in the style text (`image=data:image/png,<base64>`),
// so one cell can carry a style of several megabytes, and every copy of the
// cell (undo snapshots, clipboard, duplicate), every parse of its style and
// every resolved style used to copy that text again. A SharedValue keeps
// values below kSharedValueThreshold in a plain std::string and moves larger
// ones into an immutable, reference-counted buffer: copying the value, its
// Attributes, its Cell or its Style bumps a count. Setting a value replaces
// the buffer, it never edits it, which keeps D-015's snapshot semantics (a
// snapshot is a value, edits to the live cell never reach it) and makes the
// buffers safe to read from any thread.
//
// The buffer memoises two derived things so that nothing rescans a megabyte:
// its content hash (the key of the render image cache) and, for a style
// attribute, its parsed core::Style (see Cell::style()).
//
// Standard library only, like the rest of qraw_core.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <typeinfo>

namespace qraw::core {

// Values of at least this many bytes live in a shared buffer.
inline constexpr std::size_t kSharedValueThreshold = 16 * 1024;

// 64-bit content hash (a multiply-xorshift over 8-byte words; deterministic
// on the little-endian platforms Qt 6 runs on). Never 0.
[[nodiscard]] std::uint64_t hashBytes(std::string_view bytes) noexcept;

// Number of shared buffers that were allocated by copying bytes (a copy of a
// std::string or string_view into a new buffer, not a refcount bump and not a
// move of an rvalue string). A paint or an edit of another cell leaves it
// unchanged; the benchmark reads it as the work counter `largeValueCopies`.
[[nodiscard]] std::uint64_t largeValueCopies() noexcept;
void resetLargeValueCopies() noexcept;

class SharedValue {
public:
    struct Buffer {
        std::string text;

        // hashBytes(text), computed on first use.
        [[nodiscard]] std::uint64_t hash() const noexcept;

        // The value derived from the text, built once by `make` (a style
        // attribute's parsed Style). One type per buffer: asking for another
        // type returns nullptr.
        template <class T, class Make>
        [[nodiscard]] std::shared_ptr<const T> memo(Make&& make) const
        {
            std::lock_guard lock(memoMutex_);
            if (!memo_) {
                memo_ = std::make_shared<const T>(make());
                memoType_ = &typeid(T);
            }
            if (*memoType_ != typeid(T))
                return nullptr;
            return std::static_pointer_cast<const T>(memo_);
        }

    private:
        mutable std::atomic<std::uint64_t> hash_{0};
        mutable std::mutex memoMutex_;
        mutable std::shared_ptr<const void> memo_;
        mutable const std::type_info* memoType_ = nullptr;
    };

    SharedValue() = default;
    SharedValue(std::string_view text);
    SharedValue(const std::string& text) : SharedValue(std::string_view(text)) {}
    SharedValue(const char* text) : SharedValue(std::string_view(text)) {}
    // Takes the bytes over without copying them.
    SharedValue(std::string&& text);

    SharedValue& operator=(std::string_view text);
    SharedValue& operator=(const std::string& text) { return *this = std::string_view(text); }
    SharedValue& operator=(const char* text) { return *this = std::string_view(text); }
    SharedValue& operator=(std::string&& text);

    [[nodiscard]] const std::string& str() const noexcept { return big_ ? big_->text : small_; }
    operator const std::string&() const noexcept { return str(); }
    operator std::string_view() const noexcept { return str(); }

    [[nodiscard]] bool empty() const noexcept { return str().empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return str().size(); }
    [[nodiscard]] const char* data() const noexcept { return str().data(); }

    // True when the value lives in a shared buffer.
    [[nodiscard]] bool isShared() const noexcept { return big_ != nullptr; }
    [[nodiscard]] const std::shared_ptr<const Buffer>& buffer() const noexcept { return big_; }
    // Both are the same shared buffer; constant time.
    [[nodiscard]] bool sameBuffer(const SharedValue& other) const noexcept { return big_ && big_ == other.big_; }
    // hashBytes(str()), memoised on a shared buffer.
    [[nodiscard]] std::uint64_t contentHash() const noexcept;

    friend bool operator==(const SharedValue& a, const SharedValue& b) noexcept
    {
        return a.sameBuffer(b) || a.str() == b.str();
    }
    friend bool operator==(const SharedValue& a, std::string_view b) noexcept { return std::string_view(a.str()) == b; }
    friend bool operator==(const SharedValue& a, const std::string& b) noexcept { return a.str() == b; }
    friend bool operator==(const SharedValue& a, const char* b) noexcept { return a.str() == b; }

private:
    std::string small_;
    std::shared_ptr<const Buffer> big_;
};

} // namespace qraw::core
