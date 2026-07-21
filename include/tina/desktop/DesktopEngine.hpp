#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/runtime/EngineHost.hpp>

#include <memory>

namespace Tina::Desktop {

// Production Desktop composition: SteadyClock + GLFW WindowSurface + bounded
// TaskSystem + bgfx + optional Disabled AudioEngine (M11-A15). When
// TINA_BUILD_UI_FREETYPE is ON, injects FreeType UI rasterizer and opens the
// sample/source font fixture path if provided at compile time
// (TINA_DESKTOP_UI_FONT_PATH). That is a development fixture, not a Runtime
// product font loader. miniaudio device remains adapter/sample private.
[[nodiscard]] Core::Result<std::unique_ptr<EngineHost>> CreateEngine(const EngineConfig& config) noexcept;

} // namespace Tina::Desktop
