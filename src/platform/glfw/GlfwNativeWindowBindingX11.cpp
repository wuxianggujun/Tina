#include "GlfwNativeWindowBinding.hpp"

#include <tina/platform/PlatformErrors.hpp>

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace Tina::Platform::Detail {

Core::Result<Integration::Detail::NativeWindowBinding> readGlfwX11WindowBinding(GLFWwindow* window) noexcept
{
    Display* nativeDisplay = glfwGetX11Display();
    const ::Window nativeWindow = glfwGetX11Window(window);
    if (nativeDisplay == nullptr || nativeWindow == 0)
    {
        return Core::failure(PlatformErrorCode::WindowSurfaceUnavailable, "GLFW did not provide an X11 window binding");
    }
    return Integration::Detail::NativeWindowBinding{
        .kind = Integration::Detail::NativeWindowBindingKind::X11,
        .nativeDisplay = reinterpret_cast<std::uintptr_t>(nativeDisplay),
        .nativeWindow = static_cast<std::uintptr_t>(nativeWindow),
        .bindingRevision = 1,
    };
}

} // namespace Tina::Platform::Detail
