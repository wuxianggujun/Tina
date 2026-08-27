#pragma once

#include <tina/core/error/Result.hpp>

#include <string>
#include <string_view>

namespace Tina::Core {

// Which per-user location a product is asking for. Config is for settings a user
// changed deliberately and expects to survive a reinstall; State is for
// recoverable runtime data the product can rebuild if it disappears.
enum class UserDirectoryKind : u8 {
    Config = 0,
    State = 1,
};

// Resolves a per-user directory for one application, as UTF-8. The path is
// composed, not created: callers write through Core::writeFile, whose
// createParents already builds missing parents, so nothing here touches the
// filesystem and a read-only environment cannot fail at resolve time.
//
// applicationName must be a single non-empty path segment with no separator,
// no drive letter, no NUL, and no '.' or '..'. It is joined verbatim, so a
// caller cannot escape the resolved base directory.
//
// Windows prefers %LOCALAPPDATA% then %APPDATA%. Elsewhere it follows the XDG
// basedir spec: $XDG_CONFIG_HOME or $XDG_STATE_HOME when set to an absolute
// path, else $HOME/.config or $HOME/.local/state. Returns NotFound when the
// environment provides no usable base, which is a real condition on stripped
// service accounts rather than a programming error.
[[nodiscard]] Result<std::string> userApplicationDirectory(
    std::string_view applicationName,
    UserDirectoryKind kind = UserDirectoryKind::Config);

// Convenience join of userApplicationDirectory() and one relative file name,
// validated by the same rules as applicationName. Separators stay '/', which
// every supported platform accepts.
[[nodiscard]] Result<std::string> userApplicationFilePath(
    std::string_view applicationName,
    std::string_view fileName,
    UserDirectoryKind kind = UserDirectoryKind::Config);

} // namespace Tina::Core
