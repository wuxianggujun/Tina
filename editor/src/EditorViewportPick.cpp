#include <tina/editor/EditorViewportPick.hpp>

#include <tina/math/Constants.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::Editor {

std::optional<Math::Ray> editorViewportPickRay(
    const EditorViewportRayQuery& query) noexcept
{
    if (!Math::isFinite(query.cameraPosition) || !Math::isFinite(query.cameraRotation)
        || !std::isfinite(query.verticalFovDegrees) || !std::isfinite(query.viewportWidth)
        || !std::isfinite(query.viewportHeight) || !std::isfinite(query.pointerX)
        || !std::isfinite(query.pointerY)) {
        return std::nullopt;
    }
    if (query.viewportWidth <= 0.0F || query.viewportHeight <= 0.0F
        || query.verticalFovDegrees <= 0.0F || query.verticalFovDegrees >= 180.0F) {
        return std::nullopt;
    }

    const Math::Quaternion rotation = Math::normalized(query.cameraRotation);
    if (rotation == Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F}) {
        return std::nullopt;
    }

    const float tangent = std::tan(query.verticalFovDegrees * Math::DegreesToRadians * 0.5F);
    if (!std::isfinite(tangent) || tangent <= 0.0F) {
        return std::nullopt;
    }
    const float aspect = query.viewportWidth / query.viewportHeight;
    if (!std::isfinite(aspect) || aspect <= 0.0F) {
        return std::nullopt;
    }

    // Logical pixels to normalized device coordinates. Y flips because logical UI
    // space grows downward while the camera's up axis grows upward.
    const float normalizedDeviceX =
        (query.pointerX / query.viewportWidth) * 2.0F - 1.0F;
    const float normalizedDeviceY =
        1.0F - (query.pointerY / query.viewportHeight) * 2.0F;

    // Camera looks down its local -Z, so the ray leaves through negative Z. This
    // matches the projection the viewport renders with, which divides by -z.
    const Math::Vec3 cameraSpaceDirection{
        normalizedDeviceX * tangent * aspect,
        normalizedDeviceY * tangent,
        -1.0F,
    };
    const Math::Vec3 worldDirection = Math::rotate(rotation, cameraSpaceDirection);
    return Math::makeRay(query.cameraPosition, worldDirection);
}

std::optional<EditorViewportPickHit> pickNearestViewportCandidate(
    const Math::Ray& ray,
    std::span<const EditorViewportPickCandidate> candidates) noexcept
{
    if (!Math::isFinite(ray)) {
        return std::nullopt;
    }

    std::optional<EditorViewportPickHit> nearest{};
    const Core::usize considered =
        (std::min)(candidates.size(), EditorViewportPickCandidateCapacity);
    for (Core::usize index = 0; index < considered; ++index) {
        const EditorViewportPickCandidate& candidate = candidates[index];
        if (candidate.stableId == 0 || !Math::isValid(candidate.worldBounds)) {
            continue;
        }
        const std::optional<Math::RayHit> hit = Math::raycast(ray, candidate.worldBounds);
        if (!hit) {
            continue;
        }
        if (!nearest.has_value() || hit->distance < nearest->distanceMeters
            || (hit->distance == nearest->distanceMeters
                && candidate.stableId < nearest->stableId)) {
            nearest = EditorViewportPickHit{
                .stableId = candidate.stableId,
                .distanceMeters = hit->distance,
                .worldPoint = hit->point,
            };
        }
    }
    return nearest;
}

} // namespace Tina::Editor
