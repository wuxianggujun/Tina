#pragma once

// Two things this proof needs that a bare IRenderDevice borrow cannot give.
//
// First, capture timing. capturePrimaryFrameRgba8 reads the presented backbuffer, so
// calling it from updateFrame reads the frame *before* the one being built. Requesting
// a capture and taking it inside present() is what samples with a pixel gate already
// do, and it is the only way the capture reflects the sprite submitted that frame.
//
// Second, whether the sprite reached the device at all. The scene writer accepting a
// sprite and the backend drawing it are different claims; a flat capture cannot tell
// them apart. Reading the submitted scene's own statistics can.
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderFrameCapture.hpp>
#include <tina/render/RenderScene.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace Tina::Sample {

class PlaybackDeviceProbe final {
  public:
    void set(Render::IRenderDevice* device) noexcept { device_ = device; }
    [[nodiscard]] Render::IRenderDevice* get() const noexcept { return device_; }

    void requestCaptureNextPresent() noexcept { captureNextPresent_ = true; }
    [[nodiscard]] bool consumeCaptureNextPresent() noexcept
    {
        const bool requested = captureNextPresent_;
        captureNextPresent_ = false;
        return requested;
    }

    void setLastCapture(Render::Rgba8FrameCapture capture) noexcept
    {
        lastCapture_ = std::move(capture);
        hasLastCapture_ = true;
    }
    [[nodiscard]] const Render::Rgba8FrameCapture* lastCapture() const noexcept
    {
        return hasLastCapture_ ? &lastCapture_ : nullptr;
    }
    void clearLastCapture() noexcept { hasLastCapture_ = false; }

    // A capture that fails leaves the previous summary in place, which reads exactly
    // like a frame that rendered nothing. Recording the reason separates the two.
    void noteCaptureFailure(std::string_view reason) noexcept
    {
        ++captureFailures_;
        if (firstCaptureFailure_.empty())
        {
            firstCaptureFailure_ = std::string(reason);
        }
    }
    [[nodiscard]] Core::u64 captureFailures() const noexcept { return captureFailures_; }
    [[nodiscard]] const std::string& firstCaptureFailure() const noexcept
    {
        return firstCaptureFailure_;
    }

    void noteSubmittedScene(const Render::RenderSceneView& scene) noexcept
    {
        const auto spriteCount = static_cast<Core::u64>(scene.sprites2D().size());
        maxSubmittedSpriteCount_ =
            spriteCount > maxSubmittedSpriteCount_ ? spriteCount : maxSubmittedSpriteCount_;
        // The sprite pass is scheduled only when the submitted scene carries both a
        // 2D camera and at least one sprite, so a missing camera drops the sprite with
        // no error anywhere.
        if (scene.camera2D().has_value())
        {
            ++framesWithCamera2D_;
        }
    }
    [[nodiscard]] Core::u64 maxSubmittedSpriteCount() const noexcept
    {
        return maxSubmittedSpriteCount_;
    }
    [[nodiscard]] Core::u64 framesWithCamera2D() const noexcept { return framesWithCamera2D_; }

    void noteStatistics(Render::RenderStatistics statistics) noexcept { statistics_ = statistics; }
    [[nodiscard]] Render::RenderStatistics statistics() const noexcept { return statistics_; }

  private:
    Render::IRenderDevice* device_ = nullptr;
    bool captureNextPresent_ = false;
    bool hasLastCapture_ = false;
    Render::Rgba8FrameCapture lastCapture_{};
    Core::u64 captureFailures_ = 0;
    std::string firstCaptureFailure_;
    Core::u64 maxSubmittedSpriteCount_ = 0;
    Core::u64 framesWithCamera2D_ = 0;
    Render::RenderStatistics statistics_{};
};

// Forwards every call the proof or the engine makes on the device. The video decode
// entry points must be forwarded explicitly: their IRenderDevice defaults report the
// feature unsupported, so an unforwarded wrapper would turn a working decoder into a
// clean failure.
class ProbingRenderDevice final : public Render::IRenderDevice {
  public:
    ProbingRenderDevice(std::unique_ptr<Render::IRenderDevice> inner,
                        PlaybackDeviceProbe& probe) noexcept
        : inner_(std::move(inner)), probe_(&probe)
    {
        probe_->set(this);
    }

    ~ProbingRenderDevice() override
    {
        if (probe_ != nullptr && probe_->get() == this)
        {
            probe_->set(nullptr);
        }
    }

    [[nodiscard]] Core::Result<Render::RenderFrameSubmission>
    submitFrame(const Render::RenderFrame& frame) override
    {
        auto submission = inner_->submitFrame(frame);
        if (submission && submission->requiresPresent() && probe_ != nullptr)
        {
            probe_->noteSubmittedScene(frame.primaryWorldScene);
        }
        return submission;
    }

    [[nodiscard]] Core::Status present() override
    {
        // Arming has to happen before the frame is dispatched, so it belongs on this
        // side of inner_->present(), not after it.
        const bool capturing = probe_ != nullptr && probe_->consumeCaptureNextPresent();
        if (capturing)
        {
            if (auto armed = inner_->requestPrimaryFrameCaptureOnNextPresent(); !armed)
            {
                probe_->noteCaptureFailure(armed.error().message);
            }
        }

        auto status = inner_->present();
        if (probe_ != nullptr)
        {
            probe_->noteStatistics(inner_->statistics());
        }
        if (capturing && status)
        {
            auto captured = inner_->collectPrimaryFrameCapture();
            if (!captured.has_value())
            {
                probe_->noteCaptureFailure(captured.error().message);
            }
            else if (captured->empty())
            {
                probe_->noteCaptureFailure("the collected capture was empty");
            }
            else
            {
                probe_->setLastCapture(std::move(*captured));
            }
        }
        return status;
    }

    [[nodiscard]] Render::RenderStatistics statistics() const noexcept override
    {
        return inner_->statistics();
    }

    void shutdown() noexcept override { inner_->shutdown(); }

    [[nodiscard]] Core::Result<Render::Rgba8FrameCapture> capturePrimaryFrameRgba8() override
    {
        return inner_->capturePrimaryFrameRgba8();
    }

    [[nodiscard]] Core::Status requestPrimaryFrameCaptureOnNextPresent() override
    {
        return inner_->requestPrimaryFrameCaptureOnNextPresent();
    }

    [[nodiscard]] Core::Result<Render::Rgba8FrameCapture> collectPrimaryFrameCapture() override
    {
        return inner_->collectPrimaryFrameCapture();
    }

    [[nodiscard]] Core::Result<Render::GpuTextureId>
    createTexture2D(const Render::Texture2DUploadDesc& desc) override
    {
        return inner_->createTexture2D(desc);
    }

    [[nodiscard]] Core::Status validateTexture2D(Render::GpuTextureId texture) const noexcept override
    {
        return inner_->validateTexture2D(texture);
    }

    [[nodiscard]] Core::Status destroyTexture2D(Render::GpuTextureId texture) noexcept override
    {
        return inner_->destroyTexture2D(texture);
    }

    [[nodiscard]] Core::Status retireTexture2D(Render::GpuTextureId texture,
                                               Render::FramePin& completionPin) noexcept override
    {
        return inner_->retireTexture2D(texture, completionPin);
    }

    [[nodiscard]] Core::Status setTexture2DBinding(Core::u32 spriteKey,
                                                   Render::GpuTextureId texture) noexcept override
    {
        return inner_->setTexture2DBinding(spriteKey, texture);
    }

    [[nodiscard]] Core::Status drainGpuRetirements() noexcept override
    {
        return inner_->drainGpuRetirements();
    }

    [[nodiscard]] Render::VideoDecodeCapabilities videoDecodeCapabilities() const noexcept override
    {
        return inner_->videoDecodeCapabilities();
    }

    [[nodiscard]] bool isVideoDecodeSupported(
        const Render::VideoDecodeTextureDesc& desc) const noexcept override
    {
        return inner_->isVideoDecodeSupported(desc);
    }

    [[nodiscard]] Core::Result<Render::GpuTextureId> createVideoDecodeTexture(
        const Render::VideoDecodeTextureDesc& desc) override
    {
        return inner_->createVideoDecodeTexture(desc);
    }

    [[nodiscard]] Core::Status submitVideoDecodeFrame(
        Render::GpuTextureId texture,
        const Render::VideoDecodeSubmission& submission) noexcept override
    {
        return inner_->submitVideoDecodeFrame(texture, submission);
    }

  private:
    std::unique_ptr<Render::IRenderDevice> inner_;
    PlaybackDeviceProbe* probe_ = nullptr;
};

[[nodiscard]] inline Core::Result<std::unique_ptr<Render::IRenderDevice>>
wrapProbingRenderDevice(std::unique_ptr<Render::IRenderDevice> device, PlaybackDeviceProbe& probe)
{
    std::unique_ptr<Render::IRenderDevice> probing =
        std::make_unique<ProbingRenderDevice>(std::move(device), probe);
    return probing;
}

} // namespace Tina::Sample
