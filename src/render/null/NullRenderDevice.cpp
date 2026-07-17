#include <tina/render/RenderErrors.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include "../RenderSurfaceStateTracker.hpp"

#include <memory>
#include <utility>

namespace Tina::Render {
namespace {

class NullRenderDevice final : public IRenderDevice {
  public:
    explicit NullRenderDevice(Detail::RenderSurfaceStateTracker surfaceStateTracker) noexcept
        : surfaceStateTracker_(std::move(surfaceStateTracker))
    {
    }

    [[nodiscard]] Core::Result<RenderFrameSubmission> submitFrame(const RenderFrame& frame) override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (frameOpen_)
        {
            return Core::failure(RenderErrorCode::FrameAlreadyOpen,
                                 "The previously submitted frame has not been presented");
        }
        if (frame.frameIndex != nextFrameIndex_)
        {
            return Core::failure(RenderErrorCode::UnexpectedFrameIndex,
                                 "Render frame indices must be contiguous and begin at zero");
        }

        if (auto status = surfaceStateTracker_.validateAndCommit(frame.primaryWindowSurface); !status)
        {
            return Core::failure(std::move(status.error()));
        }

        ++nextFrameIndex_;
        if (frame.primaryWindowSurface.has_value() &&
            frame.primaryWindowSurface->availability == RenderSurfaceAvailability::Suspended)
        {
            ++statistics_.skippedSuspendedSurfaceFrames;
            return RenderFrameSubmission::SkippedSuspendedSurface();
        }

        frameOpen_ = true;
        ++statistics_.submitted;
        return RenderFrameSubmission::Submitted(nextSubmissionIndex_++);
    }

    [[nodiscard]] Core::Status present() override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (!frameOpen_)
        {
            return Core::failure(RenderErrorCode::NoFrameSubmitted,
                                 "A frame must be submitted before it can be presented");
        }

        frameOpen_ = false;
        ++statistics_.presented;
        return Core::success();
    }

    [[nodiscard]] RenderStatistics statistics() const noexcept override
    {
        return statistics_;
    }

    void shutdown() noexcept override
    {
        stopped_ = true;
        frameOpen_ = false;
        statistics_.liveResources = 0;
    }

  private:
    Detail::RenderSurfaceStateTracker surfaceStateTracker_;
    RenderStatistics statistics_{};
    u64 nextFrameIndex_ = 0;
    u64 nextSubmissionIndex_ = 0;
    bool frameOpen_ = false;
    bool stopped_ = false;
};

} // namespace

Core::Result<std::unique_ptr<IRenderDevice>> createNullRenderDevice(const RenderDeviceCreateParams& params)
{
    auto surfaceStateTracker = Detail::RenderSurfaceStateTracker::create(params.initialPrimaryWindowSurface);
    if (!surfaceStateTracker)
    {
        return Core::failure(std::move(surfaceStateTracker.error()));
    }

    std::unique_ptr<IRenderDevice> renderDevice = std::make_unique<NullRenderDevice>(std::move(*surfaceStateTracker));
    return renderDevice;
}

} // namespace Tina::Render
