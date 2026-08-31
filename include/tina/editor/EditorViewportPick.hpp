#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/math/Geometry3D.hpp>
#include <tina/math/Quaternion.hpp>
#include <tina/math/Vec.hpp>

#include <optional>
#include <span>

namespace Tina::Editor {

inline constexpr Core::usize EditorViewportPickCandidateCapacity = 64;

// World-space bounding sphere of one pickable object.
//
// The sphere carries its CENTER, not just a radius. A mesh whose authored
// localBounds sit away from its origin — which is the common case for imported
// glTF — has a pick volume offset from its transform position, and dropping the
// center puts the hot zone somewhere the object is not.
struct EditorViewportPickCandidate final {
    Core::u64 stableId = 0;
    Math::Sphere worldBounds{};

    friend bool operator==(const EditorViewportPickCandidate&,
                           const EditorViewportPickCandidate&) = default;
};

struct EditorViewportPickHit final {
    Core::u64 stableId = 0;
    // Along the pick ray, in world meters. Comparable across candidates.
    float distanceMeters = 0.0F;
    Math::Vec3 worldPoint{};

    friend bool operator==(const EditorViewportPickHit&,
                           const EditorViewportPickHit&) = default;
};

// Inputs for turning one viewport-local logical pixel into a world ray.
//
// The field of view and the camera basis must be the same values the viewport
// renders with. A pick ray derived from a different FOV still looks plausible but
// silently drifts from the picture, and that class of bug is very hard to see.
struct EditorViewportRayQuery final {
    Math::Vec3 cameraPosition{};
    Math::Quaternion cameraRotation{};
    float verticalFovDegrees = 0.0F;
    // Viewport extent in logical pixels.
    float viewportWidth = 0.0F;
    float viewportHeight = 0.0F;
    // Pointer position relative to the viewport origin, in logical pixels.
    float pointerX = 0.0F;
    float pointerY = 0.0F;
};

// Builds the world-space ray through a viewport pixel for a perspective camera
// looking down its local -Z.
//
// Returns nullopt for a degenerate camera rotation, a zero-area viewport, an
// out-of-range field of view, or non-finite input, rather than producing an
// arbitrary ray that would pick something at random.
[[nodiscard]] std::optional<Math::Ray> editorViewportPickRay(
    const EditorViewportRayQuery& query) noexcept;

// Nearest hit along the ray wins.
//
// Nearest, not smallest-on-screen: a distant small object must never take a click
// away from the large near object in front of it. Ties break on the lower stableId
// so the outcome is deterministic regardless of candidate order.
//
// Candidates beyond EditorViewportPickCandidateCapacity are ignored, matching the
// fixed-capacity convention used by the marquee selection surface.
[[nodiscard]] std::optional<EditorViewportPickHit> pickNearestViewportCandidate(
    const Math::Ray& ray,
    std::span<const EditorViewportPickCandidate> candidates) noexcept;

} // namespace Tina::Editor
