#include <tina/render/Camera2DPick.hpp>

#include <tina/render/RenderErrors.hpp>

#include <cmath>
#include <limits>

namespace Tina::Render {
namespace {

[[nodiscard]] bool finite(float value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] bool finite(double value) noexcept
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

[[nodiscard]] bool isValidCamera(const RenderCamera2D& camera) noexcept
{
    return camera.stableCameraKey != 0 && finite(camera.centerX) && finite(camera.centerY) &&
           finite(camera.rotationRadians) && finite(camera.worldWidth) && finite(camera.worldHeight) &&
           finite(camera.actualPixelsPerMeter) && camera.worldWidth > 0.0F && camera.worldHeight > 0.0F &&
           camera.actualPixelsPerMeter > 0.0F && finiteViewport(camera.normalizedViewport);
}

[[nodiscard]] bool isRepresentableLogical(double value) noexcept
{
    constexpr double Maximum = static_cast<double>((std::numeric_limits<float>::max)());
    return finite(value) && value >= -Maximum && value <= Maximum;
}

} // namespace

Core::Result<WorldPointerSample> pickWorldFromLogicalPointer(const Camera2DPickQuery& query) noexcept
{
    if (!isRepresentableLogical(query.logicalX) || !isRepresentableLogical(query.logicalY))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "Camera2D pick logical coordinates must be finite and float-representable");
    }
    if (query.logicalWidth == 0 || query.logicalHeight == 0)
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "Camera2D pick requires a non-zero logical window extent");
    }
    if (!isValidCamera(query.camera))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "Camera2D pick received invalid projection or viewport values");
    }

    const double logicalWidth = static_cast<double>(query.logicalWidth);
    const double logicalHeight = static_cast<double>(query.logicalHeight);
    const double normalizedX = query.logicalX / logicalWidth;
    const double normalizedY = query.logicalY / logicalHeight;
    if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "Camera2D pick normalized coordinates are not finite");
    }

    const auto& viewport = query.camera.normalizedViewport;
    const double viewportLeft = static_cast<double>(viewport.x);
    const double viewportTop = static_cast<double>(viewport.y);
    const double viewportRight = viewportLeft + static_cast<double>(viewport.width);
    const double viewportBottom = viewportTop + static_cast<double>(viewport.height);
    // Half-open viewport bounds: left/top inclusive, right/bottom exclusive.
    if (normalizedX < viewportLeft || normalizedY < viewportTop || normalizedX >= viewportRight ||
        normalizedY >= viewportBottom)
    {
        return WorldPointerSample{
            .cameraRevision = query.cameraRevision,
            .surfaceRevision = query.surfaceRevision,
            .inputSequence = query.inputSequence,
            .stableCameraKey = query.camera.stableCameraKey,
            .hit = false,
        };
    }

    const double localU = (normalizedX - viewportLeft) / static_cast<double>(viewport.width);
    const double localV = (normalizedY - viewportTop) / static_cast<double>(viewport.height);
    // Logical Y grows downward; world Y grows upward. Map local viewport UV into
    // camera-local orthographic space matching Sprite2D mtxOrtho half extents.
    const double cameraLocalX = (localU - 0.5) * static_cast<double>(query.camera.worldWidth);
    const double cameraLocalY = (0.5 - localV) * static_cast<double>(query.camera.worldHeight);

    const double cosine = std::cos(static_cast<double>(query.camera.rotationRadians));
    const double sine = std::sin(static_cast<double>(query.camera.rotationRadians));
    // Inverse of the Sprite2D view rotation that maps world -> camera local.
    const double worldX =
        static_cast<double>(query.camera.centerX) + cosine * cameraLocalX - sine * cameraLocalY;
    const double worldY =
        static_cast<double>(query.camera.centerY) + sine * cameraLocalX + cosine * cameraLocalY;
    if (!std::isfinite(worldX) || !std::isfinite(worldY))
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "Camera2D pick produced a non-finite world point");
    }

    return WorldPointerSample{
        .worldX = static_cast<float>(worldX),
        .worldY = static_cast<float>(worldY),
        .cameraRevision = query.cameraRevision,
        .surfaceRevision = query.surfaceRevision,
        .inputSequence = query.inputSequence,
        .stableCameraKey = query.camera.stableCameraKey,
        .hit = true,
    };
}

} // namespace Tina::Render
