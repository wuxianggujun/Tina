#include "GlfwNativeWindowBinding.hpp"

#include <tina/platform/PlatformErrors.hpp>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace Tina::Platform::Detail {

Core::Result<Integration::Detail::NativeWindowBinding> readGlfwWin32WindowBinding(GLFWwindow* window) noexcept
{
    const HWND nativeWindow = glfwGetWin32Window(window);
    if (nativeWindow == nullptr)
    {
        return Core::failure(PlatformErrorCode::WindowSurfaceUnavailable,
                             "GLFW did not provide a Win32 window binding");
    }
    return Integration::Detail::NativeWindowBinding{
        .kind = Integration::Detail::NativeWindowBindingKind::Win32,
        .nativeDisplay = 0,
        .nativeWindow = reinterpret_cast<std::uintptr_t>(nativeWindow),
        .bindingRevision = 1,
    };
}

} // namespace Tina::Platform::Detail
