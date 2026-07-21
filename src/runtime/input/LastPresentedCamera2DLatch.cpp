#include "LastPresentedCamera2DLatch.hpp"

#include <tina/runtime/RuntimeErrors.hpp>

namespace Tina::Runtime::Input {

void LastPresentedCamera2DLatch::notePresented(const Render::RenderSceneView& worldScene,
                                               u64 surfaceRevision) noexcept
{
    if (!worldScene.camera2D().has_value())
    {
        clear();
        return;
    }

    camera_ = *worldScene.camera2D();
    surfaceRevision_ = surfaceRevision;
    ++cameraRevision_;
    if (cameraRevision_ == 0)
    {
        cameraRevision_ = 1;
    }
    hasCamera_ = true;
}

void LastPresentedCamera2DLatch::clear() noexcept
{
    camera_ = {};
    surfaceRevision_ = 0;
    hasCamera_ = false;
    // cameraRevision_ is intentionally retained so callers can detect that a
    // prior latch was cleared without recycling old camera identity bytes.
}

Core::Result<Render::WorldPointerSample>
LastPresentedCamera2DLatch::pickLogical(double logicalX, double logicalY, u32 logicalWidth, u32 logicalHeight,
                                        u64 inputSequence) const noexcept
{
    if (!hasCamera_)
    {
        return Core::failure(RuntimeErrorCode::LifecycleInvariantViolation,
                             "World pointer pick requires a last-presented Camera2D");
    }

    return Render::pickWorldFromLogicalPointer(Render::Camera2DPickQuery{
        .logicalX = logicalX,
        .logicalY = logicalY,
        .logicalWidth = logicalWidth,
        .logicalHeight = logicalHeight,
        .camera = camera_,
        .cameraRevision = cameraRevision_,
        .surfaceRevision = surfaceRevision_,
        .inputSequence = inputSequence,
    });
}

} // namespace Tina::Runtime::Input
