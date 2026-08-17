#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/platform/Input.hpp>
#include <tina/platform/Window.hpp>

namespace Tina::Platform::Detail {

// Physical client-pixel placement consumed by the private Win32 IMM32 host.
// Keeping this value backend-owned prevents HWND/POINT/RECT from crossing the
// public Platform boundary and also gives non-Windows builds a deterministic
// conversion seam.
struct GlfwTextInputPlacementPixels final {
    i32 caretLeft = 0;
    i32 caretTop = 0;
    i32 caretRight = 0;
    i32 caretBottom = 0;
    i32 candidateX = 0;
    i32 candidateY = 0;
};

[[nodiscard]] Core::Result<GlfwTextInputPlacementPixels>
resolveGlfwTextInputPlacement(const TextInputPlacement& placement,
                              const WindowMetricsSnapshot& metrics) noexcept;

} // namespace Tina::Platform::Detail\n
