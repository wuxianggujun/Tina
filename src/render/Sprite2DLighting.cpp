#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderScene.hpp>

#include "Sprite2DShadowMath.hpp"

#include <algorithm>
#include <cmath>

namespace Tina::Render {

Core::Status validateSprite2DLightingDesc(const Sprite2DLightingDesc& lighting) noexcept
{
    if (lighting.pointLights.size() > Sprite2DLightingDesc::MaximumPointLightCount)
    {
        return Core::failure(RenderErrorCode::InvalidSprite2DLighting,
                             "Sprite2D point light count exceeds the fixed frame limit");
    }
    if (lighting.shadowSegments.size() > Sprite2DLightingDesc::MaximumShadowSegmentCount)
    {
        return Core::failure(RenderErrorCode::InvalidSprite2DLighting,
                             "Sprite2D shadow segment count exceeds the fixed frame limit");
    }
    if (!std::isfinite(lighting.ambientScale) || lighting.ambientScale < 0.0F)
    {
        return Core::failure(RenderErrorCode::InvalidSprite2DLighting,
                             "Sprite2D ambient scale must be finite and non-negative");
    }

    for (const Sprite2DPointLight& light : lighting.pointLights)
    {
        if (!std::isfinite(light.positionX) || !std::isfinite(light.positionY) ||
            !std::isfinite(light.radiusMeters) || light.radiusMeters <= 0.0F ||
            !std::isfinite(light.sourceRadiusMeters) || light.sourceRadiusMeters < 0.0F ||
            light.sourceRadiusMeters > light.radiusMeters ||
            !std::isfinite(light.colorR) || !std::isfinite(light.colorG) ||
            !std::isfinite(light.colorB) || light.colorR < 0.0F || light.colorG < 0.0F ||
            light.colorB < 0.0F)
        {
            return Core::failure(
                RenderErrorCode::InvalidSprite2DLighting,
                "Sprite2D point light position/radii must be finite with a positive influence radius, a bounded non-negative source radius, and non-negative RGB");
        }
    }

    for (const Sprite2DShadowSegment& segment : lighting.shadowSegments)
    {
        if (!std::isfinite(segment.startX) || !std::isfinite(segment.startY) ||
            !std::isfinite(segment.endX) || !std::isfinite(segment.endY) ||
            (segment.startX == segment.endX && segment.startY == segment.endY))
        {
            return Core::failure(
                RenderErrorCode::InvalidSprite2DLighting,
                "Sprite2D shadow segment endpoints must be finite and non-degenerate");
        }
    }

    return Core::success();
}

namespace {

constexpr float ShadowDepthEpsilon = 0.001F;
constexpr float ShadowParallelEpsilon = 0.00001F;

[[nodiscard]] constexpr float cross2D(float leftX, float leftY,
                                      float rightX, float rightY) noexcept
{
    return leftX * rightY - leftY * rightX;
}

[[nodiscard]] bool hardShadowSegmentBlocksLight(
    float fragmentX,
    float fragmentY,
    const Sprite2DPointLight& light,
    const Sprite2DShadowSegment& segment) noexcept
{
    const float lightRayX = light.positionX - fragmentX;
    const float lightRayY = light.positionY - fragmentY;
    const float segmentDirectionX = segment.endX - segment.startX;
    const float segmentDirectionY = segment.endY - segment.startY;
    const float denominator =
        cross2D(lightRayX, lightRayY, segmentDirectionX, segmentDirectionY);
    if (std::abs(denominator) < ShadowParallelEpsilon) {
        return false;
    }

    const float offsetX = segment.startX - fragmentX;
    const float offsetY = segment.startY - fragmentY;
    const float lightRayFactor =
        cross2D(offsetX, offsetY, segmentDirectionX, segmentDirectionY) / denominator;
    const float segmentFactor = cross2D(offsetX, offsetY, lightRayX, lightRayY) / denominator;
    return lightRayFactor > ShadowDepthEpsilon &&
           lightRayFactor < 1.0F - ShadowDepthEpsilon &&
           segmentFactor >= 0.0F && segmentFactor <= 1.0F;
}

} // namespace

float Detail::sprite2DShadowSegmentVisibility(
    float fragmentX,
    float fragmentY,
    const Sprite2DPointLight& light,
    const Sprite2DShadowSegment& segment) noexcept
{
    if (light.sourceRadiusMeters <= 0.0F) {
        return hardShadowSegmentBlocksLight(fragmentX, fragmentY, light, segment) ? 0.0F : 1.0F;
    }

    const float coordinateMagnitude = std::max({
        std::abs(fragmentX),
        std::abs(fragmentY),
        std::abs(light.positionX),
        std::abs(light.positionY),
        std::abs(segment.startX),
        std::abs(segment.startY),
        std::abs(segment.endX),
        std::abs(segment.endY),
        light.sourceRadiusMeters,
    });
    const float coordinateScale = std::max(1.0F, coordinateMagnitude * 0.25F);
    const float inverseCoordinateScale = 1.0F / coordinateScale;
    const float scaledFragmentX = fragmentX * inverseCoordinateScale;
    const float scaledFragmentY = fragmentY * inverseCoordinateScale;
    const float scaledLightX = light.positionX * inverseCoordinateScale;
    const float scaledLightY = light.positionY * inverseCoordinateScale;
    const float scaledSourceRadius = light.sourceRadiusMeters * inverseCoordinateScale;
    if (scaledSourceRadius <= 0.0F) {
        return hardShadowSegmentBlocksLight(fragmentX, fragmentY, light, segment) ? 0.0F : 1.0F;
    }

    const float toLightX = scaledLightX - scaledFragmentX;
    const float toLightY = scaledLightY - scaledFragmentY;
    const float lightDistance = std::hypot(toLightX, toLightY);
    if (!std::isfinite(lightDistance) ||
        lightDistance <=
            scaledSourceRadius + ShadowDepthEpsilon * inverseCoordinateScale) {
        return 1.0F;
    }

    const float axisX = toLightX / lightDistance;
    const float axisY = toLightY / lightDistance;
    const float startRelativeX =
        segment.startX * inverseCoordinateScale - scaledFragmentX;
    const float startRelativeY =
        segment.startY * inverseCoordinateScale - scaledFragmentY;
    const float endRelativeX = segment.endX * inverseCoordinateScale - scaledFragmentX;
    const float endRelativeY = segment.endY * inverseCoordinateScale - scaledFragmentY;
    const float startAxial = startRelativeX * axisX + startRelativeY * axisY;
    const float endAxial = endRelativeX * axisX + endRelativeY * axisY;
    const float minimumAxial = ShadowDepthEpsilon * lightDistance;
    const float maximumAxial = (1.0F - ShadowDepthEpsilon) * lightDistance;
    if (minimumAxial <= 0.0F || maximumAxial <= minimumAxial) {
        return 1.0F;
    }

    float clippedStart = 0.0F;
    float clippedEnd = 1.0F;
    const float axialDelta = endAxial - startAxial;
    if (std::abs(axialDelta) < ShadowParallelEpsilon * lightDistance) {
        if (startAxial <= minimumAxial || startAxial >= maximumAxial) {
            return 1.0F;
        }
    } else {
        const float atMinimumAxial = (minimumAxial - startAxial) / axialDelta;
        const float atMaximumAxial = (maximumAxial - startAxial) / axialDelta;
        clippedStart = std::max(clippedStart, std::min(atMinimumAxial, atMaximumAxial));
        clippedEnd = std::min(clippedEnd, std::max(atMinimumAxial, atMaximumAxial));
        if (clippedStart > clippedEnd) {
            return 1.0F;
        }
    }

    const float segmentX = endRelativeX - startRelativeX;
    const float segmentY = endRelativeY - startRelativeY;
    const float clippedStartX = startRelativeX + segmentX * clippedStart;
    const float clippedStartY = startRelativeY + segmentY * clippedStart;
    const float clippedEndX = startRelativeX + segmentX * clippedEnd;
    const float clippedEndY = startRelativeY + segmentY * clippedEnd;
    const float clippedStartAxial = std::clamp(
        clippedStartX * axisX + clippedStartY * axisY,
        minimumAxial,
        maximumAxial);
    const float clippedEndAxial = std::clamp(
        clippedEndX * axisX + clippedEndY * axisY,
        minimumAxial,
        maximumAxial);
    const float startProjection =
        cross2D(axisX, axisY, clippedStartX, clippedStartY) *
        (lightDistance / clippedStartAxial);
    const float endProjection =
        cross2D(axisX, axisY, clippedEndX, clippedEndY) *
        (lightDistance / clippedEndAxial);

    const float blockedMinimum = std::min(startProjection, endProjection);
    const float blockedMaximum = std::max(startProjection, endProjection);
    const float overlap = std::max(
        std::min(blockedMaximum, scaledSourceRadius) -
            std::max(blockedMinimum, -scaledSourceRadius),
        0.0F);
    const float blockedFraction = (overlap / scaledSourceRadius) * 0.5F;
    return std::clamp(1.0F - blockedFraction, 0.0F, 1.0F);
}

} // namespace Tina::Render
