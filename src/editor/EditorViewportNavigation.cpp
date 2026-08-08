#include <tina/editor/EditorViewportNavigation.hpp>

#include <tina/editor/EditorErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace Tina::Editor {
namespace {

[[nodiscard]] bool finite(float value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] bool finite(EditorViewportVector2 value) noexcept
{
    return finite(value.x) && finite(value.y);
}

[[nodiscard]] bool finite(EditorViewportVector3 value) noexcept
{
    return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] Core::Status
validateConfig(const EditorViewportNavigationConfig& config)
{
    const bool validTwoD = finite(config.twoDWorldUnitsPerPixelAtZoomOne) &&
                           config.twoDWorldUnitsPerPixelAtZoomOne > 0.0F &&
                           finite(config.minimumTwoDZoom) && config.minimumTwoDZoom > 0.0F &&
                           finite(config.maximumTwoDZoom) &&
                           config.maximumTwoDZoom >= config.minimumTwoDZoom &&
                           finite(config.twoDZoomStepFactor) &&
                           config.twoDZoomStepFactor > 1.0F;
    const bool validThreeD = finite(config.threeDOrbitRadiansPerPixel) &&
                             config.threeDOrbitRadiansPerPixel > 0.0F &&
                             finite(config.threeDPanWorldUnitsPerPixelAtUnitDistance) &&
                             config.threeDPanWorldUnitsPerPixelAtUnitDistance > 0.0F &&
                             finite(config.minimumThreeDDistance) &&
                             config.minimumThreeDDistance > 0.0F &&
                             finite(config.maximumThreeDDistance) &&
                             config.maximumThreeDDistance >= config.minimumThreeDDistance &&
                             finite(config.threeDDollyStepFactor) &&
                             config.threeDDollyStepFactor > 1.0F &&
                             finite(config.minimumThreeDPitchRadians) &&
                             finite(config.maximumThreeDPitchRadians) &&
                             config.minimumThreeDPitchRadians <
                                 config.maximumThreeDPitchRadians &&
                             config.minimumThreeDPitchRadians >
                                 -std::numbers::pi_v<float> * 0.5F &&
                             config.maximumThreeDPitchRadians <
                                 std::numbers::pi_v<float> * 0.5F;
    if (!validTwoD || !validThreeD)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor viewport navigation configuration is invalid");
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateState(
    const EditorViewportNavigationConfig& config,
    const EditorViewport2DNavigationState& twoD,
    const EditorViewport3DNavigationState& threeD)
{
    const bool validTwoD = finite(twoD.center) && finite(twoD.zoom) &&
                           twoD.zoom >= config.minimumTwoDZoom &&
                           twoD.zoom <= config.maximumTwoDZoom;
    const bool validThreeD = finite(threeD.target) && finite(threeD.yawRadians) &&
                             finite(threeD.pitchRadians) &&
                             threeD.pitchRadians >= config.minimumThreeDPitchRadians &&
                             threeD.pitchRadians <= config.maximumThreeDPitchRadians &&
                             finite(threeD.distance) &&
                             threeD.distance >= config.minimumThreeDDistance &&
                             threeD.distance <= config.maximumThreeDDistance;
    if (!validTwoD || !validThreeD)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor viewport navigation initial state is invalid");
    }
    return Core::success();
}

[[nodiscard]] float clampScaled(float value,
                                float scale,
                                float minimum,
                                float maximum,
                                bool multiply) noexcept
{
    if (std::isinf(scale))
    {
        return multiply ? maximum : minimum;
    }
    if (scale == 0.0F)
    {
        return multiply ? minimum : maximum;
    }
    const float scaled = multiply ? value * scale : value / scale;
    if (!finite(scaled))
    {
        return scaled > 0.0F ? maximum : minimum;
    }
    return std::clamp(scaled, minimum, maximum);
}

[[nodiscard]] Core::Status applyPan2D(
    EditorViewportNavigationSnapshot& candidate,
    const EditorViewportNavigationConfig& config,
    EditorViewportVector2 pixelDelta)
{
    if (!finite(pixelDelta))
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor 2D viewport pan delta must be finite");
    }
    const float worldUnitsPerPixel =
        config.twoDWorldUnitsPerPixelAtZoomOne / candidate.twoD.zoom;
    const EditorViewportVector2 center{
        .x = candidate.twoD.center.x - pixelDelta.x * worldUnitsPerPixel,
        .y = candidate.twoD.center.y + pixelDelta.y * worldUnitsPerPixel,
    };
    if (!finite(center))
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor 2D viewport pan exceeds the supported range");
    }
    candidate.twoD.center = center;
    return Core::success();
}

[[nodiscard]] Core::Status applyZoom2D(
    EditorViewportNavigationSnapshot& candidate,
    const EditorViewportNavigationConfig& config,
    const EditorViewportNavigationInput& input)
{
    if (!finite(input.wheelSteps) || !finite(input.viewportSizePixels) ||
        input.viewportSizePixels.x <= 0.0F || input.viewportSizePixels.y <= 0.0F ||
        !finite(input.anchorPixels) || input.anchorPixels.x < 0.0F ||
        input.anchorPixels.y < 0.0F ||
        input.anchorPixels.x > input.viewportSizePixels.x ||
        input.anchorPixels.y > input.viewportSizePixels.y)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor 2D viewport zoom input is invalid");
    }

    const float scale = std::pow(config.twoDZoomStepFactor, input.wheelSteps);
    const float zoom = clampScaled(candidate.twoD.zoom,
                                   scale,
                                   config.minimumTwoDZoom,
                                   config.maximumTwoDZoom,
                                   true);
    const float oldWorldUnitsPerPixel =
        config.twoDWorldUnitsPerPixelAtZoomOne / candidate.twoD.zoom;
    const float newWorldUnitsPerPixel =
        config.twoDWorldUnitsPerPixelAtZoomOne / zoom;
    const EditorViewportVector2 anchorFromCenter{
        .x = input.anchorPixels.x - input.viewportSizePixels.x * 0.5F,
        .y = input.viewportSizePixels.y * 0.5F - input.anchorPixels.y,
    };
    const EditorViewportVector2 center{
        .x = candidate.twoD.center.x +
             anchorFromCenter.x * (oldWorldUnitsPerPixel - newWorldUnitsPerPixel),
        .y = candidate.twoD.center.y +
             anchorFromCenter.y * (oldWorldUnitsPerPixel - newWorldUnitsPerPixel),
    };
    if (!finite(center))
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor 2D viewport zoom exceeds the supported range");
    }
    candidate.twoD.center = center;
    candidate.twoD.zoom = zoom;
    return Core::success();
}

[[nodiscard]] float normalizedYaw(float yawRadians) noexcept
{
    return std::remainder(yawRadians, std::numbers::pi_v<float> * 2.0F);
}

[[nodiscard]] Core::Status applyOrbit3D(
    EditorViewportNavigationSnapshot& candidate,
    const EditorViewportNavigationConfig& config,
    EditorViewportVector2 pixelDelta)
{
    if (!finite(pixelDelta))
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor 3D viewport orbit delta must be finite");
    }
    const float yaw = candidate.threeD.yawRadians +
                      pixelDelta.x * config.threeDOrbitRadiansPerPixel;
    const float pitch = candidate.threeD.pitchRadians +
                        pixelDelta.y * config.threeDOrbitRadiansPerPixel;
    if (!finite(yaw) || !finite(pitch))
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor 3D viewport orbit exceeds the supported range");
    }
    candidate.threeD.yawRadians = normalizedYaw(yaw);
    candidate.threeD.pitchRadians =
        std::clamp(pitch,
                   config.minimumThreeDPitchRadians,
                   config.maximumThreeDPitchRadians);
    return Core::success();
}

[[nodiscard]] Core::Status applyPan3D(
    EditorViewportNavigationSnapshot& candidate,
    const EditorViewportNavigationConfig& config,
    EditorViewportVector2 pixelDelta)
{
    if (!finite(pixelDelta))
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor 3D viewport pan delta must be finite");
    }

    const float yaw = candidate.threeD.yawRadians;
    const float pitch = candidate.threeD.pitchRadians;
    const EditorViewportVector3 right{
        .x = std::cos(yaw),
        .y = 0.0F,
        .z = -std::sin(yaw),
    };
    const EditorViewportVector3 up{
        .x = -std::sin(yaw) * std::sin(pitch),
        .y = std::cos(pitch),
        .z = -std::cos(yaw) * std::sin(pitch),
    };
    const float scale = candidate.threeD.distance *
                        config.threeDPanWorldUnitsPerPixelAtUnitDistance;
    const EditorViewportVector3 target{
        .x = candidate.threeD.target.x - right.x * pixelDelta.x * scale +
             up.x * pixelDelta.y * scale,
        .y = candidate.threeD.target.y - right.y * pixelDelta.x * scale +
             up.y * pixelDelta.y * scale,
        .z = candidate.threeD.target.z - right.z * pixelDelta.x * scale +
             up.z * pixelDelta.y * scale,
    };
    if (!finite(target))
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor 3D viewport pan exceeds the supported range");
    }
    candidate.threeD.target = target;
    return Core::success();
}

[[nodiscard]] Core::Status applyDolly3D(
    EditorViewportNavigationSnapshot& candidate,
    const EditorViewportNavigationConfig& config,
    float wheelSteps)
{
    if (!finite(wheelSteps))
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor 3D viewport dolly input must be finite");
    }
    const float scale = std::pow(config.threeDDollyStepFactor, wheelSteps);
    candidate.threeD.distance = clampScaled(candidate.threeD.distance,
                                            scale,
                                            config.minimumThreeDDistance,
                                            config.maximumThreeDDistance,
                                            false);
    return Core::success();
}

[[nodiscard]] Core::Status applyInput(
    EditorViewportNavigationSnapshot& candidate,
    const EditorViewportNavigationConfig& config,
    const EditorViewportNavigationInput& input)
{
    switch (input.kind)
    {
    case EditorViewportNavigationInputKind::Pan2D:
        return applyPan2D(candidate, config, input.pixelDelta);
    case EditorViewportNavigationInputKind::Zoom2D:
        return applyZoom2D(candidate, config, input);
    case EditorViewportNavigationInputKind::Orbit3D:
        return applyOrbit3D(candidate, config, input.pixelDelta);
    case EditorViewportNavigationInputKind::Pan3D:
        return applyPan3D(candidate, config, input.pixelDelta);
    case EditorViewportNavigationInputKind::Dolly3D:
        return applyDolly3D(candidate, config, input.wheelSteps);
    default:
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor viewport navigation input kind is invalid");
    }
}

[[nodiscard]] Core::u64 nextRevision(Core::u64 revision) noexcept
{
    return revision == (std::numeric_limits<Core::u64>::max)()
               ? (std::numeric_limits<Core::u64>::max)()
               : revision + 1U;
}

} // namespace

Core::Result<EditorViewportNavigation> EditorViewportNavigation::Create(
    EditorViewportNavigationConfig config,
    EditorViewport2DNavigationState twoD,
    EditorViewport3DNavigationState threeD)
{
    if (auto status = validateConfig(config); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = validateState(config, twoD, threeD); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    threeD.yawRadians = normalizedYaw(threeD.yawRadians);
    return EditorViewportNavigation{
        config,
        EditorViewportNavigationSnapshot{.twoD = twoD, .threeD = threeD},
    };
}

EditorViewportNavigation::EditorViewportNavigation(
    EditorViewportNavigationConfig config,
    EditorViewportNavigationSnapshot snapshot) noexcept
    : m_config(config), m_snapshot(snapshot)
{
}

Core::Status EditorViewportNavigation::apply(
    std::span<const EditorViewportNavigationInput> inputs)
{
    if (inputs.size() > EditorViewportNavigationLimits::MaximumInputCommandsPerBatch)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor viewport navigation input batch exceeds fixed capacity");
    }

    EditorViewportNavigationSnapshot candidate = m_snapshot;
    for (const auto& input : inputs)
    {
        if (auto status = applyInput(candidate, m_config, input); !status)
        {
            return status;
        }
    }
    if (candidate.twoD == m_snapshot.twoD && candidate.threeD == m_snapshot.threeD)
    {
        return Core::success();
    }
    candidate.revision = nextRevision(m_snapshot.revision);
    m_snapshot = candidate;
    return Core::success();
}

Core::Status EditorViewportNavigation::pan2D(EditorViewportVector2 pixelDelta)
{
    const EditorViewportNavigationInput input{
        .kind = EditorViewportNavigationInputKind::Pan2D,
        .pixelDelta = pixelDelta,
    };
    return apply(std::span{&input, 1U});
}

Core::Status EditorViewportNavigation::zoom2D(
    float wheelSteps,
    EditorViewportVector2 viewportSizePixels,
    EditorViewportVector2 anchorPixels)
{
    const EditorViewportNavigationInput input{
        .kind = EditorViewportNavigationInputKind::Zoom2D,
        .wheelSteps = wheelSteps,
        .viewportSizePixels = viewportSizePixels,
        .anchorPixels = anchorPixels,
    };
    return apply(std::span{&input, 1U});
}

Core::Status EditorViewportNavigation::orbit3D(EditorViewportVector2 pixelDelta)
{
    const EditorViewportNavigationInput input{
        .kind = EditorViewportNavigationInputKind::Orbit3D,
        .pixelDelta = pixelDelta,
    };
    return apply(std::span{&input, 1U});
}

Core::Status EditorViewportNavigation::pan3D(EditorViewportVector2 pixelDelta)
{
    const EditorViewportNavigationInput input{
        .kind = EditorViewportNavigationInputKind::Pan3D,
        .pixelDelta = pixelDelta,
    };
    return apply(std::span{&input, 1U});
}

Core::Status EditorViewportNavigation::dolly3D(float wheelSteps)
{
    const EditorViewportNavigationInput input{
        .kind = EditorViewportNavigationInputKind::Dolly3D,
        .wheelSteps = wheelSteps,
    };
    return apply(std::span{&input, 1U});
}

} // namespace Tina::Editor
