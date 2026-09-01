#pragma once

#include <tina/core/error/Result.hpp>

#include <string>
#include <string_view>

namespace Tina::Core {

// Resolves the directory holding the running executable, as UTF-8 with no
// trailing separator.
//
// This is the anchor for read-only data shipped beside the program. An installed
// game is copied wherever its player wants it and is usually launched from
// somewhere else entirely, so neither a build-time absolute path nor the process
// working directory can find its own assets; only the executable location can.
// Tina::Core::userApplicationDirectory answers the unrelated question of where a
// product may write, which is why that lives in UserPaths.hpp rather than here.
//
// Asking the OS is unavoidable, since a process cannot otherwise know where it
// was loaded from. Nothing here reads, creates, or tests for a file, so the
// result is a path that may not exist and existence is the caller's to check.
//
// Windows reads the loaded module path; elsewhere it reads /proc/self/exe.
// Returns NotFound when the executable path cannot be determined, which on Linux
// means /proc is not mounted, and Internal when the OS reports a path that is not
// valid UTF-8 or has no parent directory.
[[nodiscard]] Result<std::string> applicationDirectory();

// Convenience join of applicationDirectory() and a relative path below it.
//
// relativePath may name nested directories, because shipped data is laid out in
// them, and must use '/' as its separator on every platform. It is joined
// verbatim, so a value that could resolve outside the executable directory is
// rejected rather than sanitized: an absolute path, a '\' separator, a '.' or
// '..' component, and an empty component all return InvalidArgument.
[[nodiscard]] Result<std::string> applicationFilePath(std::string_view relativePath);

} // namespace Tina::Core
