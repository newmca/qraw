#pragma once
// What a bug report needs to know about this build and this machine.

#include <QString>

namespace qraw::diag {

[[nodiscard]] QString versionString();   // Qraw's project version
[[nodiscard]] QString gitCommit();       // short hash at configure time, or "unknown"
[[nodiscard]] QString qtVersionString(); // "6.8.2 (built with 6.8.1)"
[[nodiscard]] QString systemDescription(); // "Fedora Linux 44 (KDE Plasma), kernel 7.3.4, x86_64"

} // namespace qraw::diag
