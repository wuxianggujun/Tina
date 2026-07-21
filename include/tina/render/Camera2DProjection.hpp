#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderScene.hpp>

#include <variant>

namespace Tina::Render {

// Authored projection modes (game-2d contract). Scene/sample resolve these with
// the current surface framebuffer viewport into RenderCamera2DInput fields.
// Config reference values and derived actualPixelsPerMeter must not be mixed.

struct FixedWorldHeight2D final {
    float heightMeters = 18.0F;
};

struct PixelPerfect2D final {
    float referencePixelsPerMeter = 16.0F;
    u32 referenceHeightPixels = 288;
};

using Camera2DProjectionMode = std::variant<FixedWorldHeight2D, PixelPerfect2D>;

// Framebuffer pixel size of the orthographic view (already multiplied by
// normalized viewport). Zero extent is Suspended surface, not a Camera error.
struct Camera2DSurfaceViewport final {
    u32 pixelWidth = 0;
    u32 pixelHeight = 0;
};

struct Camera2DProjectionQuery final {
    u64 stableCameraKey = 0;
    float centerX = 0.0F;
    float centerY = 0.0F;
    float rotationRadians = 0.0F;
    Camera2DProjectionMode projection = FixedWorldHeight2D{};
    RenderNormalizedViewport normalizedViewport{};
    RenderPixelSnapPolicy pixelSnap = RenderPixelSnapPolicy::Disabled;
    Camera2DSurfaceViewport surfaceViewport{};
};

struct Camera2DProjectionResult final {
    float worldWidth = 1.0F;
    float worldHeight = 1.0F;
    float actualPixelsPerMeter = 1.0F;
    RenderPixelSnapPolicy pixelSnap = RenderPixelSnapPolicy::Disabled;
    u32 integerScale = 1; // PixelPerfect only; FixedWorldHeight leaves 1
};

// Derive worldWidth/Height + actualPPM from authored projection + surface
// framebuffer viewport. Does not apply pixel snap (RenderSceneBuilder does).
// PixelPerfect forces CameraAndSprites; other pixelSnap values fail.
// Suspended (0×0) surface returns structured error — caller skips extract.
[[nodiscard]] Core::Result<Camera2DProjectionResult>
resolveCamera2DProjection(const Camera2DProjectionQuery& query) noexcept;

// Convenience: fill a complete RenderCamera2DInput from query + resolve.
[[nodiscard]] Core::Result<RenderCamera2DInput>
makeResolvedCamera2DInput(const Camera2DProjectionQuery& query) noexcept;

} // namespace Tina::Render
