#include <tina/render/RenderErrors.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include <memory>

namespace Tina::Render {
namespace {

class NullRenderDevice final : public IRenderDevice {
public:
    [[nodiscard]] Core::Status submitFrame(const RenderFrame& frame) override
    {
        if (stopped_) {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (frameOpen_) {
            return Core::failure(
                RenderErrorCode::FrameAlreadyOpen,
                "The previously submitted frame has not been presented");
        }
        if (frame.frameIndex != nextFrameIndex_) {
            return Core::failure(
                RenderErrorCode::UnexpectedFrameIndex,
                "Render frame indices must be contiguous and begin at zero");
        }

        frameOpen_ = true;
        ++statistics_.submitted;
        return Core::success();
    }

    [[nodiscard]] Core::Status present() override
    {
        if (stopped_) {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (!frameOpen_) {
            return Core::failure(
                RenderErrorCode::NoFrameSubmitted,
                "A frame must be submitted before it can be presented");
        }

        frameOpen_ = false;
        ++nextFrameIndex_;
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
    RenderStatistics statistics_{};
    u64 nextFrameIndex_ = 0;
    bool frameOpen_ = false;
    bool stopped_ = false;
};

} // namespace

Core::Result<std::unique_ptr<IRenderDevice>> createNullRenderDevice(
    const RenderDeviceCreateParams& params)
{
    static_cast<void>(params);

    std::unique_ptr<IRenderDevice> renderDevice = std::make_unique<NullRenderDevice>();
    return renderDevice;
}

} // namespace Tina::Render
