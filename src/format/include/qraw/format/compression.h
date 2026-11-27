#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace qraw::format {

// draw.io page compression: base64(deflateRaw(encodeURIComponent(xml))).

[[nodiscard]] std::string compressDiagram(std::string_view xml);

// Accepts the current format plus the legacy variants: payloads that were
// deflated without URI encoding, and zlib/gzip-wrapped streams. Returns
// nullopt when the text is not a decodable XML payload.
[[nodiscard]] std::optional<std::string> decompressDiagram(std::string_view base64);

// JavaScript encodeURIComponent / decodeURIComponent on UTF-8 bytes. The
// decoder is lenient: malformed escapes are kept literally.
[[nodiscard]] std::string encodeUriComponent(std::string_view text);
[[nodiscard]] std::string decodeUriComponent(std::string_view text);

// zlib primitives. windowBits as in zlib: -15 raw deflate, 15 zlib, 47 auto zlib/gzip.
[[nodiscard]] std::string deflateBytes(std::string_view data, int windowBits);
[[nodiscard]] std::optional<std::string> inflateBytes(std::string_view data, int windowBits);

} // namespace qraw::format
