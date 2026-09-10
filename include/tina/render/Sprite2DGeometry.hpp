#pragma once

#include <tina/render/IsometricProjection2D.hpp>

#include <cmath>
#include <limits>
#include <optional>

namespace Tina::Render {

// The single render-facing quad representation. Its corners are center +/- the
// two half axes; the axes need not be orthogonal (isometric ground and trails).
struct Sprite2DQuad final {
    float centerX = 0.0F;
    float centerY = 0.0F;
    float halfAxisXX = 0.5F;
    float halfAxisXY = 0.0F;
    float halfAxisYX = 0.0F;
    float halfAxisYY = 0.5F;

    [[nodiscard]] bool isValid() const noexcept
    {
        if (!std::isfinite(centerX) || !std::isfinite(centerY)
            || !std::isfinite(halfAxisXX) || !std::isfinite(halfAxisXY)
            || !std::isfinite(halfAxisYX) || !std::isfinite(halfAxisYY)) {
            return false;
        }
        const double determinant = static_cast<double>(halfAxisXX) * halfAxisYY
            - static_cast<double>(halfAxisXY) * halfAxisYX;
        const double extentX = std::abs(static_cast<double>(halfAxisXX)) + std::abs(static_cast<double>(halfAxisYX));
        const double extentY = std::abs(static_cast<double>(halfAxisXY)) + std::abs(static_cast<double>(halfAxisYY));
        constexpr double Maximum = (std::numeric_limits<float>::max)();
        return determinant != 0.0 && std::abs(static_cast<double>(centerX)) + extentX <= Maximum
            && std::abs(static_cast<double>(centerY)) + extentY <= Maximum;
    }

    [[nodiscard]] bool contains(float x, float y) const noexcept
    {
        if (!isValid() || !std::isfinite(x) || !std::isfinite(y)) return false;
        const double dx = static_cast<double>(x) - centerX;
        const double dy = static_cast<double>(y) - centerY;
        const double determinant = static_cast<double>(halfAxisXX) * halfAxisYY
            - static_cast<double>(halfAxisXY) * halfAxisYX;
        const double localX = (dx * halfAxisYY - dy * halfAxisYX) / determinant;
        const double localY = (dy * halfAxisXX - dx * halfAxisXY) / determinant;
        return std::abs(localX) <= 1.0 && std::abs(localY) <= 1.0;
    }
};

// Authoring/extraction input only. Position denotes the pivot, not the center.
// Z/elevation is a grid height; width/height and pivot describe the source quad.
struct Sprite2DTransform final {
    float positionX = 0.0F;
    float positionY = 0.0F;
    float elevation = 0.0F;
    float rotationRadians = 0.0F;
    float widthMeters = 1.0F;
    float heightMeters = 1.0F;
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    float pivotX = 0.5F;
    float pivotY = 0.5F;
};

[[nodiscard]] inline Sprite2DQuad makeSprite2DQuad(const Sprite2DTransform& transform = {}) noexcept
{
    const float cosine = std::cos(transform.rotationRadians);
    const float sine = std::sin(transform.rotationRadians);
    const double halfWidth = static_cast<double>(transform.widthMeters) * transform.scaleX * 0.5;
    const double halfHeight = static_cast<double>(transform.heightMeters) * transform.scaleY * 0.5;
    Sprite2DQuad quad{
        .centerX = transform.positionX,
        .centerY = transform.positionY,
        .halfAxisXX = static_cast<float>(cosine * halfWidth),
        .halfAxisXY = static_cast<float>(sine * halfWidth),
        .halfAxisYX = static_cast<float>(-sine * halfHeight),
        .halfAxisYY = static_cast<float>(cosine * halfHeight),
    };
    const double offsetX = 1.0 - 2.0 * transform.pivotX;
    const double offsetY = 1.0 - 2.0 * transform.pivotY;
    quad.centerX = static_cast<float>(transform.positionX + offsetX * quad.halfAxisXX + offsetY * quad.halfAxisYX);
    quad.centerY = static_cast<float>(transform.positionY + offsetX * quad.halfAxisXY + offsetY * quad.halfAxisYY);
    return quad;
}

// Shared value-only extraction context. Construct from the frame's resolved
// camera; never retain a writer, World, asset resolver, or frame resource here.
struct Sprite2DProjection final {
    std::optional<IsometricProjection2D> isometric{};

    [[nodiscard]] bool isValid() const noexcept { return !isometric || isometric->isValid(); }

    [[nodiscard]] constexpr IsometricWorldPoint2D projectPoint(IsometricGridPoint2D point) const noexcept
    {
        return isometric ? isometric->project(point) : IsometricWorldPoint2D{point.x, point.y};
    }

    [[nodiscard]] constexpr double sortDepth(IsometricGridPoint2D point) const noexcept
    {
        return isometric ? isometric->sortDepth(point) : 0.0;
    }

    // Upright artwork keeps its axes in the render plane. The rotated pivot
    // offset is applied AFTER the anchor is projected, exactly once.
    [[nodiscard]] Sprite2DQuad billboard(Sprite2DTransform transform) const noexcept
    {
        const auto position = projectPoint({transform.positionX, transform.positionY, transform.elevation});
        transform.positionX = position.x;
        transform.positionY = position.y;
        return makeSprite2DQuad(transform);
    }

    // Ground artwork projects the complete affine quad, not just its center.
    // This preserves diamond corners, UVs, negative scales and trail endpoints.
    [[nodiscard]] constexpr Sprite2DQuad ground(Sprite2DQuad quad, float elevation = 0.0F) const noexcept
    {
        if (!isometric) {
            return quad;
        }
        const auto center = isometric->project({quad.centerX, quad.centerY, elevation});
        const auto axisX = isometric->project({quad.halfAxisXX, quad.halfAxisXY, 0.0F});
        const auto axisY = isometric->project({quad.halfAxisYX, quad.halfAxisYY, 0.0F});
        return {center.x, center.y, axisX.x, axisX.y, axisY.x, axisY.y};
    }
};

} // namespace Tina::Render
