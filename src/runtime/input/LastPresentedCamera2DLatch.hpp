#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/Camera2DPick.hpp>
#include <tina/render/RenderScene.hpp>

namespace Tina::Runtime::Input {

// Runtime-private latch of the Camera2D that last successfully required a
// present. Action Mapping must project unconsumed pointer samples with this
// snapshot, never with a later extraction-only camera.
class LastPresentedCamera2DLatch final {
  public:
    // Records the Camera2D from a submit-call-local world scene when present
    // will (or did) occur. Empty camera clears the latch. surfaceRevision is
    // the primary WindowSurface facts revision paired with that present.
    void notePresented(const Render::RenderSceneView& worldScene, u64 surfaceRevision) noexcept;

    void clear() noexcept;

    [[nodiscard]] bool hasCamera() const noexcept
    {
        return hasCamera_;
    }

    [[nodiscard]] const Render::RenderCamera2D* camera() const noexcept
    {
        return hasCamera_ ? &camera_ : nullptr;
    }

    [[nodiscard]] u64 cameraRevision() const noexcept
    {
        return cameraRevision_;
    }

    [[nodiscard]] u64 surfaceRevision() const noexcept
    {
        return surfaceRevision_;
    }

    // Project one logical pointer sample using the latched camera. Fails when
    // no camera has been presented yet. hit=false for viewport misses.
    [[nodiscard]] Core::Result<Render::WorldPointerSample>
    pickLogical(double logicalX, double logicalY, u32 logicalWidth, u32 logicalHeight,
                u64 inputSequence) const noexcept;

  private:
    Render::RenderCamera2D camera_{};
    u64 cameraRevision_ = 0;
    u64 surfaceRevision_ = 0;
    bool hasCamera_ = false;
};

} // namespace Tina::Runtime::Input
