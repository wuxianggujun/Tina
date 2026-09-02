#include <tina/editor/EditorViewportGrid.hpp>

#include <tina/editor/EditorErrors.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace Tina::Editor {
namespace {

inline constexpr float ReferenceWorldHeight2D = 9.0F;
inline constexpr float MinimumMinorLinePixels = 10.0F;
inline constexpr float CoordinateTolerance = 0.0001F;
inline constexpr float MinimumPerspectivePitch = -1.55334306F;
inline constexpr float MaximumPerspectivePitch = 1.55334306F;
inline constexpr float PerspectiveGridExtentInCameraDistances = 16.0F;
inline constexpr float MaximumCameraCoordinate = 10'000'000.0F;

struct GridPublication final {
    std::array<EditorViewportGridSegment, EditorViewportGridSegmentCapacity>
        segments{};
    EditorViewportGridStats stats{};
};

struct GridPoint3D final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct CameraSpacePoint final {
    float x = 0.0F;
    float y = 0.0F;
    float depth = 0.0F;
};

struct ProjectedPoint2D final {
    float x = 0.0F;
    float y = 0.0F;
};

struct PerspectiveGridCamera final {
    GridPoint3D position{};
    GridPoint3D right{};
    GridPoint3D up{};
    GridPoint3D forward{};
    float tangentHalfVerticalFov = 0.0F;
    float aspect = 1.0F;
    float nearDepth = 0.01F;
};

[[nodiscard]] Core::Status validateConfig(
    const EditorViewportGridConfig& config) noexcept
{
    if ((config.projection != EditorViewportGridProjection::Orthographic2D &&
         config.projection != EditorViewportGridProjection::Perspective3D) ||
        !std::isfinite(config.logicalWidth) ||
        !std::isfinite(config.logicalHeight) ||
        !std::isfinite(config.zoomPercent) ||
        !std::isfinite(config.cameraCenterX) ||
        !std::isfinite(config.cameraCenterY) ||
        !std::isfinite(config.cameraTargetX) ||
        !std::isfinite(config.cameraTargetY) ||
        !std::isfinite(config.cameraTargetZ) ||
        !std::isfinite(config.cameraYawRadians) ||
        !std::isfinite(config.cameraPitchRadians) ||
        !std::isfinite(config.cameraDistance) ||
        !std::isfinite(config.verticalFovDegrees) ||
        !std::isfinite(config.worldGridStep) || config.logicalWidth < 1.0F ||
        config.logicalHeight < 1.0F || config.logicalWidth > 1'000'000.0F ||
        config.logicalHeight > 1'000'000.0F || config.zoomPercent < 25.0F ||
        config.zoomPercent > 400.0F ||
        std::abs(config.cameraCenterX) > MaximumCameraCoordinate ||
        std::abs(config.cameraCenterY) > MaximumCameraCoordinate ||
        std::abs(config.cameraTargetX) > MaximumCameraCoordinate ||
        std::abs(config.cameraTargetY) > MaximumCameraCoordinate ||
        std::abs(config.cameraTargetZ) > MaximumCameraCoordinate ||
        std::abs(config.cameraYawRadians) > std::numbers::pi_v<float> ||
        config.cameraPitchRadians < MinimumPerspectivePitch ||
        config.cameraPitchRadians > MaximumPerspectivePitch ||
        config.cameraDistance < 0.01F || config.cameraDistance > 100'000.0F ||
        config.verticalFovDegrees < 5.0F || config.verticalFovDegrees > 150.0F ||
        config.worldGridStep < 0.0001F || config.worldGridStep > 1'000'000.0F ||
        config.majorLineEvery < 2U || config.majorLineEvery > 100U) {
        return Core::failure(
            EditorErrorCode::InvalidConfiguration,
            "Editor viewport grid requires finite bounded extents, camera projection, zoom, and spacing");
    }
    return Core::success();
}

[[nodiscard]] bool isAxis(EditorViewportGridSegmentKind kind) noexcept
{
    return kind == EditorViewportGridSegmentKind::AxisX ||
           kind == EditorViewportGridSegmentKind::AxisY ||
           kind == EditorViewportGridSegmentKind::AxisZ;
}

[[nodiscard]] Core::Status appendSegment(
    GridPublication& publication, EditorViewportGridSegment segment) noexcept
{
    segment.startX = std::clamp(segment.startX, 0.0F, 1.0F);
    segment.startY = std::clamp(segment.startY, 0.0F, 1.0F);
    segment.endX = std::clamp(segment.endX, 0.0F, 1.0F);
    segment.endY = std::clamp(segment.endY, 0.0F, 1.0F);
    if (publication.stats.segmentCount >= EditorViewportGridSegmentCapacity) {
        return Core::failure(
            EditorErrorCode::DocumentCapacityExceeded,
            "Editor viewport grid exceeded its fixed segment capacity");
    }
    publication.segments[publication.stats.segmentCount++] = segment;
    if (isAxis(segment.kind)) {
        ++publication.stats.axisSegmentCount;
    } else if (segment.kind == EditorViewportGridSegmentKind::Major) {
        ++publication.stats.majorSegmentCount;
    } else {
        ++publication.stats.minorSegmentCount;
    }
    return Core::success();
}

[[nodiscard]] float adaptiveGridStep(float baseStep,
                                     float pixelsPerWorldUnit) noexcept
{
    constexpr std::array Multipliers{1.0F, 2.0F, 5.0F};
    float decade = 1.0F;
    for (;;) {
        for (const float multiplier : Multipliers) {
            const float candidate = baseStep * decade * multiplier;
            if (candidate * pixelsPerWorldUnit >= MinimumMinorLinePixels) {
                return candidate;
            }
        }
        decade *= 10.0F;
    }
}

[[nodiscard]] EditorViewportGridSegmentKind classify2DLine(
    float coordinate, bool vertical, const EditorViewportGridConfig& config) noexcept
{
    if (std::abs(coordinate) <= config.worldGridStep * CoordinateTolerance) {
        return vertical ? EditorViewportGridSegmentKind::AxisY
                        : EditorViewportGridSegmentKind::AxisX;
    }
    const auto baseIndex = static_cast<long long>(
        std::llround(coordinate / config.worldGridStep));
    return baseIndex % static_cast<long long>(config.majorLineEvery) == 0
               ? EditorViewportGridSegmentKind::Major
               : EditorViewportGridSegmentKind::Minor;
}

[[nodiscard]] Core::Status buildOrthographic2D(
    const EditorViewportGridConfig& config, GridPublication& publication) noexcept
{
    const float visibleWorldHeight =
        ReferenceWorldHeight2D * 100.0F / config.zoomPercent;
    const float visibleWorldWidth =
        visibleWorldHeight * config.logicalWidth / config.logicalHeight;
    const float left = config.cameraCenterX - visibleWorldWidth * 0.5F;
    const float right = config.cameraCenterX + visibleWorldWidth * 0.5F;
    const float bottom = config.cameraCenterY - visibleWorldHeight * 0.5F;
    const float top = config.cameraCenterY + visibleWorldHeight * 0.5F;
    const float step = adaptiveGridStep(
        config.worldGridStep, config.logicalHeight / visibleWorldHeight);

    const double firstVertical =
        std::ceil(static_cast<double>(left) / step) * step;
    for (double x = firstVertical; x <= static_cast<double>(right) + step * 0.25;
         x += step) {
        const float normalizedX =
            static_cast<float>((x - left) / visibleWorldWidth);
        if (auto status = appendSegment(
                publication,
                {
                    .startX = normalizedX,
                    .startY = 0.0F,
                    .endX = normalizedX,
                    .endY = 1.0F,
                    .kind = classify2DLine(static_cast<float>(x), true, config),
                });
            !status) {
            return status;
        }
    }

    const double firstHorizontal =
        std::ceil(static_cast<double>(bottom) / step) * step;
    for (double y = firstHorizontal; y <= static_cast<double>(top) + step * 0.25;
         y += step) {
        const float normalizedY =
            1.0F - static_cast<float>((y - bottom) / visibleWorldHeight);
        if (auto status = appendSegment(
                publication,
                {
                    .startX = 0.0F,
                    .startY = normalizedY,
                    .endX = 1.0F,
                    .endY = normalizedY,
                    .kind = classify2DLine(static_cast<float>(y), false, config),
                });
            !status) {
            return status;
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Status buildPerspective3D(
    const EditorViewportGridConfig& config, GridPublication& publication) noexcept
{
    const float sineYaw = std::sin(config.cameraYawRadians);
    const float cosineYaw = std::cos(config.cameraYawRadians);
    const float sinePitch = std::sin(config.cameraPitchRadians);
    const float cosinePitch = std::cos(config.cameraPitchRadians);
    const float fovRadians = config.verticalFovDegrees *
                             std::numbers::pi_v<float> / 180.0F;
    const PerspectiveGridCamera camera{
        .position = {
            .x = config.cameraTargetX + sineYaw * cosinePitch * config.cameraDistance,
            .y = config.cameraTargetY + sinePitch * config.cameraDistance,
            .z = config.cameraTargetZ + cosineYaw * cosinePitch * config.cameraDistance,
        },
        .right = {.x = cosineYaw, .y = 0.0F, .z = -sineYaw},
        .up = {
            .x = -sineYaw * sinePitch,
            .y = cosinePitch,
            .z = -cosineYaw * sinePitch,
        },
        .forward = {
            .x = -sineYaw * cosinePitch,
            .y = -sinePitch,
            .z = -cosineYaw * cosinePitch,
        },
        .tangentHalfVerticalFov = std::tan(fovRadians * 0.5F),
        .aspect = config.logicalWidth / config.logicalHeight,
        .nearDepth = (std::max)(0.001F, config.cameraDistance * 0.0001F),
    };

    const auto transformPoint = [&camera](GridPoint3D point) noexcept {
        const GridPoint3D relative{
            .x = point.x - camera.position.x,
            .y = point.y - camera.position.y,
            .z = point.z - camera.position.z,
        };
        const auto dot = [&relative](GridPoint3D basis) noexcept {
            return relative.x * basis.x + relative.y * basis.y +
                   relative.z * basis.z;
        };
        return CameraSpacePoint{
            .x = dot(camera.right),
            .y = dot(camera.up),
            .depth = dot(camera.forward),
        };
    };
    const auto interpolate = [](CameraSpacePoint start,
                                CameraSpacePoint end,
                                float t) noexcept {
        return CameraSpacePoint{
            .x = start.x + (end.x - start.x) * t,
            .y = start.y + (end.y - start.y) * t,
            .depth = start.depth + (end.depth - start.depth) * t,
        };
    };
    const auto clipBoundary = [](float p, float q, float& first,
                                 float& last) noexcept {
        if (std::abs(p) <= 1.0e-7F) {
            return q >= 0.0F;
        }
        const float ratio = q / p;
        if (p < 0.0F) {
            if (ratio > last) {
                return false;
            }
            first = (std::max)(first, ratio);
        } else {
            if (ratio < first) {
                return false;
            }
            last = (std::min)(last, ratio);
        }
        return true;
    };
    const auto appendProjected = [&](GridPoint3D worldStart,
                                     GridPoint3D worldEnd,
                                     EditorViewportGridSegmentKind kind)
        -> Core::Status {
        CameraSpacePoint start = transformPoint(worldStart);
        CameraSpacePoint end = transformPoint(worldEnd);
        if (start.depth < camera.nearDepth && end.depth < camera.nearDepth) {
            return Core::success();
        }
        if (start.depth < camera.nearDepth || end.depth < camera.nearDepth) {
            const float denominator = end.depth - start.depth;
            if (std::abs(denominator) <= 1.0e-7F) {
                return Core::success();
            }
            const float t = (camera.nearDepth - start.depth) / denominator;
            if (start.depth < camera.nearDepth) {
                start = interpolate(start, end, t);
            } else {
                end = interpolate(start, end, t);
            }
        }

        const auto project = [&camera](CameraSpacePoint point) noexcept {
            return ProjectedPoint2D{
                .x = 0.5F + point.x /
                                  (point.depth * camera.tangentHalfVerticalFov *
                                   camera.aspect) *
                                  0.5F,
                .y = 0.5F - point.y /
                                  (point.depth * camera.tangentHalfVerticalFov) *
                                  0.5F,
            };
        };
        ProjectedPoint2D projectedStart = project(start);
        ProjectedPoint2D projectedEnd = project(end);
        if (!std::isfinite(projectedStart.x) ||
            !std::isfinite(projectedStart.y) ||
            !std::isfinite(projectedEnd.x) ||
            !std::isfinite(projectedEnd.y)) {
            return Core::failure(EditorErrorCode::InvalidConfiguration,
                                 "Editor perspective grid projection is not finite");
        }

        const float deltaX = projectedEnd.x - projectedStart.x;
        const float deltaY = projectedEnd.y - projectedStart.y;
        float first = 0.0F;
        float last = 1.0F;
        if (!clipBoundary(-deltaX, projectedStart.x, first, last) ||
            !clipBoundary(deltaX, 1.0F - projectedStart.x, first, last) ||
            !clipBoundary(-deltaY, projectedStart.y, first, last) ||
            !clipBoundary(deltaY, 1.0F - projectedStart.y, first, last) ||
            last < first) {
            return Core::success();
        }
        const EditorViewportGridSegment segment{
            .startX = projectedStart.x + deltaX * first,
            .startY = projectedStart.y + deltaY * first,
            .endX = projectedStart.x + deltaX * last,
            .endY = projectedStart.y + deltaY * last,
            .kind = kind,
        };
        if (std::abs(segment.endX - segment.startX) <= 1.0e-6F &&
            std::abs(segment.endY - segment.startY) <= 1.0e-6F) {
            return Core::success();
        }
        return appendSegment(publication, segment);
    };

    const float halfExtent = (std::max)(
        config.cameraDistance * PerspectiveGridExtentInCameraDistances,
        config.worldGridStep * static_cast<float>(config.majorLineEvery) * 4.0F);
    const float pixelsPerWorldUnit =
        config.logicalHeight /
        (2.0F * config.cameraDistance * camera.tangentHalfVerticalFov);
    float step = adaptiveGridStep(config.worldGridStep, pixelsPerWorldUnit);
    constexpr Core::usize MaximumLinesPerAxis =
        EditorViewportGridSegmentCapacity / 2U - 2U;
    while (static_cast<Core::usize>(std::ceil(halfExtent * 2.0F / step)) + 2U >
           MaximumLinesPerAxis) {
        step *= 2.0F;
    }

    const float minimumX = config.cameraTargetX - halfExtent;
    const float maximumX = config.cameraTargetX + halfExtent;
    const float minimumZ = config.cameraTargetZ - halfExtent;
    const float maximumZ = config.cameraTargetZ + halfExtent;
    const auto lineKind = [&](float coordinate,
                              EditorViewportGridSegmentKind axisKind) noexcept {
        if (std::abs(coordinate) <= step * CoordinateTolerance) {
            return axisKind;
        }
        const auto index = static_cast<long long>(std::llround(coordinate / step));
        return index % static_cast<long long>(config.majorLineEvery) == 0
                   ? EditorViewportGridSegmentKind::Major
                   : EditorViewportGridSegmentKind::Minor;
    };

    const double firstZ = std::ceil(static_cast<double>(minimumZ) / step) * step;
    for (double z = firstZ; z <= static_cast<double>(maximumZ) + step * 0.25;
         z += step) {
        if (auto status = appendProjected(
                {.x = minimumX, .y = 0.0F, .z = static_cast<float>(z)},
                {.x = maximumX, .y = 0.0F, .z = static_cast<float>(z)},
                lineKind(static_cast<float>(z),
                         EditorViewportGridSegmentKind::AxisX));
            !status) {
            return status;
        }
    }

    const double firstX = std::ceil(static_cast<double>(minimumX) / step) * step;
    for (double x = firstX; x <= static_cast<double>(maximumX) + step * 0.25;
         x += step) {
        if (auto status = appendProjected(
                {.x = static_cast<float>(x), .y = 0.0F, .z = minimumZ},
                {.x = static_cast<float>(x), .y = 0.0F, .z = maximumZ},
                lineKind(static_cast<float>(x),
                         EditorViewportGridSegmentKind::AxisZ));
            !status) {
            return status;
        }
    }
    return Core::success();
}

[[nodiscard]] Core::u64 nextRevision(Core::u64 revision) noexcept
{
    return revision == (std::numeric_limits<Core::u64>::max)()
               ? (std::numeric_limits<Core::u64>::max)()
               : revision + 1U;
}

} // namespace

Core::Result<bool> EditorViewportGrid::update(
    const EditorViewportGridConfig& config)
{
    if (auto status = validateConfig(config); !status) {
        return Core::failure(std::move(status.error()));
    }
    if (m_hasConfig && m_config == config) {
        return false;
    }

    GridPublication candidate{};
    const Core::Status buildStatus =
        config.projection == EditorViewportGridProjection::Orthographic2D
            ? buildOrthographic2D(config, candidate)
            : buildPerspective3D(config, candidate);
    if (!buildStatus) {
        return Core::failure(std::move(buildStatus.error()));
    }

    const bool changed =
        !m_hasConfig || candidate.stats.segmentCount != m_stats.segmentCount ||
        !std::equal(candidate.segments.begin(),
                    candidate.segments.begin() + candidate.stats.segmentCount,
                    m_segments.begin());
    m_config = config;
    m_hasConfig = true;
    if (!changed) {
        return false;
    }

    std::copy_n(candidate.segments.begin(), candidate.stats.segmentCount,
                m_segments.begin());
    candidate.stats.revision = nextRevision(m_stats.revision);
    m_stats = candidate.stats;
    return true;
}

} // namespace Tina::Editor
