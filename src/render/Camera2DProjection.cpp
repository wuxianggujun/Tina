#include <tina/render/Camera2DProjection.hpp>

#include <tina/render/RenderErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Tina::Render {
namespace {

[[nodiscard]] bool finite(float value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] bool finiteViewport(const RenderNormalizedViewport& viewport) noexcept
{
    const double right = static_cast<double>(viewport.x) + static_cast<double>(viewport.width);
    const double bottom = static_cast<double>(viewport.y) + static_cast<double>(viewport.height);
    return finite(viewport.x) && finite(viewport.y) && finite(viewport.width) && finite(viewport.height) &&
           viewport.x >= 0.0F && viewport.y >= 0.0F && viewport.width > 0.0F && viewport.height > 0.0F &&
           std::isfinite(right) && std::isfinite(bottom) && right <= 1.0 && bottom <= 1.0;
}

[[nodiscard]] bool validPixelSnapPolicy(RenderPixelSnapPolicy policy) noexcept
{
    return policy == RenderPixelSnapPolicy::Disabled || policy == RenderPixelSnapPolicy::CameraTranslation ||
           policy == RenderPixelSnapPolicy::CameraAndSprites;
}

[[nodiscard]] Core::Result<Camera2DSurfaceViewport>
viewportPixels(const Camera2DSurfaceViewport& surface, const RenderNormalizedViewport& viewport) noexcept
{
    if (surface.pixelWidth == 0 || surface.pixelHeight == 0)
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "Camera2D projection requires a non-zero framebuffer viewport "
                             "(zero extent is Suspended surface, not a Camera config error)");
    }
    if (!finiteViewport(viewport))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "Camera2D projection received an invalid normalized viewport");
    }

    const double width =
        static_cast<double>(surface.pixelWidth) * static_cast<double>(viewport.width);
    const double height =
        static_cast<double>(surface.pixelHeight) * static_cast<double>(viewport.height);
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1.0 || height < 1.0)
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "Camera2D projection viewport pixel size is not usable");
    }
    if (width > static_cast<double>((std::numeric_limits<u32>::max)()) ||
        height > static_cast<double>((std::numeric_limits<u32>::max)()))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "Camera2D projection viewport pixel size overflows u32");
    }
    return Camera2DSurfaceViewport{
        .pixelWidth = static_cast<u32>(width),
        .pixelHeight = static_cast<u32>(height),
    };
}

} // namespace

Core::Result<Camera2DProjectionResult> resolveCamera2DProjection(const Camera2DProjectionQuery& query) noexcept
{
    if (query.stableCameraKey == 0 || !finite(query.centerX) || !finite(query.centerY) ||
        !finite(query.rotationRadians) || !validPixelSnapPolicy(query.pixelSnap))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "Camera2D projection query contains invalid camera identity or pose");
    }

    auto pixels = viewportPixels(query.surfaceViewport, query.normalizedViewport);
    if (!pixels)
    {
        return Core::failure(std::move(pixels.error()));
    }

    const double viewportW = static_cast<double>(pixels->pixelWidth);
    const double viewportH = static_cast<double>(pixels->pixelHeight);
    const double aspect = viewportW / viewportH;

    if (const auto* fixed = std::get_if<FixedWorldHeight2D>(&query.projection); fixed != nullptr)
    {
        if (!finite(fixed->heightMeters) || fixed->heightMeters <= 0.0F)
        {
            return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                                 "FixedWorldHeight2D heightMeters must be finite and greater than zero");
        }
        const double worldHeight = static_cast<double>(fixed->heightMeters);
        const double worldWidth = worldHeight * aspect;
        const double actualPpm = viewportH / worldHeight;
        if (!std::isfinite(worldWidth) || !std::isfinite(actualPpm) || worldWidth <= 0.0 || actualPpm <= 0.0)
        {
            return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                                 "FixedWorldHeight2D produced non-finite projection values");
        }
        return Camera2DProjectionResult{
            .worldWidth = static_cast<float>(worldWidth),
            .worldHeight = static_cast<float>(worldHeight),
            .actualPixelsPerMeter = static_cast<float>(actualPpm),
            .pixelSnap = query.pixelSnap,
            .integerScale = 1,
        };
    }

    const auto* pixelPerfect = std::get_if<PixelPerfect2D>(&query.projection);
    if (pixelPerfect == nullptr)
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "Camera2D projection mode is unrecognized");
    }
    if (!finite(pixelPerfect->referencePixelsPerMeter) || pixelPerfect->referencePixelsPerMeter <= 0.0F ||
        pixelPerfect->referenceHeightPixels == 0)
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "PixelPerfect2D reference values must be finite and greater than zero");
    }
    // PixelPerfect forces CameraAndSprites; other policies are illegal configs.
    if (query.pixelSnap != RenderPixelSnapPolicy::CameraAndSprites)
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "PixelPerfect2D requires PixelSnapPolicy::CameraAndSprites");
    }

    const double scaleRatio = viewportH / static_cast<double>(pixelPerfect->referenceHeightPixels);
    const u32 integerScale = (std::max)(1U, static_cast<u32>(std::floor(scaleRatio)));
    const double actualPpm =
        static_cast<double>(pixelPerfect->referencePixelsPerMeter) * static_cast<double>(integerScale);
    const double worldHeight = viewportH / actualPpm;
    const double worldWidth = worldHeight * aspect;
    if (!std::isfinite(actualPpm) || !std::isfinite(worldHeight) || !std::isfinite(worldWidth) || actualPpm <= 0.0 ||
        worldHeight <= 0.0 || worldWidth <= 0.0)
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "PixelPerfect2D produced non-finite projection values");
    }
    return Camera2DProjectionResult{
        .worldWidth = static_cast<float>(worldWidth),
        .worldHeight = static_cast<float>(worldHeight),
        .actualPixelsPerMeter = static_cast<float>(actualPpm),
        .pixelSnap = RenderPixelSnapPolicy::CameraAndSprites,
        .integerScale = integerScale,
    };
}

Core::Result<RenderCamera2DInput> makeResolvedCamera2DInput(const Camera2DProjectionQuery& query) noexcept
{
    auto resolved = resolveCamera2DProjection(query);
    if (!resolved)
    {
        return Core::failure(std::move(resolved.error()));
    }
    return RenderCamera2DInput{
        .stableCameraKey = query.stableCameraKey,
        .centerX = query.centerX,
        .centerY = query.centerY,
        .rotationRadians = query.rotationRadians,
        .worldWidth = resolved->worldWidth,
        .worldHeight = resolved->worldHeight,
        .actualPixelsPerMeter = resolved->actualPixelsPerMeter,
        .normalizedViewport = query.normalizedViewport,
        .pixelSnap = resolved->pixelSnap,
    };
}

} // namespace Tina::Render
