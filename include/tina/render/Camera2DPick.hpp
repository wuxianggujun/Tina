#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/render/WorldPointerSample.hpp>

namespace Tina::Render {

// logicalWidth/Height are primary-window logical pixels for the same frame as
// the pointer sample (Platform WindowMetricsSnapshot), not framebuffer pixels.
struct Camera2DPickQuery final {
    double logicalX = 0.0;
    double logicalY = 0.0;
    u32 logicalWidth = 0;
    u32 logicalHeight = 0;
    RenderCamera2D camera{};
    u64 cameraRevision = 0;
    u64 surfaceRevision = 0;
    u64 inputSequence = 0;
};

// Project one primary-window logical pointer sample into world meters using the
// same orthographic Camera2D basis as the Sprite2D backend (Y-up world, top-left
// logical UI). Outside the camera normalized viewport returns hit=false without
// inventing a world point. Invalid camera/extent/coords return structured error.
[[nodiscard]] Core::Result<WorldPointerSample> pickWorldFromLogicalPointer(
    const Camera2DPickQuery& query) noexcept;

} // namespace Tina::Render
