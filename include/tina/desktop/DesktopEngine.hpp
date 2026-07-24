#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/runtime/EngineHost.hpp>

#include <functional>
#include <memory>

#if !defined(__cpp_lib_move_only_function) || __cpp_lib_move_only_function < 202110L
#error "Tina Desktop requires C++23 std::move_only_function support"
#endif

namespace Tina::Desktop {

// Optional post-create wrap for the product WindowSurface RenderDevice (bgfx).
// Used by product gates / samples that need capture or diagnostics without
// hand-rolling EngineCompositionFactories. Must not expose bgfx/GLFW types.
// Empty → identity (device returned as created by Desktop bootstrap).
using WindowSurfaceRenderDeviceWrap =
    std::move_only_function<Core::Result<std::unique_ptr<Render::IRenderDevice>>(
        std::unique_ptr<Render::IRenderDevice> device)>;

struct CreateEngineOptions final {
    WindowSurfaceRenderDeviceWrap wrapWindowSurfaceRenderDevice{};
};

// Production Desktop composition: SteadyClock + GLFW WindowSurface + bounded
// TaskSystem + bgfx + optional Disabled AudioEngine (M11-A15). When
// TINA_BUILD_UI_FREETYPE is ON, injects FreeType UI rasterizer and opens the
// sample/source font fixture path if provided at compile time
// (TINA_DESKTOP_UI_FONT_PATH). That is a development fixture, not a Runtime
// product font loader. miniaudio device remains adapter/sample private.
[[nodiscard]] Core::Result<std::unique_ptr<EngineHost>> CreateEngine(const EngineConfig& config) noexcept;

// Same production composition with optional hooks (device wrap). Prefer the
// single-arg overload for ordinary games; use options for product-sample
// capture / diagnostics only.
[[nodiscard]] Core::Result<std::unique_ptr<EngineHost>> CreateEngine(const EngineConfig& config,
                                                                    CreateEngineOptions options) noexcept;

} // namespace Tina::Desktop
