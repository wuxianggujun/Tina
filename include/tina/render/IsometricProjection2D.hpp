#pragma once

#include <tina/core/base/Types.hpp>

#include <cmath>

namespace Tina::Render {

// A grid point is kept separate from a rendered world point. Gameplay systems can
// continue to reason in integer-like grid coordinates while RenderScene receives
// the projected orthographic position.
struct IsometricGridPoint2D final {
    float x = 0.0F;
    float y = 0.0F;
    float elevation = 0.0F;
};

struct IsometricWorldPoint2D final {
    float x = 0.0F;
    float y = 0.0F;
};

// Default 2:1 diamond projection used by Camera2D. tileWidthMeters/tileHeightMeters
// describe the distance between the left/right and top/bottom corners of one tile;
// they are not the dimensions of the source PNG quad. elevation moves an object up
// the screen without changing its grid cell.
struct IsometricProjection2D final {
    float tileWidthMeters = 1.5F;
    float tileHeightMeters = 0.75F;
    float elevationStepMeters = 0.25F;
    float viewHeightMeters = 11.25F;

    friend constexpr bool operator==(const IsometricProjection2D&, const IsometricProjection2D&) noexcept = default;

    [[nodiscard]] bool isValid() const noexcept
    {
        return std::isfinite(tileWidthMeters) && tileWidthMeters > 0.0F
            && std::isfinite(tileHeightMeters) && tileHeightMeters > 0.0F
            && std::isfinite(elevationStepMeters) && elevationStepMeters >= 0.0F
            && std::isfinite(viewHeightMeters) && viewHeightMeters > 0.0F;
    }

    [[nodiscard]] constexpr IsometricWorldPoint2D project(
        IsometricGridPoint2D point) const noexcept
    {
        return IsometricWorldPoint2D{
            .x = static_cast<float>((static_cast<double>(point.x) - point.y) * tileWidthMeters * 0.5),
            .y = static_cast<float>((static_cast<double>(point.x) + point.y) * tileHeightMeters * 0.5
                + static_cast<double>(point.elevation) * elevationStepMeters),
        };
    }

    [[nodiscard]] constexpr IsometricGridPoint2D unproject(
        IsometricWorldPoint2D point, float elevation = 0.0F) const noexcept
    {
        const double halfWidth = static_cast<double>(tileWidthMeters) * 0.5;
        const double halfHeight = static_cast<double>(tileHeightMeters) * 0.5;
        const double elevatedGridY =
            (static_cast<double>(point.y) - static_cast<double>(elevation) * elevationStepMeters) / halfHeight;
        const double gridX = static_cast<double>(point.x) / halfWidth;
        return IsometricGridPoint2D{
            .x = static_cast<float>((gridX + elevatedGridY) * 0.5),
            .y = static_cast<float>((elevatedGridY - gridX) * 0.5),
            .elevation = elevation,
        };
    }

    // Painter depth is independent of authored order. Increasing ground Y is
    // farther away; increasing elevation is nearer, not another ground-Y offset.
    // Keep fractional positions in double: quantizing/clamping an i32 key lets
    // authored order or saturation silently reverse spatial occlusion.
    [[nodiscard]] constexpr double sortDepth(IsometricGridPoint2D point) const noexcept
    {
        return -(static_cast<double>(point.x) + point.y) * tileHeightMeters * 0.5
            + static_cast<double>(point.elevation) * elevationStepMeters;
    }
};

} // namespace Tina::Render
