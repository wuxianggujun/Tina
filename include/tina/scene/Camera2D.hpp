#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/render/Camera2DProjection.hpp>
#include <tina/render/RenderScene.hpp>

#include <cmath>
#include <variant>

namespace Tina::Scene {

// Scene-owned orthographic Camera component. Pose comes from WorldTransform on
// the same entity; projection is authored here and resolved at extract time
// with the current surface framebuffer viewport (Render::Camera2DProjection).
struct Camera2D final {
    Render::Camera2DProjectionMode projection = Render::FixedWorldHeight2D{};
    Render::RenderNormalizedViewport normalizedViewport{};
    Render::RenderPixelSnapPolicy pixelSnap = Render::RenderPixelSnapPolicy::Disabled;
    bool active = true;

    friend constexpr bool operator==(const Camera2D&, const Camera2D&) noexcept = default;
};

[[nodiscard]] inline bool isFiniteViewport(const Render::RenderNormalizedViewport& viewport) noexcept
{
    return std::isfinite(viewport.x) && std::isfinite(viewport.y)
        && std::isfinite(viewport.width) && std::isfinite(viewport.height);
}

// Validates authored Camera2D fields only. Surface 0×0 suspension is not a
// component configuration error and is checked at extract with the surface
// viewport argument.
[[nodiscard]] inline bool isValid(const Camera2D& camera) noexcept
{
    if (!isFiniteViewport(camera.normalizedViewport)) {
        return false;
    }
    if (camera.normalizedViewport.x < 0.0F || camera.normalizedViewport.y < 0.0F
        || camera.normalizedViewport.width <= 0.0F || camera.normalizedViewport.height <= 0.0F) {
        return false;
    }
    if (camera.normalizedViewport.x + camera.normalizedViewport.width > 1.0F + 1.0e-6F
        || camera.normalizedViewport.y + camera.normalizedViewport.height > 1.0F + 1.0e-6F) {
        return false;
    }

    if (const auto* fixed = std::get_if<Render::FixedWorldHeight2D>(&camera.projection)) {
        return std::isfinite(fixed->heightMeters) && fixed->heightMeters > 0.0F;
    }
    if (const auto* pixel = std::get_if<Render::PixelPerfect2D>(&camera.projection)) {
        if (!std::isfinite(pixel->referencePixelsPerMeter)
            || pixel->referencePixelsPerMeter <= 0.0F
            || pixel->referenceHeightPixels == 0) {
            return false;
        }
        // PixelPerfect forces CameraAndSprites at resolve; other policies are illegal.
        return camera.pixelSnap == Render::RenderPixelSnapPolicy::CameraAndSprites;
    }
    return false;
}

} // namespace Tina::Scene
