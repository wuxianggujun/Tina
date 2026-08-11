#include <tina/scene/CameraFollow2D.hpp>

#include <tina/scene/SceneErrors.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::Scene {
namespace {

[[nodiscard]] bool isFinite(CameraFollowPoint2D point) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] Core::Status validateViewportAndBounds(
    CameraFollowPoint2D viewportHalfExtentsMeters,
    const std::optional<CameraFollowBounds2D>& worldBounds) noexcept
{
    if (!isFinite(viewportHalfExtentsMeters) || viewportHalfExtentsMeters.x <= 0.0F
        || viewportHalfExtentsMeters.y <= 0.0F) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "CameraFollow2D viewport half extents must be finite and greater than zero");
    }
    if (worldBounds
        && (!isFinite(worldBounds->minimum) || !isFinite(worldBounds->maximum)
            || worldBounds->minimum.x > worldBounds->maximum.x
            || worldBounds->minimum.y > worldBounds->maximum.y)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "CameraFollow2D world bounds are invalid");
    }
    return Core::success();
}

[[nodiscard]] CameraFollowPoint2D clampToBounds(
    CameraFollowPoint2D center,
    CameraFollowPoint2D viewportHalfExtentsMeters,
    const std::optional<CameraFollowBounds2D>& worldBounds) noexcept
{
    if (!worldBounds) {
        return center;
    }

    const auto clampAxis = [](float value, float minimum, float maximum, float halfExtent) noexcept {
        const double minimumCenter =
            static_cast<double>(minimum) + static_cast<double>(halfExtent);
        const double maximumCenter =
            static_cast<double>(maximum) - static_cast<double>(halfExtent);
        if (minimumCenter > maximumCenter) {
            return static_cast<float>(
                (static_cast<double>(minimum) + static_cast<double>(maximum)) * 0.5);
        }
        return static_cast<float>(
            std::clamp(static_cast<double>(value), minimumCenter, maximumCenter));
    };

    return {
        .x = clampAxis(
            center.x,
            worldBounds->minimum.x,
            worldBounds->maximum.x,
            viewportHalfExtentsMeters.x),
        .y = clampAxis(
            center.y,
            worldBounds->minimum.y,
            worldBounds->maximum.y,
            viewportHalfExtentsMeters.y),
    };
}

} // namespace

Core::Result<CameraFollow2D> CameraFollow2D::Create(CameraFollow2DConfig config) noexcept
{
    if (!isFinite(config.initialCenter) || !isFinite(config.deadZoneHalfExtentsMeters)
        || config.deadZoneHalfExtentsMeters.x < 0.0F
        || config.deadZoneHalfExtentsMeters.y < 0.0F
        || (config.maximumSpeedMetersPerSecond
            && (!std::isfinite(*config.maximumSpeedMetersPerSecond)
                || *config.maximumSpeedMetersPerSecond <= 0.0F))) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "CameraFollow2D configuration contains invalid center, dead zone, or maximum speed");
    }
    return CameraFollow2D{config};
}

Core::Status CameraFollow2D::fixedUpdate(const CameraFollow2DStep& step) noexcept
{
    if (!isFinite(step.target)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "CameraFollow2D target must be finite");
    }
    if (const Core::Status status =
            validateViewportAndBounds(step.viewportHalfExtentsMeters, step.worldBounds);
        !status) {
        return status;
    }

    const double deltaSeconds = step.fixedDelta.count();
    if (!(deltaSeconds > 0.0) || !std::isfinite(deltaSeconds)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "CameraFollow2D fixed delta must be finite and greater than zero");
    }

    CameraFollowPoint2D desired = m_currentCenter;
    const float minimumTargetX = m_currentCenter.x - m_config.deadZoneHalfExtentsMeters.x;
    const float maximumTargetX = m_currentCenter.x + m_config.deadZoneHalfExtentsMeters.x;
    const float minimumTargetY = m_currentCenter.y - m_config.deadZoneHalfExtentsMeters.y;
    const float maximumTargetY = m_currentCenter.y + m_config.deadZoneHalfExtentsMeters.y;
    if (step.target.x < minimumTargetX) {
        desired.x = step.target.x + m_config.deadZoneHalfExtentsMeters.x;
    } else if (step.target.x > maximumTargetX) {
        desired.x = step.target.x - m_config.deadZoneHalfExtentsMeters.x;
    }
    if (step.target.y < minimumTargetY) {
        desired.y = step.target.y + m_config.deadZoneHalfExtentsMeters.y;
    } else if (step.target.y > maximumTargetY) {
        desired.y = step.target.y - m_config.deadZoneHalfExtentsMeters.y;
    }

    CameraFollowPoint2D next = desired;
    if (m_config.maximumSpeedMetersPerSecond) {
        const double dx = static_cast<double>(desired.x) - static_cast<double>(m_currentCenter.x);
        const double dy = static_cast<double>(desired.y) - static_cast<double>(m_currentCenter.y);
        const double distance = std::hypot(dx, dy);
        const double maximumDistance =
            static_cast<double>(*m_config.maximumSpeedMetersPerSecond) * deltaSeconds;
        if (!std::isfinite(distance) || !std::isfinite(maximumDistance)) {
            return Core::failure(
                SceneErrorCode::InvalidComponent,
                "CameraFollow2D movement overflowed finite coordinates");
        }
        if (distance > maximumDistance && distance > 0.0) {
            const double scale = maximumDistance / distance;
            next.x = static_cast<float>(static_cast<double>(m_currentCenter.x) + dx * scale);
            next.y = static_cast<float>(static_cast<double>(m_currentCenter.y) + dy * scale);
        }
    }

    next = clampToBounds(next, step.viewportHalfExtentsMeters, step.worldBounds);
    if (!isFinite(next)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "CameraFollow2D update produced a non-finite center");
    }

    m_previousCenter = m_currentCenter;
    m_currentCenter = next;
    return Core::success();
}

Core::Status CameraFollow2D::snapTo(
    CameraFollowPoint2D center,
    CameraFollowPoint2D viewportHalfExtentsMeters,
    std::optional<CameraFollowBounds2D> worldBounds) noexcept
{
    if (!isFinite(center)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "CameraFollow2D snap center must be finite");
    }
    if (const Core::Status status =
            validateViewportAndBounds(viewportHalfExtentsMeters, worldBounds);
        !status) {
        return status;
    }
    const CameraFollowPoint2D clamped =
        clampToBounds(center, viewportHalfExtentsMeters, worldBounds);
    if (!isFinite(clamped)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "CameraFollow2D snap produced a non-finite center");
    }
    m_previousCenter = clamped;
    m_currentCenter = clamped;
    return Core::success();
}

Core::Result<CameraFollowPoint2D>
CameraFollow2D::interpolatedCenter(double interpolation) const noexcept
{
    if (!std::isfinite(interpolation) || interpolation < 0.0 || interpolation > 1.0) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "CameraFollow2D interpolation must be finite and within [0, 1]");
    }
    return CameraFollowPoint2D{
        .x = static_cast<float>(
            static_cast<double>(m_previousCenter.x)
            + (static_cast<double>(m_currentCenter.x) - static_cast<double>(m_previousCenter.x))
                * interpolation),
        .y = static_cast<float>(
            static_cast<double>(m_previousCenter.y)
            + (static_cast<double>(m_currentCenter.y) - static_cast<double>(m_previousCenter.y))
                * interpolation),
    };
}

} // namespace Tina::Scene
