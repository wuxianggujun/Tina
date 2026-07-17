#include "GlfwNativeWindowBinding.hpp"

#include <tina/platform/PlatformErrors.hpp>

#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace Tina::Platform::Detail {

Core::Result<Integration::Detail::NativeWindowBinding> readGlfwWaylandWindowBinding(GLFWwindow* window) noexcept
{
    wl_display* nativeDisplay = glfwGetWaylandDisplay();
    wl_surface* nativeWindow = glfwGetWaylandWindow(window);
    if (nativeDisplay == nullptr || nativeWindow == nullptr)
    {
        return Core::failure(PlatformErrorCode::WindowSurfaceUnavailable,
                             "GLFW did not provide a Wayland window binding");
    }
    return Integration::Detail::NativeWindowBinding{
        .kind = Integration::Detail::NativeWindowBindingKind::Wayland,
        .nativeDisplay = reinterpret_cast<std::uintptr_t>(nativeDisplay),
        .nativeWindow = reinterpret_cast<std::uintptr_t>(nativeWindow),
        .bindingRevision = 1,
    };
}

} // namespace Tina::Platform::Detail
