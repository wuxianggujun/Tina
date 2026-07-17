#pragma once

#include "../../integration/WindowSurfaceLeaseAccess.hpp"

struct GLFWwindow;

namespace Tina::Platform::Detail {

[[nodiscard]] Core::Result<Integration::Detail::NativeWindowBinding>
readGlfwNativeWindowBinding(GLFWwindow* window) noexcept;

#if defined(_WIN32)
[[nodiscard]] Core::Result<Integration::Detail::NativeWindowBinding>
readGlfwWin32WindowBinding(GLFWwindow* window) noexcept;
#elif defined(__linux__)
[[nodiscard]] Core::Result<Integration::Detail::NativeWindowBinding>
readGlfwX11WindowBinding(GLFWwindow* window) noexcept;

#if defined(TINA_GLFW_ENABLE_WAYLAND)
[[nodiscard]] Core::Result<Integration::Detail::NativeWindowBinding>
readGlfwWaylandWindowBinding(GLFWwindow* window) noexcept;
#endif
#endif

} // namespace Tina::Platform::Detail
