#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <cstddef>
#include <span>
#include <string_view>

namespace Tina::Core {

struct WriteFileConfig final {
    // When true (default), write to a sibling temp file then rename into place.
    bool atomicReplace = true;
    // Create parent directories if missing.
    bool createParents = true;
};

// Synchronously writes the entire buffer to utf8Path.
// Path is UTF-8 without embedded NUL. Does not canonicalize paths or follow async IO.
// atomicReplace uses a unique temp sibling and an OS-level same-directory
// replace; a failed replace does not remove the previous target.
[[nodiscard]] Status writeFile(std::string_view utf8Path, std::span<const std::byte> bytes,
                               WriteFileConfig config = {});

// Creates parent directories for utf8Path when missing (including intermediate parents).
[[nodiscard]] Status createParentDirectories(std::string_view utf8Path);

} // namespace Tina::Core
