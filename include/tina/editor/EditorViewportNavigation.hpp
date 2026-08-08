#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <span>

namespace Tina::Editor {

namespace EditorViewportNavigationLimits {

inline constexpr Core::usize MaximumInputCommandsPerBatch = 64;

} // namespace EditorViewportNavigationLimits

struct EditorViewportVector2 final {
    float x = 0.0F;
    float y = 0.0F;

    friend bool operator==(const EditorViewportVector2&,
                           const EditorViewportVector2&) = default;
};

struct EditorViewportVector3 final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    friend bool operator==(const EditorViewportVector3&,
                           const EditorViewportVector3&) = default;
};

struct EditorViewport2DNavigationState final {
    EditorViewportVector2 center{};
    float zoom = 1.0F;

    friend bool operator==(const EditorViewport2DNavigationState&,
                           const EditorViewport2DNavigationState&) = default;
};

struct EditorViewport3DNavigationState final {
    EditorViewportVector3 target{};
    float yawRadians = 0.0F;
    float pitchRadians = 0.0F;
    float distance = 10.0F;

    friend bool operator==(const EditorViewport3DNavigationState&,
                           const EditorViewport3DNavigationState&) = default;
};

struct EditorViewportNavigationConfig final {
    float twoDWorldUnitsPerPixelAtZoomOne = 0.01F;
    float minimumTwoDZoom = 0.05F;
    float maximumTwoDZoom = 64.0F;
    float twoDZoomStepFactor = 1.125F;

    float threeDOrbitRadiansPerPixel = 0.01F;
    float threeDPanWorldUnitsPerPixelAtUnitDistance = 0.0025F;
    float minimumThreeDDistance = 0.05F;
    float maximumThreeDDistance = 10000.0F;
    float threeDDollyStepFactor = 1.125F;
    float minimumThreeDPitchRadians = -1.55334306F;
    float maximumThreeDPitchRadians = 1.55334306F;
};

enum class EditorViewportNavigationInputKind : Core::u8 {
    Pan2D = 0,
    Zoom2D = 1,
    Orbit3D = 2,
    Pan3D = 3,
    Dolly3D = 4,
};

// One backend-neutral pointer/wheel navigation command. pixelDelta is used by
// pan/orbit, while wheelSteps is used by zoom/dolly. Zoom2D additionally uses
// viewportSizePixels and anchorPixels to keep the world point below the pointer
// stationary.
struct EditorViewportNavigationInput final {
    EditorViewportNavigationInputKind kind = EditorViewportNavigationInputKind::Pan2D;
    EditorViewportVector2 pixelDelta{};
    float wheelSteps = 0.0F;
    EditorViewportVector2 viewportSizePixels{};
    EditorViewportVector2 anchorPixels{};
};

struct EditorViewportNavigationSnapshot final {
    EditorViewport2DNavigationState twoD{};
    EditorViewport3DNavigationState threeD{};
    Core::u64 revision = 1;

    friend bool operator==(const EditorViewportNavigationSnapshot&,
                           const EditorViewportNavigationSnapshot&) = default;
};

// Allocation-free navigation owner shared by 2D and 3D editor workspaces.
// apply() is an atomic fixed-capacity transaction: invalid commands preserve
// both camera states and revision, and a changed batch advances revision once.
class EditorViewportNavigation final {
public:
    [[nodiscard]] static Core::Result<EditorViewportNavigation>
    Create(EditorViewportNavigationConfig config = {},
           EditorViewport2DNavigationState twoD = {},
           EditorViewport3DNavigationState threeD = {});

    [[nodiscard]] const EditorViewportNavigationConfig& config() const noexcept
    {
        return m_config;
    }
    [[nodiscard]] const EditorViewportNavigationSnapshot& snapshot() const noexcept
    {
        return m_snapshot;
    }
    [[nodiscard]] const EditorViewport2DNavigationState& twoD() const noexcept
    {
        return m_snapshot.twoD;
    }
    [[nodiscard]] const EditorViewport3DNavigationState& threeD() const noexcept
    {
        return m_snapshot.threeD;
    }
    [[nodiscard]] Core::u64 revision() const noexcept { return m_snapshot.revision; }

    [[nodiscard]] Core::Status
    apply(std::span<const EditorViewportNavigationInput> inputs);

    [[nodiscard]] Core::Status pan2D(EditorViewportVector2 pixelDelta);
    [[nodiscard]] Core::Status zoom2D(float wheelSteps,
                                      EditorViewportVector2 viewportSizePixels,
                                      EditorViewportVector2 anchorPixels);
    [[nodiscard]] Core::Status orbit3D(EditorViewportVector2 pixelDelta);
    [[nodiscard]] Core::Status pan3D(EditorViewportVector2 pixelDelta);
    [[nodiscard]] Core::Status dolly3D(float wheelSteps);

private:
    EditorViewportNavigation(EditorViewportNavigationConfig config,
                             EditorViewportNavigationSnapshot snapshot) noexcept;

    EditorViewportNavigationConfig m_config{};
    EditorViewportNavigationSnapshot m_snapshot{};
};

} // namespace Tina::Editor
