#pragma once
// Text building that is safe inside a signal handler: fixed-size buffers, no allocation, no locale,
// no stdio. The crash handler writes its report with these (POSIX allows only async-signal-safe
// functions there); they are plain C++ so that the tests can check the output.

#include <cstddef>
#include <cstdint>

namespace qraw::diag {

// A text of at most N-1 characters. Appending beyond the capacity drops the excess.
template <std::size_t N>
class FixedText {
public:
    FixedText& append(const char* text)
    {
        for (; text != nullptr && *text != '\0' && size_ + 1 < N; ++text)
            data_[size_++] = *text;
        data_[size_] = '\0';
        return *this;
    }
    FixedText& append(char c)
    {
        if (size_ + 1 < N)
            data_[size_++] = c;
        data_[size_] = '\0';
        return *this;
    }
    FixedText& appendDecimal(std::uint64_t value)
    {
        char digits[20];
        std::size_t count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        while (count > 0)
            append(digits[--count]);
        return *this;
    }
    // Two or more digits, zero padded ("07", "2026").
    FixedText& appendDecimal(std::uint64_t value, int minimumDigits)
    {
        int digits = 1;
        for (std::uint64_t rest = value; rest >= 10; rest /= 10)
            ++digits;
        for (int i = digits; i < minimumDigits; ++i)
            append('0');
        return appendDecimal(value);
    }
    // "0x1f2e" (lower case, no padding).
    FixedText& appendHex(std::uint64_t value)
    {
        append("0x");
        char digits[16];
        std::size_t count = 0;
        do {
            digits[count++] = "0123456789abcdef"[value & 0xf];
            value >>= 4;
        } while (value != 0);
        while (count > 0)
            append(digits[--count]);
        return *this;
    }
    [[nodiscard]] const char* c_str() const { return data_; }
    [[nodiscard]] std::size_t size() const { return size_; }

private:
    char data_[N] = {};
    std::size_t size_ = 0;
};

// A UTC time as "20261126T123456Z" (file names) or "2026-11-26 12:34:56 UTC" (report text), from
// seconds since 1970 (what time() returns). gmtime() is not async-signal-safe, so the calendar is
// computed here.
template <std::size_t N>
void appendUtcTime(FixedText<N>& text, std::int64_t epochSeconds, bool forFileName)
{
    const std::int64_t days = epochSeconds >= 0 ? epochSeconds / 86400 : -((-epochSeconds + 86399) / 86400);
    const std::int64_t secondOfDay = epochSeconds - days * 86400;
    // Days since 1970-01-01 to a civil date (H. Hinnant's algorithm, valid for the whole range used).
    const std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t dayOfEra = z - era * 146097;
    const std::int64_t yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const std::int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const std::int64_t shiftedMonth = (5 * dayOfYear + 2) / 153;
    const std::int64_t day = dayOfYear - (153 * shiftedMonth + 2) / 5 + 1;
    const std::int64_t month = shiftedMonth < 10 ? shiftedMonth + 3 : shiftedMonth - 9;
    const std::int64_t year = yearOfEra + era * 400 + (month <= 2 ? 1 : 0);
    const auto hour = static_cast<std::uint64_t>(secondOfDay / 3600);
    const auto minute = static_cast<std::uint64_t>(secondOfDay / 60 % 60);
    const auto second = static_cast<std::uint64_t>(secondOfDay % 60);

    text.appendDecimal(static_cast<std::uint64_t>(year), 4);
    if (forFileName) {
        text.appendDecimal(static_cast<std::uint64_t>(month), 2).appendDecimal(static_cast<std::uint64_t>(day), 2);
        text.append('T').appendDecimal(hour, 2).appendDecimal(minute, 2).appendDecimal(second, 2).append('Z');
    } else {
        text.append('-').appendDecimal(static_cast<std::uint64_t>(month), 2);
        text.append('-').appendDecimal(static_cast<std::uint64_t>(day), 2);
        text.append(' ').appendDecimal(hour, 2).append(':').appendDecimal(minute, 2);
        text.append(':').appendDecimal(second, 2).append(" UTC");
    }
}

// "SIGSEGV" for the fatal signals the crash handler catches (POSIX numbers of Linux and macOS; they
// differ for SIGBUS), "signal" otherwise.
[[nodiscard]] const char* fatalSignalName(int signalNumber);

} // namespace qraw::diag
