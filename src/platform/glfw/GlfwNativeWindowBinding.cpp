#include "GlfwNativeWindowBinding.hpp"

#include <tina/platform/PlatformErrors.hpp>

#include <GLFW/glfw3.h>

namespace Tina::Platform::Detail {

Core::Result<Integration::Detail::NativeWindowBinding> readGlfwNativeWindowBinding(GLFWwindow* window) noexcept
{
    if (window == nullptr)
    {
        return Core::failure(PlatformErrorCode::WindowSurfaceUnavailable,
                             "The GLFW native window binding is unavailable");
    }

#if defined(_WIN32)
    if (glfwGetPlatform() != GLFW_PLATFORM_WIN32)
    {
        return Core::failure(PlatformErrorCode::WindowSurfaceUnavailable, "The GLFW runtime platform is not Win32");
    }
    return readGlfwWin32WindowBinding(window);
#elif defined(__linux__)
    switch (glfwGetPlatform())
    {
    case GLFW_PLATFORM_X11:
        return readGlfwX11WindowBinding(window);
#if defined(TINA_GLFW_ENABLE_WAYLAND)
    case GLFW_PLATFORM_WAYLAND:
        return readGlfwWaylandWindowBinding(window);
#endif
    default:
        return Core::failure(PlatformErrorCode::WindowSurfaceUnavailable,
                             "The GLFW runtime platform has no Tina native surface decoder");
    }
#else
    return Core::failure(PlatformErrorCode::WindowSurfaceUnavailable,
                         "This operating system has no Tina GLFW native surface decoder");
#endif
}

} // namespace Tina::Platform::Detail
