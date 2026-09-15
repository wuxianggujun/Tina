#pragma once

#include <tina/core/error/Result.hpp>

#include <string_view>

namespace Tina::Platform {

// Opens the native file manager and selects one existing filesystem path.
//
// This is a capability separate from the backend that owns it. Returning
// nullptr from IPlatformBackend::shellReveal() is how a backend states that
// the host has no file-manager reveal: Headless, mobile, the browser, and
// Linux desktop currently have none. Windows GLFW implements this with
// SHOpenFolderAndSelectItems.
//
// Implementations are thread-affine to the owning backend's owner thread.
// The argument must be a strict UTF-8 absolute path without embedded NUL;
// relative paths, empty strings, and missing files fail closed. Public
// headers never mention HWND, PIDLIST, or other native types.
class IShellReveal {
  public:
    virtual ~IShellReveal() noexcept = default;

    IShellReveal(const IShellReveal&) = delete;
    IShellReveal& operator=(const IShellReveal&) = delete;
    IShellReveal(IShellReveal&&) = delete;
    IShellReveal& operator=(IShellReveal&&) = delete;

    [[nodiscard]] virtual Core::Status revealPath(std::string_view utf8AbsolutePath) = 0;

  protected:
    IShellReveal() noexcept = default;
};

} // namespace Tina::Platform
